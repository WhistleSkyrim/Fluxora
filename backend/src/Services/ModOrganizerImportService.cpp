#include "FluxoraCore/Services/ModOrganizerImportService.hpp"

#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/BulkFileCopyService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Services/ModOrganizerExecutableImportService.hpp"
#include "FluxoraCore/Services/ModOrganizerPluginGroupService.hpp"
#include "FluxoraCore/Services/ModOrganizerProfileOrderService.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"
#include "FluxoraCore/Support/JsonWriter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <cwctype>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <restartmanager.h>
#endif

namespace fluxora
{
    namespace
    {
        constexpr std::wstring_view fallbackProfileName = L"Default";
        constexpr std::wstring_view buildManifestsFolderName = L"Builds";
        constexpr std::wstring_view manifestFileExtension = L".json";
        constexpr std::wstring_view invalidFolderCharacters = L"<>:\"/\\|?*";
        constexpr std::wstring_view modKind = L"mod";
        constexpr std::wstring_view separatorKind = L"separator";

        struct ImportedModPlan
        {
            std::wstring folderName;
            bool isEnabled{true};
        };

        struct ModOrganizerSourceLayout
        {
            std::filesystem::path rootDirectory;
            std::filesystem::path modsDirectory;
            std::filesystem::path profilesDirectory;
            std::filesystem::path downloadsDirectory;
            std::filesystem::path overwriteDirectory;
        };

        struct ImportPlan
        {
            ModOrganizerImportAnalysis analysis;
            ModOrganizerSourceLayout sourceLayout;
            BuildTemplate resolvedTemplate;
            std::vector<ImportedModPlan> mods;
            std::vector<ProfileOrderImportItemRecord> orderItems;
            std::vector<ProfilePluginOrderImportItemRecord> pluginOrderItems;
            ModOrganizerExecutableImportPlan executableImport;
        };

        std::uintmax_t directorySize(
            const std::filesystem::path& directory,
            const std::function<bool(const std::filesystem::path&)>& shouldSkip);

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

        std::wstring trim(std::wstring value)
        {
            const auto first = value.find_first_not_of(L" \t\r\n");
            if (first == std::wstring::npos)
            {
                return {};
            }

            const auto last = value.find_last_not_of(L" \t\r\n");
            return value.substr(first, last - first + 1);
        }

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

        bool endsWithIgnoreCase(std::wstring_view value, std::wstring_view suffix)
        {
            if (value.size() < suffix.size())
            {
                return false;
            }

            return equalsIgnoreCase(value.substr(value.size() - suffix.size()), suffix);
        }

        std::wstring stripQuotes(std::wstring value)
        {
            value = trim(std::move(value));
            if (value.size() >= 2 &&
                ((value.front() == L'"' && value.back() == L'"') ||
                 (value.front() == L'\'' && value.back() == L'\'')))
            {
                return value.substr(1, value.size() - 2);
            }

            return value;
        }

        void replaceAllIgnoreCase(
            std::wstring& value,
            std::wstring_view token,
            const std::wstring& replacement)
        {
            if (token.empty())
            {
                return;
            }

            std::wstring lowerValue = toLower(value);
            const std::wstring lowerToken = toLower(std::wstring(token));
            const std::wstring lowerReplacement = toLower(replacement);

            std::size_t position = 0;
            while ((position = lowerValue.find(lowerToken, position)) != std::wstring::npos)
            {
                value.replace(position, token.size(), replacement);
                lowerValue.replace(position, token.size(), lowerReplacement);
                position += replacement.size();
            }
        }

        std::wstring expandEnvironmentVariables(std::wstring value)
        {
#ifdef _WIN32
            const DWORD requiredLength = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
            if (requiredLength == 0)
            {
                return value;
            }

            std::wstring expanded(requiredLength, L'\0');
            const DWORD actualLength =
                ExpandEnvironmentStringsW(value.c_str(), expanded.data(), requiredLength);
            if (actualLength == 0 || actualLength > requiredLength)
            {
                return value;
            }

            expanded.resize(actualLength == 0 ? 0 : actualLength - 1);
            return expanded;
#else
            return value;
#endif
        }

        std::wstring sanitizeFolderName(std::wstring_view name, std::wstring_view fallback)
        {
            std::wstring sanitized;
            sanitized.reserve(name.size());

            for (wchar_t character : name)
            {
                sanitized.push_back(
                    character < 32 || invalidFolderCharacters.find(character) != std::wstring_view::npos
                        ? L'-'
                        : character);
            }

            sanitized = trimFolderName(std::move(sanitized));
            return sanitized.empty() ? std::wstring(fallback) : sanitized;
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

        std::filesystem::path uniquePath(
            const std::filesystem::path& directory,
            std::wstring_view fileStem,
            std::wstring_view extension = L"")
        {
            std::filesystem::path candidate =
                directory / std::filesystem::path(std::wstring(fileStem) + std::wstring(extension));

            for (int index = 2; std::filesystem::exists(candidate); ++index)
            {
                candidate = directory / std::filesystem::path(
                    std::wstring(fileStem) + L"-" + std::to_wstring(index) + std::wstring(extension));
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
            if (size <= 0)
            {
                throw std::runtime_error("Failed to encode text as UTF-8.");
            }

            std::string out(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
            return out;
#else
            return std::string(value.begin(), value.end());
#endif
        }

        std::wstring fromBytes(const std::string& value)
        {
#ifdef _WIN32
            if (value.empty())
            {
                return {};
            }

            int size = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            UINT codePage = CP_UTF8;
            DWORD flags = MB_ERR_INVALID_CHARS;
            if (size <= 0)
            {
                codePage = CP_ACP;
                flags = 0;
                size = MultiByteToWideChar(
                    codePage,
                    flags,
                    value.data(),
                    static_cast<int>(value.size()),
                    nullptr,
                    0);
            }

            if (size <= 0)
            {
                throw std::invalid_argument("Text file encoding is not supported.");
            }

            std::wstring out(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(
                codePage,
                flags,
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
                return {};
            }

            return std::string(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
        }

        void writeTextFile(const std::filesystem::path& path, const std::string& content)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }

            std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Failed to write import file.");
            }

            file.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        std::map<std::wstring, std::wstring> readIni(const std::filesystem::path& path)
        {
            std::map<std::wstring, std::wstring> values;
            const std::string bytes = readTextFile(path);
            if (bytes.empty())
            {
                return values;
            }

            std::wstring text = fromBytes(bytes);
            if (!text.empty() && text.front() == 0xFEFF)
            {
                text.erase(text.begin());
            }

            std::wstring section;
            std::size_t lineStart = 0;
            while (lineStart <= text.size())
            {
                std::size_t lineEnd = text.find_first_of(L"\r\n", lineStart);
                std::wstring line = lineEnd == std::wstring::npos
                    ? text.substr(lineStart)
                    : text.substr(lineStart, lineEnd - lineStart);
                lineStart = lineEnd == std::wstring::npos
                    ? text.size() + 1
                    : text.find_first_not_of(L"\r\n", lineEnd);
                if (lineStart == std::wstring::npos)
                {
                    lineStart = text.size() + 1;
                }

                line = trim(std::move(line));
                if (line.empty() || line.front() == L';' || line.front() == L'#')
                {
                    continue;
                }

                if (line.size() >= 2 && line.front() == L'[' && line.back() == L']')
                {
                    section = toLower(trim(line.substr(1, line.size() - 2)));
                    continue;
                }

                const std::size_t equals = line.find(L'=');
                if (equals == std::wstring::npos)
                {
                    continue;
                }

                const std::wstring key = toLower(trim(line.substr(0, equals)));
                const std::wstring value = stripQuotes(line.substr(equals + 1));
                values[key] = value;
                if (!section.empty())
                {
                    values[section + L"." + key] = value;
                }
            }

            return values;
        }

        std::wstring iniValue(
            const std::map<std::wstring, std::wstring>& values,
            std::initializer_list<std::wstring_view> keys)
        {
            for (std::wstring_view key : keys)
            {
                const auto found = values.find(toLower(std::wstring(key)));
                if (found != values.end() && !found->second.empty())
                {
                    return found->second;
                }
            }

            return {};
        }

        bool isSeparatorName(std::wstring_view name)
        {
            return endsWithIgnoreCase(name, L"_separator");
        }

        std::wstring separatorTitle(std::wstring name)
        {
            if (isSeparatorName(name))
            {
                name = name.substr(0, name.size() - std::wstring_view(L"_separator").size());
            }

            std::replace(name.begin(), name.end(), L'_', L' ');
            name = trim(std::move(name));
            while (!name.empty() && (name.front() == L'+' || name.front() == L'-'))
            {
                name.erase(name.begin());
                name = trim(std::move(name));
            }

            return name.empty() ? std::wstring(L"Раздел") : name;
        }

        std::filesystem::path nearestExistingPath(std::filesystem::path path)
        {
            if (path.empty())
            {
                return {};
            }

            path = std::filesystem::absolute(path);
            while (!path.empty() && !std::filesystem::exists(path))
            {
                path = path.parent_path();
            }

            return path;
        }

        std::uintmax_t availableSpaceFor(const std::filesystem::path& path)
        {
            const std::filesystem::path existing = nearestExistingPath(path);
            if (existing.empty())
            {
                return 0;
            }

            std::error_code error;
            const std::filesystem::space_info info = std::filesystem::space(existing, error);
            return error ? 0 : info.available;
        }

        std::tm localTimeNow()
        {
            const std::time_t now = std::time(nullptr);
            std::tm local{};
#ifdef _WIN32
            localtime_s(&local, &now);
#else
            localtime_r(&now, &local);
#endif
            return local;
        }

        std::string logTimestamp()
        {
            const std::tm local = localTimeNow();
            std::ostringstream stream;
            stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
            return stream.str();
        }

        std::string_view logLevelLabel(LogLevel level) noexcept
        {
            switch (level)
            {
            case LogLevel::Debug:
                return "DEBUG";
            case LogLevel::Info:
                return "INFO";
            case LogLevel::Warning:
                return "WARNING";
            case LogLevel::Error:
                return "ERROR";
            }

            return "UNKNOWN";
        }

        std::wstring compactLogTimestamp()
        {
            const std::tm local = localTimeNow();
            std::wostringstream stream;
            stream << std::put_time(&local, L"%Y%m%d-%H%M%S");
            return stream.str();
        }

        std::string pathForLog(const std::filesystem::path& path)
        {
            return toUtf8(path.wstring());
        }

        std::filesystem::path resolveModuleDirectory()
        {
#ifdef _WIN32
            static int moduleAnchor = 0;
            HMODULE module = nullptr;
            if (GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&moduleAnchor),
                    &module))
            {
                std::wstring buffer(MAX_PATH, L'\0');
                const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length > 0 && length < buffer.size())
                {
                    buffer.resize(length);
                    return std::filesystem::path(buffer).parent_path();
                }
            }
#endif

            std::error_code error;
            const std::filesystem::path current = std::filesystem::current_path(error);
            return error ? std::filesystem::path{} : current;
        }

        std::filesystem::path createImportLogPath()
        {
            std::vector<std::filesystem::path> candidates;
            if (const std::filesystem::path moduleDirectory = resolveModuleDirectory(); !moduleDirectory.empty())
            {
                candidates.push_back(moduleDirectory / L"logs");
            }
            candidates.push_back(resolveFluxoraDataDirectory() / L"logs");

            const std::wstring fileStem = L"mod-organizer-import-" + compactLogTimestamp();
            for (const std::filesystem::path& directory : candidates)
            {
                std::error_code error;
                std::filesystem::create_directories(directory, error);
                if (error)
                {
                    continue;
                }

                const std::filesystem::path path = uniquePath(directory, fileStem, L".log");
                std::ofstream file(path, std::ios::out | std::ios::app | std::ios::binary);
                if (file)
                {
                    return std::filesystem::absolute(path);
                }
            }

            return {};
        }

        std::string filesystemErrorForLog(const std::error_code& error)
        {
            if (!error)
            {
                return "none";
            }

            std::ostringstream stream;
            stream << "value=" << error.value()
                   << ", category=" << error.category().name()
                   << ", message=" << error.message();
            return stream.str();
        }

        struct ImportDiagnostics
        {
            explicit ImportDiagnostics(std::filesystem::path path)
                : logPath(std::move(path))
            {
            }

            void write(LogLevel level, std::string_view message) const
            {
                if (logPath.empty())
                {
                    return;
                }

                try
                {
                    std::lock_guard lock(mutex);
                    std::ofstream file(logPath, std::ios::out | std::ios::app | std::ios::binary);
                    if (!file)
                    {
                        return;
                    }

                    file << "[" << logTimestamp() << "] "
                         << "[" << logLevelLabel(level) << "] "
                         << message << "\n";
                }
                catch (...)
                {
                }
            }

            void write(std::string_view message) const
            {
                write(LogLevel::Info, message);
            }

            std::string logPathText() const
            {
                return logPath.empty() ? std::string{} : pathForLog(logPath);
            }

            std::filesystem::path logPath;
            mutable std::mutex mutex;
        };

        std::string importDiagnosticsSuffix(const ImportDiagnostics& diagnostics)
        {
            const std::string logPath = diagnostics.logPathText();
            return logPath.empty() ? std::string{} : " diagnosticsLog=\"" + logPath + "\"";
        }

        bool isSameOrInside(
            const std::filesystem::path& possibleChild,
            const std::filesystem::path& possibleParent)
        {
            std::error_code error;
            const auto child = std::filesystem::weakly_canonical(possibleChild, error);
            if (error)
            {
                return false;
            }

            const auto parent = std::filesystem::weakly_canonical(possibleParent, error);
            if (error)
            {
                return false;
            }

            auto childIterator = child.begin();
            auto parentIterator = parent.begin();
            for (; parentIterator != parent.end(); ++parentIterator, ++childIterator)
            {
                if (childIterator == child.end() || !equalsIgnoreCase(childIterator->wstring(), parentIterator->wstring()))
                {
                    return false;
                }
            }

            return true;
        }

        bool hasUnsafeSourceTargetOverlap(
            const std::filesystem::path& sourceDirectory,
            const std::filesystem::path& targetProjectDirectory)
        {
            return isSameOrInside(targetProjectDirectory, sourceDirectory) ||
                isSameOrInside(sourceDirectory, targetProjectDirectory);
        }

        bool areSamePath(
            const std::filesystem::path& left,
            const std::filesystem::path& right)
        {
            std::error_code error;
            auto normalizedLeft = std::filesystem::weakly_canonical(left, error);
            if (error)
            {
                normalizedLeft = std::filesystem::absolute(left);
                error.clear();
            }

            auto normalizedRight = std::filesystem::weakly_canonical(right, error);
            if (error)
            {
                normalizedRight = std::filesystem::absolute(right);
            }

            return equalsIgnoreCase(normalizedLeft.wstring(), normalizedRight.wstring());
        }

        std::optional<std::filesystem::path> relativePathIfSameOrInsideLexical(
            const std::filesystem::path& possibleChild,
            const std::filesystem::path& possibleParent)
        {
            std::error_code error;
            std::filesystem::path child = std::filesystem::weakly_canonical(possibleChild, error);
            if (error)
            {
                error.clear();
                child = std::filesystem::absolute(possibleChild, error);
                if (error)
                {
                    child = possibleChild;
                }
                error.clear();
            }

            std::filesystem::path parent = std::filesystem::weakly_canonical(possibleParent, error);
            if (error)
            {
                error.clear();
                parent = std::filesystem::absolute(possibleParent, error);
                if (error)
                {
                    parent = possibleParent;
                }
            }

            child = child.lexically_normal();
            parent = parent.lexically_normal();

            auto childIterator = child.begin();
            auto parentIterator = parent.begin();
            for (; parentIterator != parent.end(); ++parentIterator, ++childIterator)
            {
                if (childIterator == child.end() ||
                    !equalsIgnoreCase(childIterator->wstring(), parentIterator->wstring()))
                {
                    return std::nullopt;
                }
            }

            std::filesystem::path relative;
            for (; childIterator != child.end(); ++childIterator)
            {
                relative /= *childIterator;
            }

            return relative;
        }

        std::filesystem::path remapProjectDestinationToStaging(
            const std::filesystem::path& destination,
            const std::filesystem::path& finalProjectDirectory,
            const std::filesystem::path& stagingProjectDirectory)
        {
            const std::optional<std::filesystem::path> relative =
                relativePathIfSameOrInsideLexical(destination, finalProjectDirectory);
            if (!relative.has_value())
            {
                return destination;
            }

            return relative->empty()
                ? stagingProjectDirectory
                : stagingProjectDirectory / relative.value();
        }

        std::vector<std::filesystem::path> localBuildRoots(const ModOrganizerSourceLayout& layout)
        {
            std::vector<std::filesystem::path> roots;
            const auto addRoot = [&roots](const std::filesystem::path& root)
            {
                if (root.empty())
                {
                    return;
                }

                const auto duplicate = std::find_if(
                    roots.begin(),
                    roots.end(),
                    [&root](const std::filesystem::path& existing)
                    {
                        return areSamePath(existing, root);
                    });
                if (duplicate == roots.end())
                {
                    roots.push_back(root);
                }
            };

            addRoot(layout.rootDirectory);
            addRoot(layout.modsDirectory.parent_path());
            addRoot(layout.profilesDirectory.parent_path());
            addRoot(layout.downloadsDirectory.parent_path());
            addRoot(layout.overwriteDirectory.parent_path());
            return roots;
        }

        std::optional<std::filesystem::path> relativePathInsideLocalBuild(
            const std::filesystem::path& path,
            const ModOrganizerSourceLayout& layout)
        {
            std::optional<std::filesystem::path> bestRelative;
            std::size_t bestRootLength = 0;

            for (const std::filesystem::path& root : localBuildRoots(layout))
            {
                const std::optional<std::filesystem::path> relative =
                    relativePathIfSameOrInsideLexical(path, root);
                if (!relative.has_value() || relative->empty())
                {
                    continue;
                }

                const std::size_t rootLength = root.wstring().size();
                if (!bestRelative.has_value() || rootLength > bestRootLength)
                {
                    bestRelative = relative;
                    bestRootLength = rootLength;
                }
            }

            return bestRelative;
        }

        std::filesystem::path remapImportedGamePath(
            const std::filesystem::path& gamePath,
            const ModOrganizerSourceLayout& sourceLayout,
            const std::filesystem::path& targetProjectDirectory)
        {
            const std::optional<std::filesystem::path> relative =
                relativePathInsideLocalBuild(gamePath, sourceLayout);
            return relative.has_value()
                ? targetProjectDirectory / relative.value()
                : gamePath;
        }

        bool isCoveredByDirectoryCopyRoot(
            const std::filesystem::path& path,
            const std::vector<ModOrganizerExecutableCopyRoot>& copyRoots)
        {
            for (const ModOrganizerExecutableCopyRoot& root : copyRoots)
            {
                if (root.onlyFile.has_value())
                {
                    continue;
                }

                if (relativePathIfSameOrInsideLexical(path, root.sourceDirectory).has_value())
                {
                    return true;
                }
            }

            return false;
        }

        void ensureLocalGameDirectoryIsCopied(
            const std::filesystem::path& gamePath,
            const ModOrganizerSourceLayout& sourceLayout,
            const std::filesystem::path& targetProjectDirectory,
            ModOrganizerExecutableImportPlan& executableImport)
        {
            const std::optional<std::filesystem::path> relative =
                relativePathInsideLocalBuild(gamePath, sourceLayout);
            if (!relative.has_value())
            {
                return;
            }

            std::error_code error;
            if (!std::filesystem::is_directory(gamePath, error))
            {
                return;
            }

            if (isCoveredByDirectoryCopyRoot(gamePath, executableImport.copyRoots))
            {
                return;
            }

            executableImport.copyRoots.push_back(ModOrganizerExecutableCopyRoot{
                gamePath,
                targetProjectDirectory / relative.value(),
                std::nullopt
            });
            executableImport.totalCopyBytes += directorySize(
                gamePath,
                [](const std::filesystem::path&) { return false; });
        }

        std::filesystem::path canonicalOrAbsolute(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto canonical = std::filesystem::weakly_canonical(path, error);
            return error ? std::filesystem::absolute(path) : canonical;
        }

        bool pathExists(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error);
        }

        std::filesystem::path resolveOrganizerPath(
            std::wstring value,
            const std::filesystem::path& sourceDirectory,
            const std::filesystem::path& baseDirectory,
            const std::filesystem::path& gamePath)
        {
            value = stripQuotes(std::move(value));
            if (value.empty())
            {
                return {};
            }

            replaceAllIgnoreCase(value, L"%BASE_DIR%", baseDirectory.wstring());
            replaceAllIgnoreCase(value, L"${BASE_DIR}", baseDirectory.wstring());
            replaceAllIgnoreCase(value, L"{BASE_DIR}", baseDirectory.wstring());
            replaceAllIgnoreCase(value, L"%GAME_PATH%", gamePath.wstring());
            replaceAllIgnoreCase(value, L"${GAME_PATH}", gamePath.wstring());
            replaceAllIgnoreCase(value, L"{GAME_PATH}", gamePath.wstring());

            value = expandEnvironmentVariables(std::move(value));

            std::filesystem::path resolved(value);
            if (resolved.is_relative())
            {
                resolved = baseDirectory.empty()
                    ? sourceDirectory / resolved
                    : baseDirectory / resolved;
            }

            return canonicalOrAbsolute(resolved);
        }

        std::filesystem::path resolveOrganizerDirectory(
            const std::map<std::wstring, std::wstring>& organizerIni,
            std::initializer_list<std::wstring_view> keys,
            const std::filesystem::path& sourceDirectory,
            const std::filesystem::path& baseDirectory,
            const std::filesystem::path& gamePath,
            const std::filesystem::path& fallback)
        {
            const std::wstring configured = iniValue(organizerIni, keys);
            if (configured.empty())
            {
                return canonicalOrAbsolute(fallback);
            }

            const std::filesystem::path resolved =
                resolveOrganizerPath(configured, sourceDirectory, baseDirectory, gamePath);
            if (pathExists(resolved) || !pathExists(fallback))
            {
                return resolved;
            }

            return canonicalOrAbsolute(fallback);
        }

        std::filesystem::path resolveOrganizerBaseDirectory(
            const std::map<std::wstring, std::wstring>& organizerIni,
            const std::filesystem::path& sourceDirectory,
            const std::filesystem::path& gamePath)
        {
            const std::wstring configured = iniValue(
                organizerIni,
                {
                    L"base_directory",
                    L"settings.base_directory",
                    L"basedirectory",
                    L"settings.basedirectory"
                });
            if (configured.empty())
            {
                return canonicalOrAbsolute(sourceDirectory);
            }

            const std::filesystem::path resolved =
                resolveOrganizerPath(configured, sourceDirectory, sourceDirectory, gamePath);
            return pathExists(resolved) ? resolved : canonicalOrAbsolute(sourceDirectory);
        }

        ModOrganizerSourceLayout resolveSourceLayout(
            const std::filesystem::path& sourceDirectory,
            const std::map<std::wstring, std::wstring>& organizerIni,
            const std::filesystem::path& gamePath)
        {
            const std::filesystem::path root = canonicalOrAbsolute(sourceDirectory);
            const std::filesystem::path baseDirectory =
                resolveOrganizerBaseDirectory(organizerIni, root, gamePath);

            return ModOrganizerSourceLayout{
                root,
                resolveOrganizerDirectory(
                    organizerIni,
                    {
                        L"mod_directory",
                        L"settings.mod_directory",
                        L"mods_directory",
                        L"settings.mods_directory",
                        L"moddirectory",
                        L"settings.moddirectory"
                    },
                    root,
                    baseDirectory,
                    gamePath,
                    baseDirectory / L"mods"),
                resolveOrganizerDirectory(
                    organizerIni,
                    {
                        L"profiles_directory",
                        L"settings.profiles_directory",
                        L"profile_directory",
                        L"settings.profile_directory",
                        L"profilesdirectory",
                        L"settings.profilesdirectory"
                    },
                    root,
                    baseDirectory,
                    gamePath,
                    baseDirectory / L"profiles"),
                resolveOrganizerDirectory(
                    organizerIni,
                    {
                        L"download_directory",
                        L"settings.download_directory",
                        L"downloads_directory",
                        L"settings.downloads_directory",
                        L"downloaddirectory",
                        L"settings.downloaddirectory"
                    },
                    root,
                    baseDirectory,
                    gamePath,
                    baseDirectory / L"downloads"),
                resolveOrganizerDirectory(
                    organizerIni,
                    {
                        L"overwrite_directory",
                        L"settings.overwrite_directory",
                        L"overwritedirectory",
                        L"settings.overwritedirectory"
                    },
                    root,
                    baseDirectory,
                    gamePath,
                    baseDirectory / L"overwrite")
            };
        }

        std::wstring defaultProfileFromProfilesDirectory(const std::filesystem::path& profilesDirectory)
        {
            const auto defaultProfile = profilesDirectory / std::filesystem::path(fallbackProfileName);
            if (std::filesystem::exists(defaultProfile) && std::filesystem::is_directory(defaultProfile))
            {
                return std::wstring(fallbackProfileName);
            }

            std::error_code error;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(profilesDirectory, error))
            {
                if (!error && entry.is_directory(error))
                {
                    return entry.path().filename().wstring();
                }
            }

            return std::wstring(fallbackProfileName);
        }

        std::wstring profileFromSourceLayout(
            const ModOrganizerSourceLayout& layout,
            const std::map<std::wstring, std::wstring>& organizerIni)
        {
            std::wstring selectedProfile = iniValue(
                organizerIni,
                {
                    L"selected_profile",
                    L"settings.selected_profile",
                    L"selectedprofile",
                    L"settings.selectedprofile",
                    L"profile",
                    L"settings.profile"
                });
            selectedProfile = trimFolderName(std::move(selectedProfile));
            if (!selectedProfile.empty())
            {
                const std::filesystem::path profileDirectory =
                    layout.profilesDirectory / std::filesystem::path(selectedProfile);
                if (std::filesystem::exists(profileDirectory) && std::filesystem::is_directory(profileDirectory))
                {
                    return selectedProfile;
                }
            }

            return defaultProfileFromProfilesDirectory(layout.profilesDirectory);
        }

        std::vector<std::wstring> enumerateModFolders(const std::filesystem::path& modsDirectory)
        {
            std::vector<std::wstring> folders;
            std::error_code error;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(modsDirectory, error))
            {
                if (error)
                {
                    break;
                }

                if (entry.is_directory(error))
                {
                    const std::wstring name = entry.path().filename().wstring();
                    if (!isSeparatorName(name))
                    {
                        folders.push_back(name);
                    }
                }
            }

            std::sort(folders.begin(), folders.end(), [](const std::wstring& left, const std::wstring& right)
            {
                return toLower(left) < toLower(right);
            });
            return folders;
        }

        bool isRegularFile(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error) && std::filesystem::is_regular_file(path, error);
        }

        bool hasProfileMarker(const std::filesystem::path& profileDirectory)
        {
            return isRegularFile(profileDirectory / L"modlist.txt") ||
                isRegularFile(profileDirectory / L"plugins.txt") ||
                isRegularFile(profileDirectory / L"loadorder.txt");
        }

        bool hasAnyProfile(const std::filesystem::path& profilesDirectory)
        {
            std::error_code error;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(profilesDirectory, error))
            {
                if (error)
                {
                    break;
                }

                if (entry.is_directory(error) && hasProfileMarker(entry.path()))
                {
                    return true;
                }
            }

            return false;
        }

        void validateModOrganizerInstanceStructure(
            const std::filesystem::path& source,
            const ModOrganizerSourceLayout& layout)
        {
            const auto modsDirectory = layout.modsDirectory;
            if (!std::filesystem::exists(modsDirectory) || !std::filesystem::is_directory(modsDirectory))
            {
                throw std::invalid_argument("The selected folder does not look like a Mod Organizer 2 instance: mods folder is missing.");
            }

            const auto profilesDirectory = layout.profilesDirectory;
            if (!std::filesystem::exists(profilesDirectory) || !std::filesystem::is_directory(profilesDirectory))
            {
                throw std::invalid_argument("The selected folder does not look like a Mod Organizer 2 instance: profiles folder is missing.");
            }

            if (!hasAnyProfile(profilesDirectory))
            {
                throw std::invalid_argument("The selected folder does not look like a Mod Organizer 2 instance: no profile contains modlist.txt, plugins.txt, or loadorder.txt.");
            }

            const bool hasOrganizerConfig = isRegularFile(source / L"ModOrganizer.ini") ||
                isRegularFile(source / L"nxmhandler.ini");
            const bool hasPortableExecutable = isRegularFile(source / L"ModOrganizer.exe") ||
                isRegularFile(source / L"usvfs_proxy_x64.exe") ||
                isRegularFile(source / L"usvfs_proxy_x86.exe");

            if (!hasOrganizerConfig && !hasPortableExecutable)
            {
                throw std::invalid_argument("The selected folder does not look like a Mod Organizer 2 instance: ModOrganizer.ini or MO2 files are missing.");
            }
        }

        std::uintmax_t directorySize(
            const std::filesystem::path& directory,
            const std::function<bool(const std::filesystem::path&)>& shouldSkip)
        {
            std::uintmax_t total = 0;
            std::error_code error;
            std::filesystem::recursive_directory_iterator iterator(
                directory,
                std::filesystem::directory_options::skip_permission_denied,
                error);
            const std::filesystem::recursive_directory_iterator end;
            while (!error && iterator != end)
            {
                const std::filesystem::path current = iterator->path();
                if (shouldSkip(current))
                {
                    if (iterator->is_directory(error))
                    {
                        iterator.disable_recursion_pending();
                    }
                    iterator.increment(error);
                    continue;
                }

                if (iterator->is_regular_file(error))
                {
                    const std::uintmax_t size = iterator->file_size(error);
                    if (!error)
                    {
                        total += size;
                    }
                }

                iterator.increment(error);
            }

            return total;
        }

        std::uintmax_t sourceSize(const ModOrganizerSourceLayout& layout)
        {
            std::uintmax_t total = 0;
            const auto modsDirectory = layout.modsDirectory;
            for (const std::wstring& folder : enumerateModFolders(modsDirectory))
            {
                total += directorySize(
                    modsDirectory / std::filesystem::path(folder),
                    [](const std::filesystem::path& path)
                    {
                        return equalsIgnoreCase(path.filename().wstring(), L"meta.ini");
                    });
            }

            const std::array<std::filesystem::path, 3> extraFolders{
                layout.profilesDirectory,
                layout.downloadsDirectory,
                layout.overwriteDirectory
            };
            for (const std::filesystem::path& path : extraFolders)
            {
                if (std::filesystem::exists(path))
                {
                    total += directorySize(path, [](const std::filesystem::path&) { return false; });
                }
            }

            return total;
        }

        BuildTemplate chooseTemplate(
            const TemplateService& templates,
            const std::map<std::wstring, std::wstring>& organizerIni,
            const std::vector<std::wstring>& modFolders,
            const std::filesystem::path& modsDirectory)
        {
            std::wstring detectedGame = iniValue(
                organizerIni,
                {L"gameName", L"General.gameName", L"game", L"General.game"});

            for (const std::wstring& folder : modFolders)
            {
                const auto meta = readIni(modsDirectory / std::filesystem::path(folder) / L"meta.ini");
                if (detectedGame.empty())
                {
                    detectedGame = iniValue(meta, {L"gameName", L"General.gameName", L"game"});
                }
                const std::wstring domain = iniValue(meta, {L"gameDomain", L"nexusDomain"});
                if (equalsIgnoreCase(domain, L"skyrimspecialedition"))
                {
                    return templates.resolve(L"skyrimse");
                }
            }

            if (detectedGame.find(L"Skyrim Special Edition") != std::wstring::npos ||
                detectedGame.find(L"SkyrimSE") != std::wstring::npos)
            {
                return templates.resolve(L"skyrimse");
            }

            const auto& gameTemplates = templates.gameTemplates();
            if (gameTemplates.empty())
            {
                throw std::invalid_argument("No Fluxora game templates are available.");
            }

            return templates.resolve(gameTemplates.front().id);
        }

        std::filesystem::path chooseGamePath(
            const std::map<std::wstring, std::wstring>& organizerIni,
            const ProjectOpenResult* existingProject,
            const std::filesystem::path& sourceDirectory)
        {
            std::wstring gamePath = iniValue(
                organizerIni,
                {L"gamePath", L"General.gamePath", L"managed_game", L"General.managed_game"});

            if (!gamePath.empty())
            {
                std::filesystem::path path(gamePath);
                if (std::filesystem::exists(path))
                {
                    return std::filesystem::absolute(path);
                }
            }

            if (existingProject != nullptr && !existingProject->project.gamePath.empty())
            {
                return existingProject->project.gamePath;
            }

            return std::filesystem::absolute(sourceDirectory);
        }

        ModOrganizerImportAnalysis buildAnalysis(
            const std::filesystem::path& sourceDirectory,
            const std::filesystem::path& destinationRootDirectory,
            const ProjectOpenResult* existingProject,
            const BuildTemplate& resolvedTemplate,
            const ModOrganizerSourceLayout& sourceLayout,
            std::wstring_view profileName,
            const std::vector<std::wstring>& modFolders,
            const std::vector<ProfileOrderImportItemRecord>& orderItems,
            const std::map<std::wstring, std::wstring>& organizerIni)
        {
            ModOrganizerImportAnalysis analysis;
            analysis.sourceDirectory = std::filesystem::absolute(sourceDirectory);
            analysis.willOverwrite = existingProject != nullptr;
            analysis.profileName = std::wstring(profileName);
            analysis.projectName = analysis.sourceDirectory.filename().wstring();
            if (analysis.projectName.empty())
            {
                analysis.projectName = L"MO2 Import";
            }

            analysis.templateId = resolvedTemplate.id;
            analysis.gameName = resolvedTemplate.gameName;
            analysis.gamePath = chooseGamePath(organizerIni, existingProject, sourceDirectory);
            analysis.modCount = static_cast<int>(modFolders.size());
            analysis.separatorCount = static_cast<int>(std::count_if(
                orderItems.begin(),
                orderItems.end(),
                [](const ProfileOrderImportItemRecord& item) { return item.kind == separatorKind; }));
            analysis.totalBytes = sourceSize(sourceLayout);

            if (existingProject != nullptr)
            {
                const std::filesystem::path existingRoot = existingProject->project.installRootDirectory.empty()
                    ? existingProject->project.projectDirectory.parent_path()
                    : existingProject->project.installRootDirectory;
                analysis.destinationRootDirectory = destinationRootDirectory.empty()
                    ? existingRoot
                    : normalizeRootDirectory(destinationRootDirectory);
                const bool keepExistingProjectDirectory =
                    destinationRootDirectory.empty() ||
                    areSamePath(analysis.destinationRootDirectory, existingRoot);
                analysis.targetProjectDirectory = keepExistingProjectDirectory
                    ? existingProject->project.projectDirectory
                    : analysis.destinationRootDirectory /
                        std::filesystem::path(sanitizeFolderName(existingProject->project.name, analysis.projectName));
                analysis.targetConfigPath = existingProject->project.configPath;
            }
            else
            {
                analysis.destinationRootDirectory = normalizeRootDirectory(destinationRootDirectory);
                const std::wstring projectFolder = sanitizeFolderName(analysis.projectName, L"MO2 Import");
                analysis.targetProjectDirectory = uniquePath(analysis.destinationRootDirectory, projectFolder);
                analysis.targetConfigPath = uniquePath(
                    resolveBuildManifestDirectory(),
                    projectFolder,
                    manifestFileExtension);
            }

            analysis.availableBytes = availableSpaceFor(
                analysis.targetProjectDirectory.empty()
                    ? analysis.destinationRootDirectory
                    : analysis.targetProjectDirectory);
            analysis.hasEnoughSpace = analysis.availableBytes >= analysis.totalBytes;

            const bool hasSourceTargetOverlap = hasUnsafeSourceTargetOverlap(
                analysis.sourceDirectory,
                analysis.targetProjectDirectory);

            if (hasSourceTargetOverlap)
            {
                analysis.statusMessage =
                    L"Невозможно перенести сборку: источник и папка назначения совпадают или вложены друг в друга.";
            }
            else if (!analysis.hasEnoughSpace)
            {
                analysis.statusMessage = L"На выбранном диске недостаточно места.";
            }
            else if (analysis.modCount == 0)
            {
                analysis.statusMessage = L"В папке MO2 не найдено модов для переноса.";
            }
            else
            {
                analysis.statusMessage = L"Сборка готова к переносу.";
            }

            if (hasSourceTargetOverlap)
            {
                analysis.warningMessage =
                    L"Выберите отдельную папку назначения. Fluxora не очищает и не изменяет исходную сборку.";
            }
            else if (analysis.willOverwrite)
            {
                analysis.warningMessage =
                    L"Текущая сборка Fluxora будет заменена копией из Mod Organizer 2.";
            }

            analysis.canImport = !hasSourceTargetOverlap && analysis.hasEnoughSpace && analysis.modCount > 0;
            return analysis;
        }

        ImportPlan createPlan(
            const TemplateService& templates,
            ProjectService& projects,
            const std::filesystem::path& sourceDirectory,
            const std::filesystem::path& destinationRootDirectory,
            const std::filesystem::path& existingConfigPath)
        {
            if (sourceDirectory.empty())
            {
                throw std::invalid_argument("Mod Organizer 2 directory is required.");
            }

            const auto source = std::filesystem::absolute(sourceDirectory);
            if (!std::filesystem::exists(source) || !std::filesystem::is_directory(source))
            {
                throw std::invalid_argument("Mod Organizer 2 directory does not exist.");
            }

            std::optional<ProjectOpenResult> existingProject;
            if (!existingConfigPath.empty())
            {
                existingProject = projects.openProjectConfig(existingConfigPath);
            }

            const std::map<std::wstring, std::wstring> organizerIni = readIni(source / L"ModOrganizer.ini");
            const std::filesystem::path gamePath = chooseGamePath(
                organizerIni,
                existingProject ? &existingProject.value() : nullptr,
                source);
            const ModOrganizerSourceLayout sourceLayout = resolveSourceLayout(source, organizerIni, gamePath);
            validateModOrganizerInstanceStructure(source, sourceLayout);
            const auto modsDirectory = sourceLayout.modsDirectory;

            std::vector<std::wstring> modFolders = enumerateModFolders(modsDirectory);
            const std::wstring profileName = profileFromSourceLayout(sourceLayout, organizerIni);
            ModOrganizerProfileOrder profileOrder = ModOrganizerProfileOrderService::read(
                sourceLayout.profilesDirectory / std::filesystem::path(profileName));
            std::map<std::wstring, bool> enabledByFolder = std::move(profileOrder.enabledByFolder);
            std::vector<ProfileOrderImportItemRecord> orderItems = std::move(profileOrder.items);

            std::set<std::wstring> orderedKeys;
            for (const ProfileOrderImportItemRecord& item : orderItems)
            {
                if (item.kind == modKind)
                {
                    orderedKeys.insert(toLower(item.folderName));
                }
            }

            for (const std::wstring& folder : modFolders)
            {
                const std::wstring key = toLower(folder);
                if (orderedKeys.insert(key).second)
                {
                    enabledByFolder.try_emplace(key, true);
                    orderItems.push_back(ProfileOrderImportItemRecord{
                        std::wstring(modKind),
                        folder,
                        {}
                    });
                }
            }

            BuildTemplate resolved = chooseTemplate(templates, organizerIni, modFolders, modsDirectory);
            if (!profileName.empty())
            {
                resolved.defaultProfileName = profileName;
            }

            std::vector<ProfilePluginOrderImportItemRecord> pluginOrderItems =
                ModOrganizerPluginGroupService::read(
                    sourceLayout.profilesDirectory / std::filesystem::path(profileName),
                    resolved);

            ModOrganizerImportAnalysis analysis = buildAnalysis(
                source,
                destinationRootDirectory,
                existingProject ? &existingProject.value() : nullptr,
                resolved,
                sourceLayout,
                profileName,
                modFolders,
                orderItems,
                organizerIni);

            ModOrganizerExecutableImportPlan executableImport =
                ModOrganizerExecutableImportService::createPlan(
                    organizerIni,
                    ModOrganizerExecutableImportContext{
                        sourceLayout.rootDirectory,
                        sourceLayout.modsDirectory,
                        sourceLayout.profilesDirectory,
                        sourceLayout.downloadsDirectory,
                        sourceLayout.overwriteDirectory,
                        analysis.gamePath,
                        analysis.targetProjectDirectory,
                        resolved.id,
                        resolved.scriptExtender.has_value()
                            ? resolved.scriptExtender->loaderExecutable
                            : std::wstring()
                    });
            ensureLocalGameDirectoryIsCopied(
                analysis.gamePath,
                sourceLayout,
                analysis.targetProjectDirectory,
                executableImport);
            analysis.gamePath = remapImportedGamePath(
                analysis.gamePath,
                sourceLayout,
                analysis.targetProjectDirectory);

            if (executableImport.totalCopyBytes > 0)
            {
                analysis.totalBytes += executableImport.totalCopyBytes;
                analysis.hasEnoughSpace = analysis.availableBytes >= analysis.totalBytes;

                const bool hasSourceTargetOverlap = hasUnsafeSourceTargetOverlap(
                    analysis.sourceDirectory,
                    analysis.targetProjectDirectory);
                if (!hasSourceTargetOverlap && !analysis.hasEnoughSpace)
                {
                    analysis.statusMessage = L"На выбранном диске недостаточно места.";
                }
                else if (!hasSourceTargetOverlap && analysis.modCount > 0)
                {
                    analysis.statusMessage = L"Сборка готова к переносу.";
                }

                analysis.canImport =
                    !hasSourceTargetOverlap && analysis.hasEnoughSpace && analysis.modCount > 0;
            }

            std::vector<ImportedModPlan> mods;
            mods.reserve(modFolders.size());
            for (const std::wstring& folder : modFolders)
            {
                const auto enabled = enabledByFolder.find(toLower(folder));
                mods.push_back(ImportedModPlan{
                    folder,
                    enabled == enabledByFolder.end() ? true : enabled->second
                });
            }

            return ImportPlan{
                analysis,
                std::move(sourceLayout),
                std::move(resolved),
                std::move(mods),
                std::move(orderItems),
                std::move(pluginOrderItems),
                std::move(executableImport)
            };
        }

        void materializeTemplate(
            const std::filesystem::path& projectDirectory,
            const BuildTemplate& resolved)
        {
            std::filesystem::create_directories(projectDirectory);
            for (const std::wstring& folder : resolved.folders)
            {
                std::filesystem::create_directories(projectDirectory / std::filesystem::path(folder));
            }

            const std::wstring profileName = resolved.defaultProfileName.empty()
                ? std::wstring(fallbackProfileName)
                : resolved.defaultProfileName;
            const auto profileDirectory = projectDirectory / L"profiles" / std::filesystem::path(profileName);
            std::filesystem::create_directories(profileDirectory);

            for (const std::wstring& profileFile : resolved.profileFiles)
            {
                const std::filesystem::path profileFilePath =
                    profileDirectory / std::filesystem::path(profileFile);
                if (std::filesystem::exists(profileFilePath))
                {
                    continue;
                }

                std::string content;
                if (profileFile == L"plugins.txt")
                {
                    for (const std::wstring& plugin : resolved.basePlugins)
                    {
                        content += "*" + toUtf8(plugin) + "\n";
                    }
                }
                else if (profileFile == L"loadorder.txt")
                {
                    for (const std::wstring& plugin : resolved.basePlugins)
                    {
                        content += toUtf8(plugin) + "\n";
                    }
                }

                writeTextFile(profileFilePath, content);
            }
        }

        void writeLaunchExecutable(JsonWriter& writer, const GameExecutable& executable)
        {
            writer.beginObject();
            writer.field(L"id", executable.id);
            writer.field(L"displayName", executable.displayName);
            writer.field(L"executablePath", executable.executablePath);
            writer.field(L"arguments", executable.arguments);
            writer.field(L"workingDirectory", executable.workingDirectory);
            writer.endObject();
        }

        void writeBuildManifest(
            const ProjectDescriptor& project,
            const BuildTemplate& resolved,
            const std::vector<GameExecutable>& launchExecutables)
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
            writer.field(L"defaultProfile", project.name.empty() ? resolved.defaultProfileName : resolved.defaultProfileName);
            writer.stringArray(L"folders", resolved.folders);
            writer.stringArray(L"profileFiles", resolved.profileFiles);
            writer.stringArray(L"basePlugins", resolved.basePlugins);
            writer.stringArray(L"pluginExtensions", resolved.pluginExtensions);
            writer.stringArray(L"executables", resolved.executables);
            if (!launchExecutables.empty())
            {
                writer.key(L"launchExecutables").beginArray();
                for (const GameExecutable& executable : launchExecutables)
                {
                    writeLaunchExecutable(writer, executable);
                }
                writer.endArray();
            }
            writer.key(L"capabilities").beginArray();
            for (const TemplateCapability& capability : resolved.capabilities)
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

        bool isTransientFilesystemError(const std::error_code& error)
        {
#ifdef _WIN32
            return error.value() == ERROR_SHARING_VIOLATION ||
                error.value() == ERROR_ACCESS_DENIED ||
                error.value() == ERROR_LOCK_VIOLATION;
#else
            (void)error;
            return false;
#endif
        }

        bool closeProcessesLockingPath(const std::filesystem::path& path)
        {
#ifdef _WIN32
            std::error_code absoluteError;
            const std::filesystem::path absolutePath = std::filesystem::absolute(path, absoluteError);
            const std::wstring resource = (absoluteError ? path : absolutePath).wstring();
            if (resource.empty())
            {
                return false;
            }

            DWORD sessionHandle = 0;
            WCHAR sessionKey[CCH_RM_SESSION_KEY + 1]{};
            if (RmStartSession(&sessionHandle, 0, sessionKey) != ERROR_SUCCESS)
            {
                return false;
            }

            struct SessionGuard
            {
                DWORD handle{0};
                bool active{false};

                ~SessionGuard()
                {
                    if (active)
                    {
                        RmEndSession(handle);
                    }
                }
            } guard{sessionHandle, true};

            LPCWSTR resources[] = {resource.c_str()};
            if (RmRegisterResources(sessionHandle, 1, resources, 0, nullptr, 0, nullptr) != ERROR_SUCCESS)
            {
                return false;
            }

            UINT processInfoNeeded = 0;
            UINT processInfoCount = 0;
            DWORD rebootReasons = RmRebootReasonNone;
            DWORD result = RmGetList(
                sessionHandle,
                &processInfoNeeded,
                &processInfoCount,
                nullptr,
                &rebootReasons);
            if (result == ERROR_SUCCESS || processInfoNeeded == 0)
            {
                return false;
            }

            if (result != ERROR_MORE_DATA)
            {
                return false;
            }

            std::vector<RM_PROCESS_INFO> processes(processInfoNeeded);
            processInfoCount = processInfoNeeded;
            result = RmGetList(
                sessionHandle,
                &processInfoNeeded,
                &processInfoCount,
                processes.data(),
                &rebootReasons);
            if (result != ERROR_SUCCESS || processInfoCount == 0)
            {
                return false;
            }

            const DWORD currentProcessId = GetCurrentProcessId();
            const bool hasExternalLocker = std::any_of(
                processes.begin(),
                processes.begin() + static_cast<std::ptrdiff_t>(processInfoCount),
                [currentProcessId](const RM_PROCESS_INFO& process)
                {
                    return process.Process.dwProcessId != currentProcessId;
                });
            if (!hasExternalLocker)
            {
                return false;
            }

            return RmShutdown(sessionHandle, 0, nullptr) == ERROR_SUCCESS;
#else
            (void)path;
            return false;
#endif
        }

        // Windows Defender, the Search indexer and Explorer routinely hold short-lived
        // handles on files that were just created or copied. Those handles surface as
        // ERROR_SHARING_VIOLATION ("the file is being used by another process"). Retrying
        // with a short backoff lets the transfer ride over them instead of aborting.
        template <typename Operation>
        std::error_code retryTransientFilesystemOperation(Operation&& operation)
        {
            constexpr int maxAttempts = 6;
            std::error_code error;
            for (int attempt = 1; attempt <= maxAttempts; ++attempt)
            {
                error.clear();
                operation(error);
                if (!error || attempt == maxAttempts || !isTransientFilesystemError(error))
                {
                    return error;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
            }

            return error;
        }

        template <typename Operation>
        std::error_code retryTransientFilesystemOperationWithUnlock(
            const std::filesystem::path& lockedPath,
            Operation&& operation)
        {
            std::error_code error = retryTransientFilesystemOperation(std::forward<Operation>(operation));
            if (error && isTransientFilesystemError(error) && closeProcessesLockingPath(lockedPath))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                error = retryTransientFilesystemOperation(std::forward<Operation>(operation));
            }

            return error;
        }

        void removePathDuringImport(const std::filesystem::path& path)
        {
            const std::error_code removeError = retryTransientFilesystemOperationWithUnlock(
                path,
                [&path](std::error_code& code)
                {
                    std::filesystem::remove(path, code);
                });
            if (removeError)
            {
                throw std::runtime_error(
                    std::string("Failed to clear the target build folder because a file is locked by another process.") +
                        " path=\"" + pathForLog(path) + "\", error={" + filesystemErrorForLog(removeError) + "}");
            }
        }

        void renamePathDuringImport(
            const std::filesystem::path& source,
            const std::filesystem::path& destination,
            const char* failureMessage)
        {
            const std::error_code renameError = retryTransientFilesystemOperationWithUnlock(
                source,
                [&source, &destination](std::error_code& code)
                {
                    std::filesystem::rename(source, destination, code);
                });
            if (!renameError)
            {
                return;
            }

            if (isTransientFilesystemError(renameError))
            {
                throw std::runtime_error(
                    std::string(failureMessage) +
                    " because it is locked by another process. source=\"" + pathForLog(source) +
                    "\", destination=\"" + pathForLog(destination) +
                    "\", error={" + filesystemErrorForLog(renameError) + "}");
            }

            throw std::runtime_error(
                std::string(failureMessage) +
                " source=\"" + pathForLog(source) +
                "\", destination=\"" + pathForLog(destination) +
                "\", error={" + filesystemErrorForLog(renameError) + "}");
        }

        void clearDirectoryContents(const std::filesystem::path& directory)
        {
            std::error_code existsError;
            if (!std::filesystem::exists(directory, existsError))
            {
                return;
            }

            std::error_code error;
            std::vector<std::filesystem::path> entries;
            std::filesystem::recursive_directory_iterator iterator(
                directory,
                std::filesystem::directory_options::skip_permission_denied,
                error);
            const std::filesystem::recursive_directory_iterator end;
            while (iterator != end)
            {
                if (error)
                {
                    throw std::runtime_error("Failed to enumerate target project directory.");
                }

                entries.push_back(iterator->path());
                iterator.increment(error);
            }

            if (error)
            {
                throw std::runtime_error("Failed to enumerate target project directory.");
            }

            for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
            {
                removePathDuringImport(*entry);
            }
        }

        void removeDirectoryTreeDuringImport(const std::filesystem::path& path)
        {
            std::error_code existsError;
            if (!std::filesystem::exists(path, existsError))
            {
                return;
            }

            std::error_code typeError;
            if (std::filesystem::is_directory(path, typeError))
            {
                clearDirectoryContents(path);
            }

            removePathDuringImport(path);
        }

        void cleanupDirectoryTreeBestEffort(const std::filesystem::path& path) noexcept
        {
            try
            {
                removeDirectoryTreeDuringImport(path);
            }
            catch (...)
            {
            }
        }

        std::filesystem::path createImportStagingDirectory(
            const std::filesystem::path& targetProjectDirectory)
        {
            std::filesystem::path parent = targetProjectDirectory.parent_path();
            if (parent.empty())
            {
                parent = std::filesystem::current_path();
            }

            std::filesystem::create_directories(parent);

            std::wstring targetName = targetProjectDirectory.filename().wstring();
            if (targetName.empty())
            {
                targetName = L"import";
            }

            const std::filesystem::path stagingDirectory =
                uniquePath(parent, L"." + targetName + L".importing");
            std::filesystem::create_directories(stagingDirectory);
            return stagingDirectory;
        }

        std::filesystem::path activateStagedProjectDirectory(
            const std::filesystem::path& stagingDirectory,
            const std::filesystem::path& targetProjectDirectory,
            bool replaceExisting)
        {
            std::filesystem::path backupDirectory;

            std::error_code existsError;
            if (std::filesystem::exists(targetProjectDirectory, existsError))
            {
                if (!replaceExisting)
                {
                    throw std::invalid_argument("Target build folder already exists.");
                }

                std::wstring targetName = targetProjectDirectory.filename().wstring();
                if (targetName.empty())
                {
                    targetName = L"build";
                }

                backupDirectory = uniquePath(
                    targetProjectDirectory.parent_path(),
                    L"." + targetName + L".previous");
                renamePathDuringImport(
                    targetProjectDirectory,
                    backupDirectory,
                    "Failed to move the existing target build folder during import.");
            }

            try
            {
                renamePathDuringImport(
                    stagingDirectory,
                    targetProjectDirectory,
                    "Failed to activate the imported build folder.");
            }
            catch (...)
            {
                if (!backupDirectory.empty() && pathExists(backupDirectory) && !pathExists(targetProjectDirectory))
                {
                    try
                    {
                        renamePathDuringImport(
                            backupDirectory,
                            targetProjectDirectory,
                            "Failed to restore the previous target build folder after import failure.");
                    }
                    catch (...)
                    {
                    }
                }

                throw;
            }

            return backupDirectory;
        }

        ModSourceRecord sourceRecordFromMeta(
            const std::map<std::wstring, std::wstring>& meta,
            const BuildTemplate& resolvedTemplate)
        {
            const std::wstring modId = iniValue(meta, {L"modid", L"General.modid"});
            const std::wstring fileId = iniValue(meta, {L"fileid", L"General.fileid"});
            const std::wstring url = iniValue(meta, {L"url", L"General.url"});
            const std::wstring newestVersion = iniValue(meta, {L"newestVersion", L"General.newestVersion"});

            ModSourceRecord source;
            source.provider = (!modId.empty() && modId != L"0") || !fileId.empty() || !url.empty()
                ? L"nexus"
                : L"local";
            source.gameDomain = resolvedTemplate.nexusDomain;
            source.remoteModId = modId == L"0" ? std::wstring() : modId;
            source.remoteFileId = fileId;
            source.url = url;
            source.latestVersion = newestVersion;
            return source;
        }

        std::wstring versionFromMeta(const std::map<std::wstring, std::wstring>& meta)
        {
            return iniValue(meta, {L"version", L"General.version"});
        }

        std::wstring nameFromMeta(
            const std::map<std::wstring, std::wstring>& meta,
            std::wstring_view fallback)
        {
            const std::wstring name = iniValue(meta, {L"name", L"General.name", L"displayName", L"General.displayName"});
            return name.empty() ? std::wstring(fallback) : name;
        }

        ProjectDescriptor descriptorFromPlan(const ImportPlan& plan)
        {
            return ProjectDescriptor{
                plan.analysis.projectName,
                plan.resolvedTemplate.id,
                plan.resolvedTemplate.gameName,
                plan.analysis.gamePath,
                plan.analysis.destinationRootDirectory,
                plan.analysis.targetProjectDirectory,
                plan.analysis.targetConfigPath
            };
        }
    }

    ModOrganizerImportService::ModOrganizerImportService(
        Logger& logger,
        const TemplateService& templates,
        ProjectService& projects,
        BuildPathSettingsService& pathSettings) noexcept
        : logger_(logger),
          templates_(templates),
          projects_(projects),
          pathSettings_(pathSettings)
    {
    }

    void ModOrganizerImportService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        initialized_ = true;
        logger_.write(LogLevel::Info, "Mod Organizer import service initialized.");
    }

    void ModOrganizerImportService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        logger_.write(LogLevel::Info, "Mod Organizer import service shut down.");
        initialized_ = false;
    }

    ModOrganizerImportAnalysis ModOrganizerImportService::analyze(
        const std::filesystem::path& sourceDirectory,
        const std::filesystem::path& destinationRootDirectory,
        const std::filesystem::path& existingConfigPath) const
    {
        ImportPlan plan = createPlan(
            templates_,
            projects_,
            sourceDirectory,
            destinationRootDirectory,
            existingConfigPath);
        return plan.analysis;
    }

    ModOrganizerImportResult ModOrganizerImportService::importInstance(
        const ModOrganizerImportRequest& request) const
    {
        const bool replaceExisting = request.mode == ModOrganizerImportMode::ReplaceExisting;
        ImportPlan plan = createPlan(
            templates_,
            projects_,
            request.sourceDirectory,
            request.destinationRootDirectory,
            replaceExisting ? request.existingConfigPath : std::filesystem::path{});

        if (hasUnsafeSourceTargetOverlap(plan.analysis.sourceDirectory, plan.analysis.targetProjectDirectory))
        {
            throw std::invalid_argument("Source and destination directories must be separate.");
        }

        if (!plan.analysis.canImport)
        {
            throw std::invalid_argument("Mod Organizer 2 instance cannot be imported with the current settings.");
        }

        const ImportDiagnostics diagnostics(createImportLogPath());
        const std::filesystem::path finalProjectDirectory = plan.analysis.targetProjectDirectory;
        std::filesystem::path stagingProjectDirectory;
        std::filesystem::path replacedProjectBackup;
        bool projectDirectoryActivated = false;
        std::uintmax_t copiedBytes = 0;
        std::uintmax_t copyTotalBytes = plan.analysis.totalBytes;
        ProjectDescriptor finalDescriptor = descriptorFromPlan(plan);

        try
        {
            stagingProjectDirectory = createImportStagingDirectory(finalProjectDirectory);

            std::ostringstream startLog;
            startLog << "mod-organizer-import-start\n"
                     << "  sourceDirectory=" << pathForLog(plan.analysis.sourceDirectory) << "\n"
                     << "  destinationRootDirectory=" << pathForLog(plan.analysis.destinationRootDirectory) << "\n"
                     << "  finalProjectDirectory=" << pathForLog(finalProjectDirectory) << "\n"
                     << "  stagingProjectDirectory=" << pathForLog(stagingProjectDirectory) << "\n"
                     << "  targetConfigPath=" << pathForLog(plan.analysis.targetConfigPath) << "\n"
                     << "  replaceExisting=" << (replaceExisting ? "true" : "false") << "\n"
                     << "  modCount=" << plan.analysis.modCount << "\n"
                     << "  totalBytes=" << plan.analysis.totalBytes << "\n"
                     << "  availableBytes=" << plan.analysis.availableBytes;
            diagnostics.write(startLog.str());

            logger_.write(
                LogLevel::Info,
                "MO2Import",
                std::string("Mod Organizer import started. source=\"") +
                    pathForLog(plan.analysis.sourceDirectory) + "\", destination=\"" +
                    pathForLog(finalProjectDirectory) + "\", staging=\"" +
                    pathForLog(stagingProjectDirectory) + "\"" +
                    importDiagnosticsSuffix(diagnostics));

            if (request.progress)
            {
                request.progress(ModOrganizerImportProgress{
                    L"prepare",
                    L"Готовлю временную копию",
                    stagingProjectDirectory.wstring(),
                    0,
                    0,
                    0,
                    0,
                    plan.analysis.totalBytes
                });
            }

            const auto sourceModsDirectory = plan.sourceLayout.modsDirectory;
            const auto targetModsDirectory = stagingProjectDirectory / L"mods";
            std::vector<BulkFileCopyRoot> copyRoots;
            copyRoots.reserve(plan.mods.size() + 3 + plan.executableImport.copyRoots.size());

            for (const ImportedModPlan& mod : plan.mods)
            {
                copyRoots.push_back(BulkFileCopyRoot{
                    sourceModsDirectory / std::filesystem::path(mod.folderName),
                    targetModsDirectory / std::filesystem::path(mod.folderName),
                    L"Копирую моды",
                    [](const std::filesystem::path& path)
                    {
                        return equalsIgnoreCase(path.filename().wstring(), L"meta.ini");
                    }
                });
            }

            const std::array<std::pair<std::filesystem::path, std::wstring_view>, 3> extraFolders{
                std::pair{plan.sourceLayout.profilesDirectory, std::wstring_view(L"profiles")},
                std::pair{plan.sourceLayout.downloadsDirectory, std::wstring_view(L"downloads")},
                std::pair{plan.sourceLayout.overwriteDirectory, std::wstring_view(L"overwrite")}
            };
            for (const auto& [sourceFolder, targetFolderName] : extraFolders)
            {
                copyRoots.push_back(BulkFileCopyRoot{
                    sourceFolder,
                    stagingProjectDirectory / std::filesystem::path(targetFolderName),
                    L"Копирую файлы профиля",
                    [](const std::filesystem::path&) { return false; }
                });
            }

            for (const ModOrganizerExecutableCopyRoot& executableRoot : plan.executableImport.copyRoots)
            {
                const std::optional<std::filesystem::path> onlyFile = executableRoot.onlyFile;
                const std::filesystem::path destinationDirectory = remapProjectDestinationToStaging(
                    executableRoot.destinationDirectory,
                    finalProjectDirectory,
                    stagingProjectDirectory);
                copyRoots.push_back(BulkFileCopyRoot{
                    executableRoot.sourceDirectory,
                    destinationDirectory,
                    L"Копирую исполняемые файлы",
                    [onlyFile](const std::filesystem::path& path)
                    {
                        return onlyFile.has_value() && !areSamePath(path, onlyFile.value());
                    }
                });
            }

            copyTotalBytes = plan.analysis.totalBytes;
            {
                std::ostringstream batchLog;
                batchLog << "bulk-copy-start\n"
                         << "  roots=" << copyRoots.size() << "\n"
                         << "  totalBytes=" << copyTotalBytes;
                diagnostics.write(batchLog.str());
            }
            BulkFileCopyService copyService(logger_);
            copiedBytes = copyService.copy(
                copyRoots,
                BulkFileCopyOptions{
                    copyTotalBytes,
                    0,
                    [&request](const BulkFileCopyProgress& progress)
                    {
                        if (!request.progress)
                        {
                            return;
                        }

                        const int copyPercent = progress.totalBytes == 0
                            ? 100
                            : static_cast<int>(
                                ((std::min)(progress.copiedBytes, progress.totalBytes) * 100) /
                                progress.totalBytes);
                        request.progress(ModOrganizerImportProgress{
                            L"copy",
                            progress.currentStep,
                            progress.currentItem.filename().wstring(),
                            static_cast<int>(((std::min)(100, copyPercent) * 80) / 100),
                            (std::min)(100, copyPercent),
                            0,
                            progress.copiedBytes,
                            progress.totalBytes
                        });
                    },
                    [&diagnostics](LogLevel level, std::string_view message)
                    {
                        diagnostics.write(level, message);
                    }
                });

            if (request.progress)
            {
                request.progress(ModOrganizerImportProgress{
                    L"configure",
                    L"Настраиваю структуру Fluxora",
                    stagingProjectDirectory.filename().wstring(),
                    81,
                    100,
                    0,
                    copiedBytes,
                    copyTotalBytes
                });
            }

            materializeTemplate(stagingProjectDirectory, plan.resolvedTemplate);

            if (request.progress)
            {
                request.progress(ModOrganizerImportProgress{
                    L"database",
                    L"Создаю базу Fluxora",
                    L"instance.db",
                    82,
                    100,
                    10,
                    copiedBytes,
                    copyTotalBytes
                });
            }

            ProjectDescriptor stagingDescriptor = finalDescriptor;
            stagingDescriptor.projectDirectory = stagingProjectDirectory;
            InstanceMetadataStore::ensureInstance(stagingDescriptor.projectDirectory, plan.resolvedTemplate.id);

            std::vector<InstalledModImportRecord> modsToRegister;
            modsToRegister.reserve(plan.mods.size());
            for (const ImportedModPlan& mod : plan.mods)
            {
                const std::filesystem::path sourceMod = sourceModsDirectory / std::filesystem::path(mod.folderName);
                const std::filesystem::path targetMod = targetModsDirectory / std::filesystem::path(mod.folderName);
                const auto meta = readIni(sourceMod / L"meta.ini");

                modsToRegister.push_back(InstalledModImportRecord{
                    targetMod,
                    nameFromMeta(meta, mod.folderName),
                    versionFromMeta(meta),
                    mod.isEnabled,
                    sourceRecordFromMeta(meta, plan.resolvedTemplate)
                });
            }

            InstanceMetadataStore::registerInstalledMods(
                stagingDescriptor.projectDirectory,
                modsToRegister,
                [&request, copiedBytes, copyTotalBytes](
                    std::size_t processedMods,
                    std::size_t totalMods,
                    std::wstring_view folderName)
                {
                    if (!request.progress)
                    {
                        return;
                    }

                    const int databasePercent = totalMods == 0
                        ? 100
                        : static_cast<int>((processedMods * 100) / totalMods);
                    request.progress(ModOrganizerImportProgress{
                        L"database",
                        L"Сканирую моды и записываю базу",
                        std::wstring(folderName),
                        82 + static_cast<int>(((std::min)(100, databasePercent) * 16) / 100),
                        100,
                        (std::min)(100, databasePercent),
                        copiedBytes,
                        copyTotalBytes
                    });
                });

            InstanceMetadataStore::replaceProfileOrderItems(
                stagingDescriptor.projectDirectory,
                plan.analysis.profileName,
                plan.orderItems);

            if (!plan.pluginOrderItems.empty())
            {
                InstanceMetadataStore::replaceProfilePluginOrderItems(
                    stagingDescriptor.projectDirectory,
                    plan.analysis.profileName,
                    plan.pluginOrderItems);
            }

            if (request.progress)
            {
                request.progress(ModOrganizerImportProgress{
                    L"activate",
                    replaceExisting ? L"Заменяю текущую сборку" : L"Добавляю сборку в Fluxora",
                    finalProjectDirectory.wstring(),
                    99,
                    100,
                    100,
                    copiedBytes,
                    copyTotalBytes
                });
            }

            replacedProjectBackup = activateStagedProjectDirectory(
                stagingProjectDirectory,
                finalProjectDirectory,
                replaceExisting);
            projectDirectoryActivated = true;
            writeBuildManifest(
                finalDescriptor,
                plan.resolvedTemplate,
                plan.executableImport.executables);
            const BuildPathSettings importedPathSettings = pathSettings_.saveForConfig(
                finalDescriptor.configPath,
                BuildPathSettings{
                    finalDescriptor.gamePath,
                    finalProjectDirectory / L"mods",
                    finalProjectDirectory / L"profiles",
                    finalProjectDirectory / L"downloads",
                    finalProjectDirectory / L"overwrite"
                });
            diagnostics.write(
                "mod-organizer-import-paths-saved: downloads="
                + pathForLog(importedPathSettings.downloadsDirectory));
            diagnostics.write("mod-organizer-import-activated");
        }
        catch (const std::exception& exception)
        {
            std::ostringstream failureLog;
            failureLog << "mod-organizer-import-failed: " << exception.what() << "\n"
                       << "  stagingProjectDirectory=" << pathForLog(stagingProjectDirectory) << "\n"
                       << "  finalProjectDirectory=" << pathForLog(finalProjectDirectory) << "\n"
                       << "  replacedProjectBackup=" << pathForLog(replacedProjectBackup) << "\n"
                       << "  projectDirectoryActivated=" << (projectDirectoryActivated ? "true" : "false") << "\n"
                       << "  logPath=" << diagnostics.logPathText();
            diagnostics.write(LogLevel::Error, failureLog.str());
            logger_.write(
                LogLevel::Error,
                "MO2Import",
                std::string("Mod Organizer import failed. error=\"") + exception.what() +
                    "\", staging=\"" + pathForLog(stagingProjectDirectory) +
                    "\", destination=\"" + pathForLog(finalProjectDirectory) + "\"" +
                    importDiagnosticsSuffix(diagnostics));

            if (!projectDirectoryActivated)
            {
                cleanupDirectoryTreeBestEffort(stagingProjectDirectory);
            }
            else if (replaceExisting && !replacedProjectBackup.empty() && pathExists(replacedProjectBackup))
            {
                cleanupDirectoryTreeBestEffort(finalProjectDirectory);
                try
                {
                    renamePathDuringImport(
                        replacedProjectBackup,
                        finalProjectDirectory,
                        "Failed to restore the previous target build folder after import failure.");
                }
                catch (...)
                {
                }
            }
            else if (!replaceExisting)
            {
                cleanupDirectoryTreeBestEffort(finalProjectDirectory);
            }

            throw;
        }
        catch (...)
        {
            diagnostics.write(LogLevel::Error, "mod-organizer-import-failed: unknown exception");
            logger_.write(
                LogLevel::Error,
                "MO2Import",
                std::string("Mod Organizer import failed with an unknown exception. destination=\"") +
                    pathForLog(finalProjectDirectory) + "\"" +
                    importDiagnosticsSuffix(diagnostics));
            if (!projectDirectoryActivated)
            {
                cleanupDirectoryTreeBestEffort(stagingProjectDirectory);
            }
            else if (replaceExisting && !replacedProjectBackup.empty() && pathExists(replacedProjectBackup))
            {
                cleanupDirectoryTreeBestEffort(finalProjectDirectory);
                try
                {
                    renamePathDuringImport(
                        replacedProjectBackup,
                        finalProjectDirectory,
                        "Failed to restore the previous target build folder after import failure.");
                }
                catch (...)
                {
                }
            }
            else if (!replaceExisting)
            {
                cleanupDirectoryTreeBestEffort(finalProjectDirectory);
            }

            throw;
        }

        cleanupDirectoryTreeBestEffort(replacedProjectBackup);

        if (request.progress)
        {
            request.progress(ModOrganizerImportProgress{
                L"complete",
                L"Перенос завершен",
                finalDescriptor.name,
                100,
                100,
                100,
                copiedBytes,
                copyTotalBytes
            });
        }

        logger_.write(
            LogLevel::Info,
            "MO2Import",
            std::string("Mod Organizer 2 instance imported. destination=\"") +
                pathForLog(finalProjectDirectory) + "\"" +
                importDiagnosticsSuffix(diagnostics));
        return ModOrganizerImportResult{
            projects_.openProjectConfig(finalDescriptor.configPath),
            plan.analysis
        };
    }

    bool ModOrganizerImportService::isInitialized() const noexcept
    {
        return initialized_;
    }
}
