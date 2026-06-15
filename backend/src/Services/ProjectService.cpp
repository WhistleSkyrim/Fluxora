#include "FluxoraCore/Services/ProjectService.hpp"

#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Services/TemplateService.hpp"
#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"
#include "FluxoraCore/Support/JsonWriter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fluxora
{
    namespace
    {
        constexpr std::wstring_view fallbackProjectFolderName = L"New Build";
        constexpr std::wstring_view fallbackProfileName = L"Default";
        constexpr std::wstring_view buildManifestsFolderName = L"Builds";
        constexpr std::wstring_view manifestFileExtension = L".json";
        constexpr std::wstring_view invalidFolderCharacters = L"<>:\"/\\|?*";

#ifdef _WIN32
        std::wstring readEnvironmentVariable(const wchar_t* name)
        {
            const DWORD requiredLength = GetEnvironmentVariableW(name, nullptr, 0);
            if (requiredLength == 0)
            {
                return {};
            }

            std::wstring value(requiredLength, L'\0');
            const DWORD actualLength = GetEnvironmentVariableW(name, value.data(), requiredLength);
            if (actualLength == 0 || actualLength >= requiredLength)
            {
                return {};
            }

            value.resize(actualLength);
            return value;
        }
#endif

        std::wstring trimFolderName(std::wstring value)
        {
            const auto first = value.find_first_not_of(L" .");
            if (first == std::wstring::npos)
            {
                return {};
            }

            const auto last = value.find_last_not_of(L" .");
            return value.substr(first, last - first + 1);
        }

        std::wstring toLower(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return value;
        }

        bool equalsIgnoreCase(std::wstring_view left, std::wstring_view right)
        {
            return toLower(std::wstring(left)) == toLower(std::wstring(right));
        }

        bool isDirectory(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && std::filesystem::is_directory(path, error);
        }

        bool isRegularFile(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && std::filesystem::is_regular_file(path, error);
        }

        bool hasExecutableExtension(const std::filesystem::path& path)
        {
            return equalsIgnoreCase(path.extension().wstring(), L".exe");
        }

        std::wstring fileNameWithoutExtension(const std::filesystem::path& path)
        {
            const std::wstring stem = path.stem().wstring();
            return stem.empty() ? path.filename().wstring() : stem;
        }

        std::wstring sanitizeFolderName(std::wstring_view name)
        {
            std::wstring sanitized;
            sanitized.reserve(name.size());

            for (wchar_t character : name)
            {
                if (character < 32 || invalidFolderCharacters.find(character) != std::wstring_view::npos)
                {
                    sanitized.push_back(L'-');
                    continue;
                }

                sanitized.push_back(character);
            }

            sanitized = trimFolderName(std::move(sanitized));
            if (sanitized.empty())
            {
                return std::wstring(fallbackProjectFolderName);
            }

            return sanitized;
        }

        std::filesystem::path normalizeRootDirectory(const std::filesystem::path& root)
        {
            std::wstring rootText = root.wstring();
            if (rootText.size() == 2 && rootText[1] == L':')
            {
                rootText.push_back(L'\\');
            }

            return std::filesystem::absolute(std::filesystem::path(rootText));
        }

        std::filesystem::path resolveFluxoraDataDirectory()
        {
#ifdef _WIN32
            if (const std::wstring appData = readEnvironmentVariable(L"APPDATA"); !appData.empty())
            {
                return std::filesystem::path(appData) / L"Fluxora";
            }

            if (const std::wstring userProfile = readEnvironmentVariable(L"USERPROFILE"); !userProfile.empty())
            {
                return std::filesystem::path(userProfile) / L"AppData" / L"Roaming" / L"Fluxora";
            }
#else
            if (const char* xdgDataHome = std::getenv("XDG_DATA_HOME");
                xdgDataHome != nullptr && xdgDataHome[0] != '\0')
            {
                return std::filesystem::path(xdgDataHome) / "Fluxora";
            }

            if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
            {
                return std::filesystem::path(home) / ".local" / "share" / "Fluxora";
            }
#endif

            return std::filesystem::temp_directory_path() / L"Fluxora";
        }

        std::filesystem::path resolveBuildManifestDirectory()
        {
            return resolveFluxoraDataDirectory() / std::filesystem::path(buildManifestsFolderName);
        }

        std::filesystem::path buildManifestPath(std::wstring_view projectName)
        {
            const std::filesystem::path directory = resolveBuildManifestDirectory();
            const std::wstring fileStem = sanitizeFolderName(projectName);
            std::filesystem::path candidate =
                directory / std::filesystem::path(fileStem + std::wstring(manifestFileExtension));

            for (int index = 2; std::filesystem::exists(candidate); ++index)
            {
                candidate = directory / std::filesystem::path(
                    fileStem + L"-" + std::to_wstring(index) + std::wstring(manifestFileExtension));
            }

            return std::filesystem::absolute(candidate);
        }

        std::wstring normalizePathForComparison(const std::filesystem::path& path)
        {
            std::wstring text = std::filesystem::absolute(path).lexically_normal().wstring();
            while (text.size() > 1 && (text.back() == L'\\' || text.back() == L'/'))
            {
                text.pop_back();
            }

#ifdef _WIN32
            std::transform(text.begin(), text.end(), text.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
#endif

            return text;
        }

        bool isSamePath(
            const std::filesystem::path& left,
            const std::filesystem::path& right)
        {
            if (left.empty() || right.empty())
            {
                return false;
            }

            return normalizePathForComparison(left) == normalizePathForComparison(right);
        }

        std::filesystem::path buildManifestPath(
            std::wstring_view projectName,
            const std::filesystem::path& currentManifestPath)
        {
            const std::filesystem::path directory = resolveBuildManifestDirectory();
            const std::wstring fileStem = sanitizeFolderName(projectName);
            const std::filesystem::path current = std::filesystem::absolute(currentManifestPath);
            std::filesystem::path candidate =
                directory / std::filesystem::path(fileStem + std::wstring(manifestFileExtension));

            for (int index = 2;
                 std::filesystem::exists(candidate) && !isSamePath(candidate, current);
                 ++index)
            {
                candidate = directory / std::filesystem::path(
                    fileStem + L"-" + std::to_wstring(index) + std::wstring(manifestFileExtension));
            }

            return std::filesystem::absolute(candidate);
        }

        std::string toUtf8(const std::wstring& value)
        {
#ifdef _WIN32
            if (value.empty())
            {
                return {};
            }

            const int size = WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
            return out;
#else
            return std::string(value.begin(), value.end());
#endif
        }

        std::wstring fromUtf8(const std::string& value)
        {
#ifdef _WIN32
            if (value.empty())
            {
                return {};
            }

            const int size = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            if (size <= 0)
            {
                throw std::invalid_argument("Build config is not valid UTF-8.");
            }

            std::wstring out(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                out.data(),
                size);
            return out;
#else
            return std::wstring(value.begin(), value.end());
#endif
        }

        std::string readTextFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::in | std::ios::binary);
            if (!file)
            {
                throw std::invalid_argument("Build config could not be opened.");
            }

            return std::string(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
        }

        void writeTextFile(const std::filesystem::path& path, const std::string& content)
        {
            const std::filesystem::path parent = path.parent_path();
            if (!parent.empty())
            {
                std::filesystem::create_directories(parent);
            }

            std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Failed to create project file.");
            }

            file.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        JsonValue parseJsonConfig(const std::string& content)
        {
            try
            {
                return JsonReader::parse(fromUtf8(content));
            }
            catch (const std::exception& exception)
            {
                throw std::invalid_argument(std::string("Build config is invalid: ") + exception.what());
            }
        }

        void writeJsonValue(JsonWriter& writer, const JsonValue& value)
        {
            switch (value.type())
            {
            case JsonValue::Type::Null:
                writer.nullValue();
                break;
            case JsonValue::Type::String:
                writer.value(value.asString());
                break;
            case JsonValue::Type::Number:
                writer.numberValue(value.asNumber());
                break;
            case JsonValue::Type::Boolean:
                writer.value(value.asBoolean());
                break;
            case JsonValue::Type::Object:
                writer.beginObject();
                for (const auto& [key, item] : value.asObject())
                {
                    writer.key(key);
                    writeJsonValue(writer, item);
                }
                writer.endObject();
                break;
            case JsonValue::Type::Array:
                writer.beginArray();
                for (const JsonValue& item : value.asArray())
                {
                    writeJsonValue(writer, item);
                }
                writer.endArray();
                break;
            }
        }

        std::string serializeJson(const JsonValue& value)
        {
            JsonWriter writer;
            writeJsonValue(writer, value);
            return toUtf8(writer.str());
        }

        const JsonValue& requireObject(const JsonValue& value)
        {
            if (!value.isObject())
            {
                throw std::invalid_argument("Build config root must be a JSON object.");
            }

            return value;
        }

        std::wstring readStringOrDefault(
            const JsonValue& object,
            std::wstring_view field,
            std::wstring_view fallback = L"")
        {
            const JsonValue* value = object.find(field);
            if (value == nullptr || value->isNull())
            {
                return std::wstring(fallback);
            }

            if (!value->isString())
            {
                throw std::invalid_argument("Build config has a field with the wrong type.");
            }

            return value->asString();
        }

        std::wstring readRequiredString(const JsonValue& object, std::wstring_view field)
        {
            std::wstring value = readStringOrDefault(object, field);
            if (value.empty())
            {
                throw std::invalid_argument("Build config is missing a required field.");
            }

            return value;
        }

        std::optional<std::vector<std::wstring>> readStringArrayField(
            const JsonValue& object,
            std::wstring_view field)
        {
            const JsonValue* value = object.find(field);
            if (value == nullptr || value->isNull())
            {
                return std::nullopt;
            }

            if (!value->isArray())
            {
                throw std::invalid_argument("Build config has an array field with the wrong type.");
            }

            std::vector<std::wstring> strings;
            for (const JsonValue& item : value->asArray())
            {
                if (!item.isString())
                {
                    throw std::invalid_argument("Build config array must contain strings.");
                }

                strings.push_back(item.asString());
            }

            return strings;
        }

        std::optional<std::vector<TemplateCapability>> readCapabilitiesField(const JsonValue& object)
        {
            const JsonValue* value = object.find(L"capabilities");
            if (value == nullptr || value->isNull())
            {
                return std::nullopt;
            }

            if (!value->isArray())
            {
                throw std::invalid_argument("Build config capabilities must be an array.");
            }

            std::vector<TemplateCapability> capabilities;
            for (const JsonValue& item : value->asArray())
            {
                if (!item.isObject())
                {
                    throw std::invalid_argument("Build config capability must be an object.");
                }

                capabilities.push_back(TemplateCapability{
                    readRequiredString(item, L"id"),
                    readRequiredString(item, L"displayName"),
                    readStringOrDefault(item, L"description")
                });
            }

            return capabilities;
        }

        std::optional<ScriptExtender> readScriptExtenderField(const JsonValue& object)
        {
            const JsonValue* value = object.find(L"scriptExtender");
            if (value == nullptr)
            {
                return std::nullopt;
            }

            if (value->isNull())
            {
                return ScriptExtender{};
            }

            if (!value->isObject())
            {
                throw std::invalid_argument("Build config script extender must be an object or null.");
            }

            return ScriptExtender{
                readRequiredString(*value, L"name"),
                readRequiredString(*value, L"loaderExecutable"),
                readStringOrDefault(*value, L"website")
            };
        }

        std::filesystem::path resolveManifestPath(
            const std::wstring& text,
            const std::filesystem::path& relativeRoot)
        {
            if (text.empty())
            {
                return {};
            }

            std::filesystem::path path(text);
            if (path.is_relative())
            {
                path = relativeRoot / path;
            }

            return std::filesystem::absolute(path);
        }

        std::filesystem::path resolveInstallRootForProject(const ProjectDescriptor& project)
        {
            if (!project.installRootDirectory.empty())
            {
                return normalizeRootDirectory(project.installRootDirectory);
            }

            const std::filesystem::path parent = project.projectDirectory.parent_path();
            if (parent.empty())
            {
                throw std::invalid_argument("Project install root could not be resolved.");
            }

            return normalizeRootDirectory(parent);
        }

        bool isRootDirectory(const std::filesystem::path& directory)
        {
            const std::filesystem::path absoluteDirectory =
                std::filesystem::absolute(directory).lexically_normal();
            const std::filesystem::path root = absoluteDirectory.root_path();
            return !root.empty() && isSamePath(absoluteDirectory, root);
        }

        std::filesystem::path nativeDeletePath(const std::filesystem::path& path)
        {
#ifdef _WIN32
            std::wstring text = std::filesystem::absolute(path).lexically_normal().wstring();
            if (text.rfind(LR"(\\?\)", 0) == 0)
            {
                return std::filesystem::path(text);
            }

            if (text.rfind(LR"(\\)", 0) == 0)
            {
                return std::filesystem::path(LR"(\\?\UNC\)" + text.substr(2));
            }

            return std::filesystem::path(LR"(\\?\)" + text);
#else
            return path;
#endif
        }

        void ensureSafeDeleteTarget(const ProjectDescriptor& project)
        {
            if (project.projectDirectory.empty())
            {
                throw std::invalid_argument("Project directory is required.");
            }

            const std::filesystem::path projectDirectory =
                std::filesystem::absolute(project.projectDirectory).lexically_normal();
            if (!std::filesystem::exists(projectDirectory) || !std::filesystem::is_directory(projectDirectory))
            {
                throw std::invalid_argument("Build project directory does not exist.");
            }

            if (isRootDirectory(projectDirectory))
            {
                throw std::invalid_argument("Refusing to delete a root directory.");
            }

            if (isSamePath(projectDirectory, resolveFluxoraDataDirectory()) ||
                isSamePath(projectDirectory, resolveBuildManifestDirectory()))
            {
                throw std::invalid_argument("Refusing to delete a Fluxora system directory.");
            }

            if (!project.installRootDirectory.empty() &&
                isSamePath(projectDirectory, project.installRootDirectory))
            {
                throw std::invalid_argument("Refusing to delete the install root directory.");
            }
        }

        struct DeleteFileTask
        {
            std::filesystem::path path;
            std::uintmax_t bytes{0};
        };

        struct DeletePlan
        {
            std::vector<DeleteFileTask> files;
            std::vector<std::filesystem::path> directories;
            std::uintmax_t totalBytes{0};
            std::uintmax_t totalEntries{1};
        };

        struct DeleteProgressState
        {
            std::atomic<std::uintmax_t> deletedBytes{0};
            std::atomic<std::uintmax_t> deletedEntries{0};
            std::mutex mutex;
            std::mutex callbackMutex;
            std::chrono::steady_clock::time_point lastReport{};
            std::uintmax_t lastReportedBytes{0};
            std::uintmax_t lastReportedEntries{0};
        };

        bool isSameOrInsidePath(
            const std::filesystem::path& candidate,
            const std::filesystem::path& root)
        {
            const std::wstring candidateText = normalizePathForComparison(candidate);
            const std::wstring rootText = normalizePathForComparison(root);
            if (candidateText == rootText)
            {
                return true;
            }

            if (candidateText.size() <= rootText.size())
            {
                return false;
            }

            const wchar_t separator = candidateText[rootText.size()];
            return (separator == L'\\' || separator == L'/') &&
                candidateText.compare(0, rootText.size(), rootText) == 0;
        }

        std::wstring relativeDisplayPath(
            const std::filesystem::path& root,
            const std::filesystem::path& path)
        {
            std::error_code error;
            const std::filesystem::path relative = std::filesystem::relative(path, root, error);
            if (!error && !relative.empty())
            {
                return relative.wstring();
            }

            return path.filename().empty() ? path.wstring() : path.filename().wstring();
        }

        int calculateDeletePercent(
            std::uintmax_t deletedBytes,
            std::uintmax_t totalBytes,
            std::uintmax_t deletedEntries,
            std::uintmax_t totalEntries)
        {
            if (totalBytes > 0)
            {
                return static_cast<int>(
                    std::min<std::uintmax_t>(99, (deletedBytes * 100) / totalBytes));
            }

            if (totalEntries > 0)
            {
                return static_cast<int>(
                    std::min<std::uintmax_t>(99, (deletedEntries * 100) / totalEntries));
            }

            return 0;
        }

        void publishDeleteProgress(
            const std::function<void(const ProjectDeleteProgress&)>& progress,
            std::wstring phase,
            std::wstring currentStep,
            std::wstring currentItem,
            int overallPercent,
            std::uintmax_t deletedBytes,
            std::uintmax_t totalBytes,
            std::uintmax_t deletedEntries,
            std::uintmax_t totalEntries)
        {
            if (!progress)
            {
                return;
            }

            progress(ProjectDeleteProgress{
                std::move(phase),
                std::move(currentStep),
                std::move(currentItem),
                std::clamp(overallPercent, 0, 100),
                deletedBytes,
                totalBytes,
                deletedEntries,
                totalEntries
            });
        }

        std::uintmax_t regularFileSize(
            const std::filesystem::path& path,
            const std::filesystem::file_status& status)
        {
            if (!std::filesystem::is_regular_file(status))
            {
                return 0;
            }

            std::error_code error;
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            return error ? 0 : size;
        }

        void clearReadOnlyAttribute(const std::filesystem::path& path);

        std::size_t pathDepth(const std::filesystem::path& path)
        {
            return static_cast<std::size_t>(std::distance(path.begin(), path.end()));
        }

        void publishScanProgress(
            const std::function<void(const ProjectDeleteProgress&)>& progress,
            const std::filesystem::path& root,
            const std::filesystem::path& path,
            std::uintmax_t scannedEntries,
            std::chrono::steady_clock::time_point& lastReport,
            bool force)
        {
            if (!progress)
            {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            if (!force &&
                scannedEntries % 512 != 0 &&
                now - lastReport < std::chrono::milliseconds(250))
            {
                return;
            }

            lastReport = now;
            publishDeleteProgress(
                progress,
                L"scan",
                L"Считаю файлы сборки",
                relativeDisplayPath(root, path),
                0,
                0,
                0,
                scannedEntries,
                0);
        }

        void sortDirectoriesDeepestFirst(std::vector<std::filesystem::path>& directories)
        {
            std::sort(directories.begin(), directories.end(), [](const auto& left, const auto& right)
            {
                const std::size_t leftDepth = pathDepth(left);
                const std::size_t rightDepth = pathDepth(right);
                if (leftDepth != rightDepth)
                {
                    return leftDepth > rightDepth;
                }

                return left.wstring().size() > right.wstring().size();
            });
        }

        DeletePlan collectDeletePlan(
            const std::filesystem::path& root,
            const std::function<void(const ProjectDeleteProgress&)>& progress)
        {
            publishDeleteProgress(
                progress,
                L"scan",
                L"Считаю файлы сборки",
                root.filename().wstring(),
                0,
                0,
                0,
                0,
                0);

            clearReadOnlyAttribute(root);

            DeletePlan plan;
            std::uintmax_t scannedEntries = 0;
            auto lastScanReport = std::chrono::steady_clock::now();
            std::error_code error;
            const std::filesystem::path nativeRoot = nativeDeletePath(root);
            std::filesystem::recursive_directory_iterator iterator(
                nativeRoot,
                std::filesystem::directory_options::skip_permission_denied,
                error);
            if (error)
            {
                throw std::runtime_error(
                    "Failed to scan build folder: " + error.message());
            }

            const std::filesystem::recursive_directory_iterator end;
            for (; iterator != end; iterator.increment(error))
            {
                if (error)
                {
                    throw std::runtime_error(
                        "Failed to scan build folder: " + error.message());
                }

                const std::filesystem::path nativePath = iterator->path();
                const std::filesystem::file_status status = iterator->symlink_status(error);
                if (error)
                {
                    throw std::runtime_error(
                        "Failed to inspect build item: " + error.message());
                }

                const bool isDirectory = std::filesystem::is_directory(status) &&
                    !std::filesystem::is_symlink(status);
                const std::uintmax_t bytes = regularFileSize(nativePath, status);
                ++plan.totalEntries;
                ++scannedEntries;

                const std::filesystem::path relative = std::filesystem::relative(nativePath, nativeRoot, error);
                if (error)
                {
                    throw std::runtime_error(
                        "Failed to resolve build item: " + error.message());
                }

                const std::filesystem::path path = root / relative;
                if (isDirectory)
                {
                    plan.directories.push_back(path);
                }
                else
                {
                    plan.totalBytes += bytes;
                    plan.files.push_back(DeleteFileTask{path, bytes});
                }

                publishScanProgress(
                    progress,
                    root,
                    path,
                    scannedEntries,
                    lastScanReport,
                    false);
            }

            sortDirectoriesDeepestFirst(plan.directories);
            publishScanProgress(
                progress,
                root,
                root,
                scannedEntries,
                lastScanReport,
                true);
            return plan;
        }

        void clearReadOnlyAttribute(const std::filesystem::path& path)
        {
#ifdef _WIN32
            const std::filesystem::path nativePath = nativeDeletePath(path);
            const DWORD attributes = GetFileAttributesW(nativePath.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                (attributes & FILE_ATTRIBUTE_READONLY) == 0)
            {
                return;
            }

            SetFileAttributesW(nativePath.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
#else
            (void)path;
#endif
        }

        void removePathWithRetry(const std::filesystem::path& path)
        {
            constexpr int maxAttempts = 3;

            for (int attempt = 0; attempt < maxAttempts; ++attempt)
            {
                clearReadOnlyAttribute(path);
                const std::filesystem::path nativePath = nativeDeletePath(path);

                std::error_code removeError;
                const bool removed = std::filesystem::remove(nativePath, removeError);
                std::error_code existsError;
                if (!removeError &&
                    (removed || !std::filesystem::exists(nativePath, existsError)))
                {
                    return;
                }

                if (attempt + 1 < maxAttempts)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(80));
                    continue;
                }

                const std::string reason = removeError
                    ? removeError.message()
                    : "path still exists";
                throw std::runtime_error(
                    "Failed to delete \"" + toUtf8(path.wstring()) + "\": " + reason);
            }
        }

        std::size_t deleteWorkerCount(std::size_t fileCount)
        {
            if (fileCount == 0)
            {
                return 0;
            }

            const unsigned int hardwareThreads = std::thread::hardware_concurrency();
            const std::size_t detectedWorkers = hardwareThreads == 0 ? 4 : hardwareThreads;
            const std::size_t workerLimit = (std::min<std::size_t>)(detectedWorkers, 4);
            return (std::max<std::size_t>)(1, (std::min)(workerLimit, fileCount));
        }

        void publishDeleteStateProgress(
            DeleteProgressState& state,
            const std::function<void(const ProjectDeleteProgress&)>& progress,
            const std::filesystem::path& root,
            const std::filesystem::path& currentItem,
            std::uintmax_t totalBytes,
            std::uintmax_t totalEntries,
            std::wstring_view currentStep,
            bool force)
        {
            if (!progress)
            {
                return;
            }

            const std::uintmax_t deletedBytes = state.deletedBytes.load(std::memory_order_relaxed);
            const std::uintmax_t deletedEntries = state.deletedEntries.load(std::memory_order_relaxed);
            const auto now = std::chrono::steady_clock::now();
            constexpr std::uintmax_t minByteInterval = 32ull * 1024ull * 1024ull;
            constexpr std::uintmax_t minEntryInterval = 128;

            ProjectDeleteProgress update;
            std::unique_lock<std::mutex> callbackLock;
            {
                std::lock_guard lock(state.mutex);
                const bool enoughBytes = deletedBytes - state.lastReportedBytes >= minByteInterval;
                const bool enoughEntries = deletedEntries - state.lastReportedEntries >= minEntryInterval;
                if (!force &&
                    !enoughBytes &&
                    !enoughEntries &&
                    now - state.lastReport < std::chrono::milliseconds(150))
                {
                    return;
                }

                state.lastReportedBytes = deletedBytes;
                state.lastReportedEntries = deletedEntries;
                state.lastReport = now;
                callbackLock = std::unique_lock<std::mutex>(state.callbackMutex);
                update = ProjectDeleteProgress{
                    L"delete",
                    std::wstring(currentStep),
                    relativeDisplayPath(root, currentItem),
                    calculateDeletePercent(deletedBytes, totalBytes, deletedEntries, totalEntries),
                    deletedBytes,
                    totalBytes,
                    deletedEntries,
                    totalEntries
                };
            }

            progress(update);
        }

        void recordDeletedEntry(
            DeleteProgressState& state,
            const std::function<void(const ProjectDeleteProgress&)>& progress,
            const std::filesystem::path& root,
            const std::filesystem::path& currentItem,
            std::uintmax_t bytes,
            std::uintmax_t totalBytes,
            std::uintmax_t totalEntries,
            std::wstring_view currentStep,
            bool force = false)
        {
            if (bytes > 0)
            {
                state.deletedBytes.fetch_add(bytes, std::memory_order_relaxed);
            }
            state.deletedEntries.fetch_add(1, std::memory_order_relaxed);
            publishDeleteStateProgress(
                state,
                progress,
                root,
                currentItem,
                totalBytes,
                totalEntries,
                currentStep,
                force);
        }

        void rememberDeleteException(
            std::exception_ptr& firstError,
            std::mutex& errorMutex,
            std::atomic<bool>& shouldStop)
        {
            {
                std::lock_guard lock(errorMutex);
                if (!firstError)
                {
                    firstError = std::current_exception();
                }
            }
            shouldStop.store(true, std::memory_order_relaxed);
        }

        void deleteFilesFromPlan(
            const DeletePlan& plan,
            DeleteProgressState& state,
            const std::function<void(const ProjectDeleteProgress&)>& progress,
            const std::filesystem::path& root,
            std::uintmax_t totalBytes,
            std::uintmax_t totalEntries)
        {
            const std::size_t workerCount = deleteWorkerCount(plan.files.size());
            if (workerCount == 0)
            {
                return;
            }

            std::atomic<std::size_t> nextIndex{0};
            std::atomic<bool> shouldStop{false};
            std::exception_ptr firstError;
            std::mutex errorMutex;

            std::vector<std::thread> workers;
            workers.reserve(workerCount);
            for (std::size_t worker = 0; worker < workerCount; ++worker)
            {
                workers.emplace_back([&]()
                {
                    while (!shouldStop.load(std::memory_order_relaxed))
                    {
                        const std::size_t index = nextIndex.fetch_add(1, std::memory_order_relaxed);
                        if (index >= plan.files.size())
                        {
                            break;
                        }

                        const DeleteFileTask& task = plan.files[index];
                        try
                        {
                            removePathWithRetry(task.path);
                            recordDeletedEntry(
                                state,
                                progress,
                                root,
                                task.path,
                                task.bytes,
                                totalBytes,
                                totalEntries,
                                L"Удаляю файлы сборки");
                        }
                        catch (...)
                        {
                            rememberDeleteException(firstError, errorMutex, shouldStop);
                            break;
                        }
                    }
                });
            }

            for (std::thread& worker : workers)
            {
                worker.join();
            }

            if (firstError)
            {
                std::rethrow_exception(firstError);
            }
        }

        void deleteDirectoriesFromPlan(
            const DeletePlan& plan,
            DeleteProgressState& state,
            const std::function<void(const ProjectDeleteProgress&)>& progress,
            const std::filesystem::path& root,
            std::uintmax_t totalBytes,
            std::uintmax_t totalEntries)
        {
            for (const std::filesystem::path& directory : plan.directories)
            {
                removePathWithRetry(directory);
                recordDeletedEntry(
                    state,
                    progress,
                    root,
                    directory,
                    0,
                    totalBytes,
                    totalEntries,
                    L"Удаляю папки сборки");
            }
        }

        std::vector<std::filesystem::path> collectExternalConfigTargets(
            const std::filesystem::path& requestedConfigPath,
            const ProjectDescriptor& project)
        {
            std::vector<std::filesystem::path> targets;
            const auto addTarget = [&targets, &project](const std::filesystem::path& path)
            {
                if (path.empty() ||
                    isSameOrInsidePath(path, project.projectDirectory) ||
                    !std::filesystem::exists(path) ||
                    !std::filesystem::is_regular_file(path))
                {
                    return;
                }

                const auto duplicate = std::find_if(
                    targets.begin(),
                    targets.end(),
                    [&path](const std::filesystem::path& candidate)
                    {
                        return isSamePath(candidate, path);
                    });
                if (duplicate == targets.end())
                {
                    targets.push_back(std::filesystem::absolute(path));
                }
            };

            addTarget(project.configPath);
            addTarget(requestedConfigPath);
            return targets;
        }

        std::string buildUpdatedManifest(
            const JsonValue& manifest,
            const ProjectDescriptor& project)
        {
            JsonValue::Object object = manifest.asObject();
            object.insert_or_assign(L"name", JsonValue::string(project.name));
            object.insert_or_assign(L"installRoot", JsonValue::string(project.installRootDirectory.wstring()));
            object.insert_or_assign(L"installRootDirectory", JsonValue::string(project.installRootDirectory.wstring()));
            object.insert_or_assign(L"projectDirectory", JsonValue::string(project.projectDirectory.wstring()));
            object.insert_or_assign(L"configPath", JsonValue::string(project.configPath.wstring()));
            return serializeJson(JsonValue::object(std::move(object)));
        }

        void applyStringField(
            const JsonValue& object,
            std::wstring_view field,
            std::wstring& target)
        {
            const JsonValue* value = object.find(field);
            if (value != nullptr && !value->isNull())
            {
                target = readStringOrDefault(object, field);
            }
        }

        void applyStringArrayField(
            const JsonValue& object,
            std::wstring_view field,
            std::vector<std::wstring>& target)
        {
            if (std::optional<std::vector<std::wstring>> value = readStringArrayField(object, field))
            {
                target = std::move(value.value());
            }
        }

        BuildTemplate buildTemplateFromManifest(const JsonValue& manifest, const TemplateService& templates)
        {
            const std::wstring templateId = readRequiredString(manifest, L"templateId");

            BuildTemplate resolved{};
            try
            {
                resolved = templates.resolve(templateId);
            }
            catch (const std::invalid_argument&)
            {
                resolved.id = templateId;
                resolved.displayName = templateId;
                resolved.defaultProfileName = std::wstring(fallbackProfileName);
            }

            resolved.id = templateId;
            applyStringField(manifest, L"baseTemplateId", resolved.baseTemplateId);
            applyStringField(manifest, L"gameName", resolved.gameName);
            applyStringField(manifest, L"dataDirectory", resolved.dataDirectory);
            applyStringField(manifest, L"nexusDomain", resolved.nexusDomain);
            applyStringField(manifest, L"defaultProfile", resolved.defaultProfileName);
            applyStringArrayField(manifest, L"folders", resolved.folders);
            applyStringArrayField(manifest, L"profileFiles", resolved.profileFiles);
            applyStringArrayField(manifest, L"basePlugins", resolved.basePlugins);
            applyStringArrayField(manifest, L"pluginExtensions", resolved.pluginExtensions);
            applyStringArrayField(manifest, L"executables", resolved.executables);

            if (std::optional<std::vector<TemplateCapability>> capabilities = readCapabilitiesField(manifest))
            {
                resolved.capabilities = std::move(capabilities.value());
            }

            if (std::optional<ScriptExtender> scriptExtender = readScriptExtenderField(manifest))
            {
                const ScriptExtender& value = scriptExtender.value();
                if (value.name.empty() && value.loaderExecutable.empty())
                {
                    resolved.scriptExtender = std::nullopt;
                }
                else
                {
                    resolved.scriptExtender = value;
                }
            }

            if (resolved.gameName.empty())
            {
                resolved.gameName = resolved.displayName;
            }
            if (resolved.displayName.empty() || resolved.displayName == templateId)
            {
                resolved.displayName = resolved.gameName.empty() ? templateId : resolved.gameName;
            }

            return resolved;
        }

        std::optional<std::filesystem::path> defaultGameExecutablePath(
            const BuildTemplate& resolved,
            const std::filesystem::path& gameDirectory)
        {
            std::vector<std::wstring> candidateNames;
            if (equalsIgnoreCase(resolved.id, L"skyrimse"))
            {
                candidateNames.push_back(L"SkyrimSE.exe");
                candidateNames.push_back(L"Skyrim SE.exe");
                candidateNames.push_back(L"SkyrimSELauncher.exe");
            }

            candidateNames.push_back(L"SkyrimSE.exe");
            candidateNames.push_back(L"Skyrim SE.exe");
            candidateNames.push_back(L"Fallout4.exe");
            candidateNames.push_back(L"Starfield.exe");

            std::vector<std::wstring> seen;
            for (const std::wstring& candidateName : candidateNames)
            {
                const std::wstring key = toLower(candidateName);
                if (std::find(seen.begin(), seen.end(), key) != seen.end())
                {
                    continue;
                }
                seen.push_back(key);

                const std::filesystem::path candidate = gameDirectory / std::filesystem::path(candidateName);
                if (isRegularFile(candidate))
                {
                    return std::filesystem::path(candidateName);
                }
            }

            return std::nullopt;
        }

        void writeDefaultLaunchExecutable(
            JsonWriter& writer,
            const BuildTemplate& resolved,
            const std::filesystem::path& gameDirectory)
        {
            const std::optional<std::filesystem::path> executablePath =
                defaultGameExecutablePath(resolved, gameDirectory);
            if (!executablePath.has_value())
            {
                return;
            }

            std::wstring displayName = fileNameWithoutExtension(executablePath.value());
            if (equalsIgnoreCase(resolved.id, L"skyrimse"))
            {
                displayName = L"Skyrim Special Edition";
            }

            writer.key(L"launchExecutables").beginArray();
            writer.beginObject();
            writer.field(L"id", L"game");
            writer.field(L"displayName", displayName);
            writer.field(L"executablePath", executablePath->wstring());
            writer.field(L"arguments", L"");
            writer.field(L"workingDirectory", L"");
            writer.endObject();
            writer.endArray();
        }
    }

    ProjectService::ProjectService(Logger& logger, const TemplateService& templates) noexcept
        : logger_(logger),
          templates_(templates)
    {
    }

    void ProjectService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        initialized_ = true;
        logger_.write(LogLevel::Info, "Project service initialized.");
    }

    void ProjectService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        projects_.clear();
        logger_.write(LogLevel::Info, "Project service shut down.");
        initialized_ = false;
    }

    std::filesystem::path ProjectService::buildProjectDirectory(
        const std::filesystem::path& installRootDirectory,
        std::wstring_view projectName) const
    {
        return normalizeRootDirectory(installRootDirectory) / sanitizeFolderName(projectName);
    }

    ProjectDescriptor ProjectService::createProject(const ProjectCreateRequest& request)
    {
        if (request.name.empty())
        {
            throw std::invalid_argument("Project name is required.");
        }

        if (request.templateId.empty())
        {
            throw std::invalid_argument("Game template is required.");
        }

        // Resolve the build template (base + game overlay) up front; an unknown
        // template id throws std::invalid_argument and surfaces to the UI.
        const BuildTemplate resolved = templates_.resolve(request.templateId);

        std::filesystem::path gameDirectory = std::filesystem::absolute(request.gamePath).lexically_normal();
        if (isRegularFile(gameDirectory))
        {
            if (!hasExecutableExtension(gameDirectory))
            {
                throw std::invalid_argument("Game executable path must point to an .exe file.");
            }

            gameDirectory = gameDirectory.parent_path();
        }

        if (!isDirectory(gameDirectory))
        {
            throw std::invalid_argument("Game directory does not exist.");
        }

        const auto normalizedRoot = normalizeRootDirectory(request.installRootDirectory);
        if (!std::filesystem::exists(normalizedRoot) || !std::filesystem::is_directory(normalizedRoot))
        {
            throw std::invalid_argument("Install root directory does not exist.");
        }

        const auto projectDirectory = buildProjectDirectory(normalizedRoot, request.name);
        const auto manifestPath = buildManifestPath(request.name);

        materializeTemplate(projectDirectory, resolved);

        ProjectDescriptor project{
            request.name,
            resolved.id,
            resolved.gameName,
            gameDirectory,
            normalizedRoot,
            projectDirectory,
            manifestPath
        };

        writeBuildManifest(project, resolved);
        InstanceMetadataStore::ensureInstance(project.projectDirectory, resolved.id);

        logger_.write(LogLevel::Info, "Project structure created from template.");
        projects_.push_back(project);
        return project;
    }

    ProjectOpenResult ProjectService::readProjectConfigSummary(const std::filesystem::path& configPath) const
    {
        if (configPath.empty())
        {
            throw std::invalid_argument("Build config path is required.");
        }

        const auto absoluteConfigPath = std::filesystem::absolute(configPath);
        if (!std::filesystem::exists(absoluteConfigPath) || !std::filesystem::is_regular_file(absoluteConfigPath))
        {
            throw std::invalid_argument("Build config file does not exist.");
        }

        const JsonValue manifest = parseJsonConfig(readTextFile(absoluteConfigPath));
        requireObject(manifest);
        const auto manifestDirectory = absoluteConfigPath.parent_path();
        BuildTemplate resolved = buildTemplateFromManifest(manifest, templates_);

        std::filesystem::path projectDirectory = resolveManifestPath(
            readStringOrDefault(manifest, L"projectDirectory", manifestDirectory.wstring()),
            manifestDirectory);
        if (projectDirectory.empty())
        {
            projectDirectory = manifestDirectory;
        }

        if (!std::filesystem::exists(projectDirectory) || !std::filesystem::is_directory(projectDirectory))
        {
            throw std::invalid_argument("Build project directory does not exist.");
        }

        const std::wstring installRootText = readStringOrDefault(
            manifest,
            L"installRoot",
            readStringOrDefault(manifest, L"installRootDirectory"));

        ProjectDescriptor project{
            readRequiredString(manifest, L"name"),
            resolved.id,
            resolved.gameName,
            resolveManifestPath(readStringOrDefault(manifest, L"gamePath"), projectDirectory),
            resolveManifestPath(installRootText, projectDirectory),
            projectDirectory,
            absoluteConfigPath
        };

        return ProjectOpenResult{
            project,
            resolved
        };
    }

    std::vector<ProjectOpenResult> ProjectService::listProjectConfigSummaries(
        const std::filesystem::path& buildConfigsDirectory) const
    {
        std::vector<ProjectOpenResult> summaries;
        if (buildConfigsDirectory.empty())
        {
            return summaries;
        }

        std::error_code error;
        if (!std::filesystem::exists(buildConfigsDirectory, error) ||
            !std::filesystem::is_directory(buildConfigsDirectory, error))
        {
            return summaries;
        }

        std::vector<std::filesystem::directory_entry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(
                 buildConfigsDirectory,
                 std::filesystem::directory_options::skip_permission_denied,
                 error))
        {
            if (error)
            {
                break;
            }

            std::error_code statusError;
            if (!entry.is_regular_file(statusError) ||
                entry.path().extension().wstring() != manifestFileExtension)
            {
                continue;
            }

            entries.push_back(entry);
        }

        std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right)
        {
            std::error_code leftError;
            std::error_code rightError;
            return left.last_write_time(leftError) > right.last_write_time(rightError);
        });

        summaries.reserve(entries.size());
        for (const auto& entry : entries)
        {
            try
            {
                summaries.push_back(readProjectConfigSummary(entry.path()));
            }
            catch (const std::exception&)
            {
                // Ignore stale or unrelated JSON files in the build catalog.
            }
        }

        return summaries;
    }

    ProjectOpenResult ProjectService::openProjectConfig(const std::filesystem::path& configPath)
    {
        ProjectOpenResult result = readProjectConfigSummary(configPath);
        const ProjectDescriptor& project = result.project;
        InstanceMetadataStore::ensureInstance(project.projectDirectory, result.resolvedTemplate.id);

        logger_.write(LogLevel::Info, "Project opened from build config.");
        projects_.push_back(project);

        return result;
    }

    ProjectOpenResult ProjectService::renameProject(
        const std::filesystem::path& configPath,
        std::wstring_view newName)
    {
        std::wstring normalizedName = trimFolderName(std::wstring(newName));
        if (normalizedName.empty())
        {
            throw std::invalid_argument("Project name is required.");
        }

        const auto absoluteConfigPath = std::filesystem::absolute(configPath);
        if (!std::filesystem::exists(absoluteConfigPath) || !std::filesystem::is_regular_file(absoluteConfigPath))
        {
            throw std::invalid_argument("Build config file does not exist.");
        }

        const JsonValue manifest = parseJsonConfig(readTextFile(absoluteConfigPath));
        requireObject(manifest);

        ProjectOpenResult current = openProjectConfig(absoluteConfigPath);
        ProjectDescriptor renamed = current.project;
        const ProjectDescriptor previous = current.project;

        renamed.name = std::move(normalizedName);
        renamed.installRootDirectory = resolveInstallRootForProject(previous);
        renamed.projectDirectory = buildProjectDirectory(renamed.installRootDirectory, renamed.name);
        renamed.configPath = buildManifestPath(renamed.name, previous.configPath);

        const bool movesProjectDirectory = !isSamePath(previous.projectDirectory, renamed.projectDirectory);
        const bool movesConfig = !isSamePath(previous.configPath, renamed.configPath);

        if (movesProjectDirectory && std::filesystem::exists(renamed.projectDirectory))
        {
            throw std::invalid_argument("A build folder with this name already exists.");
        }

        std::filesystem::create_directories(renamed.configPath.parent_path());
        const std::filesystem::path temporaryConfigPath =
            renamed.configPath.parent_path() / std::filesystem::path(L".fluxora-rename.tmp");
        writeTextFile(temporaryConfigPath, buildUpdatedManifest(manifest, renamed));

        try
        {
            if (movesProjectDirectory)
            {
                std::filesystem::create_directories(renamed.projectDirectory.parent_path());
                std::filesystem::rename(previous.projectDirectory, renamed.projectDirectory);
            }

            std::filesystem::copy_file(
                temporaryConfigPath,
                renamed.configPath,
                std::filesystem::copy_options::overwrite_existing);
            std::filesystem::remove(temporaryConfigPath);

            if (movesConfig && std::filesystem::exists(previous.configPath))
            {
                std::filesystem::remove(previous.configPath);
            }
        }
        catch (...)
        {
            std::error_code cleanupError;
            std::filesystem::remove(temporaryConfigPath, cleanupError);
            throw;
        }

        projects_.erase(
            std::remove_if(
                projects_.begin(),
                projects_.end(),
                [&previous, &renamed](const ProjectDescriptor& candidate)
                {
                    return isSamePath(candidate.configPath, previous.configPath) ||
                        isSamePath(candidate.projectDirectory, previous.projectDirectory) ||
                        isSamePath(candidate.configPath, renamed.configPath) ||
                        isSamePath(candidate.projectDirectory, renamed.projectDirectory);
                }),
            projects_.end());
        projects_.push_back(renamed);

        logger_.write(LogLevel::Info, "Project renamed.");
        return ProjectOpenResult{
            renamed,
            current.resolvedTemplate
        };
    }

    void ProjectService::deleteProject(const std::filesystem::path& configPath)
    {
        ProjectDeleteRequest request;
        request.configPath = configPath;
        deleteProject(request);
    }

    void ProjectService::deleteProject(const ProjectDeleteRequest& request)
    {
        if (request.configPath.empty())
        {
            throw std::invalid_argument("Build config path is required.");
        }

        const auto requestedConfigPath = std::filesystem::absolute(request.configPath);
        ProjectOpenResult current = openProjectConfig(requestedConfigPath);
        ensureSafeDeleteTarget(current.project);

        const std::filesystem::path projectDirectory =
            std::filesystem::absolute(current.project.projectDirectory).lexically_normal();
        DeletePlan deletePlan = collectDeletePlan(projectDirectory, request.progress);
        std::vector<std::filesystem::path> configTargets =
            collectExternalConfigTargets(requestedConfigPath, current.project);

        std::uintmax_t totalBytes = deletePlan.totalBytes;
        std::uintmax_t totalEntries = deletePlan.totalEntries + configTargets.size();

        for (const std::filesystem::path& configTarget : configTargets)
        {
            std::error_code error;
            const std::uintmax_t configBytes = std::filesystem::file_size(configTarget, error);
            if (!error)
            {
                totalBytes += configBytes;
            }
        }

        publishDeleteProgress(
            request.progress,
            L"delete",
            L"Удаляю файлы сборки",
            projectDirectory.filename().wstring(),
            1,
            0,
            totalBytes,
            0,
            totalEntries);

        DeleteProgressState deleteState;
        deleteFilesFromPlan(
            deletePlan,
            deleteState,
            request.progress,
            projectDirectory,
            totalBytes,
            totalEntries);
        deleteDirectoriesFromPlan(
            deletePlan,
            deleteState,
            request.progress,
            projectDirectory,
            totalBytes,
            totalEntries);

        removePathWithRetry(projectDirectory);
        recordDeletedEntry(
            deleteState,
            request.progress,
            projectDirectory,
            projectDirectory,
            0,
            totalBytes,
            totalEntries,
            L"Завершаю удаление",
            true);

        for (const std::filesystem::path& configTarget : configTargets)
        {
            std::error_code error;
            const std::uintmax_t configBytes = std::filesystem::file_size(configTarget, error);
            removePathWithRetry(configTarget);
            recordDeletedEntry(
                deleteState,
                request.progress,
                projectDirectory,
                configTarget,
                error ? 0 : configBytes,
                totalBytes,
                totalEntries,
                L"Завершаю удаление",
                true);
        }

        projects_.erase(
            std::remove_if(
                projects_.begin(),
                projects_.end(),
                [&current](const ProjectDescriptor& candidate)
                {
                    return isSamePath(candidate.configPath, current.project.configPath) ||
                        isSamePath(candidate.projectDirectory, current.project.projectDirectory);
                }),
            projects_.end());

        publishDeleteProgress(
            request.progress,
            L"complete",
            L"Удаление завершено",
            current.project.name,
            100,
            totalBytes,
            totalBytes,
            totalEntries,
            totalEntries);

        logger_.write(LogLevel::Info, "Project deleted.");
    }

    void ProjectService::materializeTemplate(
        const std::filesystem::path& projectDirectory,
        const BuildTemplate& resolved) const
    {
        std::filesystem::create_directories(projectDirectory);

        for (const auto& folder : resolved.folders)
        {
            std::filesystem::create_directories(projectDirectory / std::filesystem::path(folder));
        }

        const std::wstring profileName = resolved.defaultProfileName.empty()
            ? std::wstring(fallbackProfileName)
            : resolved.defaultProfileName;
        const auto profileDirectory = projectDirectory / L"profiles" / std::filesystem::path(profileName);
        std::filesystem::create_directories(profileDirectory);

        for (const auto& profileFile : resolved.profileFiles)
        {
            std::string content;

            // The game template's base plugins seed the new profile so the
            // resolved build is immediately game-correct: a Skyrim build lists
            // Skyrim's masters, a build for another game never would.
            if (profileFile == L"plugins.txt")
            {
                for (const auto& plugin : resolved.basePlugins)
                {
                    content += "*" + toUtf8(plugin) + "\n";
                }
            }
            else if (profileFile == L"loadorder.txt")
            {
                for (const auto& plugin : resolved.basePlugins)
                {
                    content += toUtf8(plugin) + "\n";
                }
            }

            writeTextFile(profileDirectory / std::filesystem::path(profileFile), content);
        }
    }

    void ProjectService::writeBuildManifest(
        const ProjectDescriptor& project,
        const BuildTemplate& resolved) const
    {
        JsonWriter writer;
        writer.beginObject();
        writer.field(L"schemaVersion", L"1");
        writer.field(L"name", project.name);
        writer.field(L"templateId", resolved.id);
        writer.field(L"baseTemplateId", resolved.baseTemplateId);
        writer.field(L"gameName", resolved.gameName);
        writer.field(L"gamePath", project.gamePath.wstring());
        writer.field(L"installRoot", project.installRootDirectory.wstring());
        writer.field(L"projectDirectory", project.projectDirectory.wstring());
        writer.field(L"configPath", project.configPath.wstring());
        writer.field(L"dataDirectory", resolved.dataDirectory);
        writer.field(L"nexusDomain", resolved.nexusDomain);
        writer.field(L"defaultProfile", resolved.defaultProfileName);
        writer.stringArray(L"folders", resolved.folders);
        writer.stringArray(L"profileFiles", resolved.profileFiles);
        writer.stringArray(L"basePlugins", resolved.basePlugins);
        writer.stringArray(L"pluginExtensions", resolved.pluginExtensions);
        writer.stringArray(L"executables", resolved.executables);
        writeDefaultLaunchExecutable(writer, resolved, project.gamePath);

        writer.key(L"capabilities").beginArray();
        for (const auto& capability : resolved.capabilities)
        {
            writer.beginObject();
            writer.field(L"id", capability.id);
            writer.field(L"displayName", capability.displayName);
            writer.field(L"description", capability.description);
            writer.endObject();
        }
        writer.endArray();

        if (resolved.scriptExtender.has_value())
        {
            const ScriptExtender& extender = resolved.scriptExtender.value();
            writer.key(L"scriptExtender").beginObject();
            writer.field(L"name", extender.name);
            writer.field(L"loaderExecutable", extender.loaderExecutable);
            writer.field(L"website", extender.website);
            writer.endObject();
        }
        else
        {
            writer.key(L"scriptExtender").nullValue();
        }

        writer.endObject();

        writeTextFile(project.configPath, toUtf8(writer.str()));
    }

    const std::vector<ProjectDescriptor>& ProjectService::projects() const noexcept
    {
        return projects_;
    }

    bool ProjectService::isInitialized() const noexcept
    {
        return initialized_;
    }
}
