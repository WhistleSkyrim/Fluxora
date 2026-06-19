#include "FluxoraCore/Services/BuildPathSettingsService.hpp"

#include "FluxoraCore/GameSupport/GameDetectionService.hpp"
#include "FluxoraCore/GameSupport/GameHealthCheckService.hpp"
#include "FluxoraCore/GameSupport/GameSupportRegistry.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Services/PathSafetyService.hpp"
#include "FluxoraCore/Storage/AtomicFileStore.hpp"
#include "FluxoraCore/Storage/ProjectStateTransaction.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"
#include "FluxoraCore/Support/JsonWriter.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fluxora
{
    namespace
    {
        constexpr std::wstring_view pathsField = L"paths";
        constexpr std::wstring_view localSettingsDirectoryName = L".fluxora";
        constexpr std::wstring_view localSettingsFileName = L"paths.json";

        std::string toUtf8(const std::wstring& value)
        {
#ifdef _WIN32
            if (value.empty())
            {
                return {};
            }

            const int size = WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            std::string out(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                out.data(),
                size,
                nullptr,
                nullptr);
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
                throw std::invalid_argument("Build path settings are not valid UTF-8.");
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
                throw std::invalid_argument("Build path settings file could not be opened.");
            }

            return std::string(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
        }

        std::string readTextFileOrEmpty(const std::filesystem::path& path)
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

        void recoverJsonStateFile(
            const std::filesystem::path& path,
            std::wstring stateName,
            Logger* logger)
        {
            static_cast<void>(AtomicFileStore().recoverFile(
                path,
                AtomicFileWriteOptions{
                    std::move(stateName),
                    ProjectStateValidation::JsonObject
                },
                logger));
        }

        void writeJsonStateFile(
            const std::filesystem::path& path,
            const std::string& content,
            std::wstring stateName)
        {
            AtomicFileStore().writeTextFile(
                path,
                content,
                AtomicFileWriteOptions{
                    std::move(stateName),
                    ProjectStateValidation::JsonObject
                });
        }

        JsonValue parseJsonConfig(const std::string& content)
        {
            try
            {
                return JsonReader::parse(fromUtf8(content));
            }
            catch (const std::exception& exception)
            {
                throw std::invalid_argument(std::string("Build path settings are invalid: ") + exception.what());
            }
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
                throw std::invalid_argument("Build path settings contain a field with the wrong type.");
            }

            return value->asString();
        }

        std::filesystem::path resolvePath(
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

            return std::filesystem::absolute(path).lexically_normal();
        }

        std::filesystem::path localPathSettingsFile(const std::filesystem::path& projectDirectory)
        {
            return projectDirectory /
                std::filesystem::path(std::wstring(localSettingsDirectoryName)) /
                std::filesystem::path(std::wstring(localSettingsFileName));
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

        bool isSameOrInsidePath(
            const std::filesystem::path& candidate,
            const std::filesystem::path& root)
        {
            if (candidate.empty() || root.empty())
            {
                return false;
            }

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

        bool pathExists(const std::filesystem::path& path)
        {
            std::error_code error;
            return std::filesystem::exists(path, error);
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

        bool hasHealthyDetectedGameDirectory(const std::filesystem::path& directory)
        {
            const GameSupportRegistry& registry = GameSupportRegistry::embedded();
            GameDetectionService detectionService(registry);
            GameDetectionRequest request;
            request.installPath = directory;
            const GameDetectionResult detection = detectionService.detect(request);
            if (!detection.detected)
            {
                return false;
            }

            const GameHealthCheckResult health = GameHealthCheckService().check(detection);
            return health.allowsAutomation();
        }

        bool isKnownGameDirectory(const std::filesystem::path& directory)
        {
            return isDirectory(directory) && hasHealthyDetectedGameDirectory(directory);
        }

        bool looksLikeModOrganizerRoot(const std::filesystem::path& directory)
        {
            return isDirectory(directory) &&
                (isRegularFile(directory / L"ModOrganizer.ini") ||
                 (isDirectory(directory / L"mods") && isDirectory(directory / L"profiles")));
        }

        std::optional<std::filesystem::path> localGameDirectoryFromProject(
            const std::filesystem::path& projectDirectory)
        {
            std::vector<std::wstring> preferredFolderNames{
                L"stock game",
                L"Stock Game",
                L"stock_game",
                L"game",
                L"Game"
            };

            const GameSupportRegistry& registry = GameSupportRegistry::embedded();
            for (const GameDefinition& definition : registry.definitions())
            {
                preferredFolderNames.insert(
                    preferredFolderNames.end(),
                    definition.installFolderAliases.begin(),
                    definition.installFolderAliases.end());
            }

            for (const std::wstring& folderName : preferredFolderNames)
            {
                const std::filesystem::path candidate =
                    std::filesystem::absolute(projectDirectory / std::filesystem::path(folderName)).lexically_normal();
                if (isKnownGameDirectory(candidate))
                {
                    return candidate;
                }
            }

            std::error_code error;
            for (const std::filesystem::directory_entry& entry :
                std::filesystem::directory_iterator(projectDirectory, error))
            {
                if (error)
                {
                    break;
                }

                if (!entry.is_directory(error))
                {
                    continue;
                }

                std::wstring name = entry.path().filename().wstring();
                std::transform(name.begin(), name.end(), name.begin(), [](wchar_t character)
                {
                    return static_cast<wchar_t>(std::towlower(character));
                });
                if (name == L"mods" ||
                    name == L"profiles" ||
                    name == L"downloads" ||
                    name == L"overwrite" ||
                    name == L"webcache" ||
                    name == L"logs")
                {
                    continue;
                }

                const std::filesystem::path candidate =
                    std::filesystem::absolute(entry.path()).lexically_normal();
                if (isKnownGameDirectory(candidate))
                {
                    return candidate;
                }
            }

            return std::nullopt;
        }

        BuildPathSettings repairTransferredGameDirectory(
            const BuildPathSettings& settings,
            const std::filesystem::path& projectDirectory)
        {
            if (projectDirectory.empty())
            {
                return settings;
            }

            const bool isOrganizerRoot = looksLikeModOrganizerRoot(settings.gameDirectory);
            if (!isOrganizerRoot && isKnownGameDirectory(settings.gameDirectory))
            {
                return settings;
            }

            const bool shouldRepair =
                settings.gameDirectory.empty() ||
                !pathExists(settings.gameDirectory) ||
                isOrganizerRoot ||
                isSameOrInsidePath(settings.gameDirectory, projectDirectory);
            if (!shouldRepair)
            {
                return settings;
            }

            const auto localGameDirectory = localGameDirectoryFromProject(projectDirectory);
            if (!localGameDirectory.has_value())
            {
                return settings;
            }

            BuildPathSettings repaired = settings;
            repaired.gameDirectory = localGameDirectory.value();
            return repaired;
        }

        std::wstring pathTextForStorage(
            const std::filesystem::path& path,
            const std::filesystem::path& projectDirectory)
        {
            if (path.empty())
            {
                return {};
            }

            const std::filesystem::path absolutePath = std::filesystem::absolute(path).lexically_normal();
            if (isSameOrInsidePath(absolutePath, projectDirectory))
            {
                std::error_code error;
                const std::filesystem::path relative =
                    std::filesystem::relative(absolutePath, projectDirectory, error);
                if (!error && !relative.empty())
                {
                    return relative.generic_wstring();
                }
            }

            return absolutePath.wstring();
        }

        BuildPathSettings defaultSettingsForProjectDirectory(const std::filesystem::path& projectDirectory)
        {
            const std::filesystem::path root = std::filesystem::absolute(projectDirectory).lexically_normal();
            return BuildPathSettings{
                {},
                root / L"mods",
                root / L"profiles",
                root / L"downloads",
                root / L"overwrite"
            };
        }

        BuildPathSettings defaultsFromManifest(
            const JsonValue& manifest,
            const std::filesystem::path& configPath)
        {
            if (!manifest.isObject())
            {
                throw std::invalid_argument("Build config root must be a JSON object.");
            }

            const std::filesystem::path manifestDirectory =
                std::filesystem::absolute(configPath).parent_path();
            std::filesystem::path projectDirectory = resolvePath(
                readStringOrDefault(manifest, L"projectDirectory", manifestDirectory.wstring()),
                manifestDirectory);
            if (projectDirectory.empty())
            {
                projectDirectory = manifestDirectory;
            }

            BuildPathSettings settings = defaultSettingsForProjectDirectory(projectDirectory);
            settings.gameDirectory = resolvePath(readStringOrDefault(manifest, L"gamePath"), projectDirectory);
            return settings;
        }

        void applyPathObject(
            const JsonValue& object,
            const std::filesystem::path& projectDirectory,
            BuildPathSettings& settings)
        {
            if (!object.isObject())
            {
                throw std::invalid_argument("Build paths must be a JSON object.");
            }

            const auto apply = [&object, &projectDirectory](std::wstring_view field, std::filesystem::path& target)
            {
                const std::wstring value = readStringOrDefault(object, field);
                if (!value.empty())
                {
                    target = resolvePath(value, projectDirectory);
                }
            };

            apply(L"gameDirectory", settings.gameDirectory);
            apply(L"gamePath", settings.gameDirectory);
            apply(L"modsDirectory", settings.modsDirectory);
            apply(L"modsPath", settings.modsDirectory);
            apply(L"profilesDirectory", settings.profilesDirectory);
            apply(L"profilesPath", settings.profilesDirectory);
            apply(L"downloadsDirectory", settings.downloadsDirectory);
            apply(L"downloadsPath", settings.downloadsDirectory);
            apply(L"overwriteDirectory", settings.overwriteDirectory);
            apply(L"overwritePath", settings.overwriteDirectory);
        }

        BuildPathSettings normalizeSettings(
            const BuildPathSettings& candidate,
            const BuildPathSettings& defaults)
        {
            BuildPathSettings settings = candidate;
            if (settings.gameDirectory.empty())
            {
                settings.gameDirectory = defaults.gameDirectory;
            }
            if (settings.modsDirectory.empty())
            {
                settings.modsDirectory = defaults.modsDirectory;
            }
            if (settings.profilesDirectory.empty())
            {
                settings.profilesDirectory = defaults.profilesDirectory;
            }
            if (settings.downloadsDirectory.empty())
            {
                settings.downloadsDirectory = defaults.downloadsDirectory;
            }
            if (settings.overwriteDirectory.empty())
            {
                settings.overwriteDirectory = defaults.overwriteDirectory;
            }

            if (!settings.gameDirectory.empty())
            {
                settings.gameDirectory = std::filesystem::absolute(settings.gameDirectory).lexically_normal();
            }
            settings.modsDirectory = std::filesystem::absolute(settings.modsDirectory).lexically_normal();
            settings.profilesDirectory = std::filesystem::absolute(settings.profilesDirectory).lexically_normal();
            settings.downloadsDirectory = std::filesystem::absolute(settings.downloadsDirectory).lexically_normal();
            settings.overwriteDirectory = std::filesystem::absolute(settings.overwriteDirectory).lexically_normal();
            return settings;
        }

        void validateSettingsForSave(const BuildPathSettings& settings)
        {
            if (settings.gameDirectory.empty() ||
                !std::filesystem::exists(settings.gameDirectory) ||
                !std::filesystem::is_directory(settings.gameDirectory))
            {
                throw std::invalid_argument("Game directory does not exist.");
            }

            const PathSafetyService safety;
            safety.validateDirectoryWriteRoot(settings.modsDirectory)
                .throwIfUnsafe("Mods directory is unsafe");
            safety.validateDirectoryWriteRoot(settings.profilesDirectory)
                .throwIfUnsafe("Profiles directory is unsafe");
            safety.validateDirectoryWriteRoot(settings.downloadsDirectory)
                .throwIfUnsafe("Downloads directory is unsafe");
            safety.validateDirectoryWriteRoot(settings.overwriteDirectory)
                .throwIfUnsafe("Overwrite directory is unsafe");

            std::filesystem::create_directories(settings.modsDirectory);
            std::filesystem::create_directories(settings.profilesDirectory);
            std::filesystem::create_directories(settings.downloadsDirectory);
            std::filesystem::create_directories(settings.overwriteDirectory);
        }

        JsonValue settingsJsonObject(
            const BuildPathSettings& settings,
            const std::filesystem::path& projectDirectory)
        {
            JsonValue::Object object;
            object.emplace(
                L"gameDirectory",
                JsonValue::string(pathTextForStorage(settings.gameDirectory, projectDirectory)));
            object.emplace(
                L"modsDirectory",
                JsonValue::string(pathTextForStorage(settings.modsDirectory, projectDirectory)));
            object.emplace(
                L"profilesDirectory",
                JsonValue::string(pathTextForStorage(settings.profilesDirectory, projectDirectory)));
            object.emplace(
                L"downloadsDirectory",
                JsonValue::string(pathTextForStorage(settings.downloadsDirectory, projectDirectory)));
            object.emplace(
                L"overwriteDirectory",
                JsonValue::string(pathTextForStorage(settings.overwriteDirectory, projectDirectory)));
            return JsonValue::object(std::move(object));
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
                for (const auto& [key, child] : value.asObject())
                {
                    writer.key(key);
                    writeJsonValue(writer, child);
                }
                writer.endObject();
                break;
            case JsonValue::Type::Array:
                writer.beginArray();
                for (const JsonValue& child : value.asArray())
                {
                    writeJsonValue(writer, child);
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

        std::string serializeLocalSettings(
            const BuildPathSettings& settings,
            const std::filesystem::path& projectDirectory)
        {
            return serializeJson(settingsJsonObject(settings, projectDirectory));
        }

        std::filesystem::path projectDirectoryFromDefaults(const BuildPathSettings& defaults)
        {
            if (!defaults.modsDirectory.empty())
            {
                return defaults.modsDirectory.parent_path();
            }

            if (!defaults.profilesDirectory.empty())
            {
                return defaults.profilesDirectory.parent_path();
            }

            return {};
        }

        BuildPathSettings loadLocalSettings(
            const std::filesystem::path& projectDirectory,
            BuildPathSettings settings,
            Logger* logger)
        {
            const std::filesystem::path localSettingsPath = localPathSettingsFile(projectDirectory);
            recoverJsonStateFile(localSettingsPath, L"profile path settings", logger);
            const std::string content = readTextFileOrEmpty(localSettingsPath);
            if (content.empty())
            {
                return settings;
            }

            const JsonValue root = parseJsonConfig(content);
            applyPathObject(root, projectDirectory, settings);
            return settings;
        }
    }

    BuildPathSettingsService::BuildPathSettingsService(Logger& logger) noexcept
        : logger_(logger)
    {
    }

    void BuildPathSettingsService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        initialized_ = true;
        logger_.write(LogLevel::Info, "Build path settings service initialized.");
    }

    void BuildPathSettingsService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        logger_.write(LogLevel::Info, "Build path settings service shut down.");
        initialized_ = false;
    }

    BuildPathSettings BuildPathSettingsService::loadForConfig(
        const std::filesystem::path& configPath) const
    {
        if (configPath.empty())
        {
            throw std::invalid_argument("Build config path is required.");
        }

        const std::filesystem::path absoluteConfigPath = std::filesystem::absolute(configPath);
        if (!std::filesystem::exists(absoluteConfigPath) || !std::filesystem::is_regular_file(absoluteConfigPath))
        {
            throw std::invalid_argument("Build config file does not exist.");
        }

        recoverJsonStateFile(absoluteConfigPath, L"project manifest", &logger_);
        const JsonValue manifest = parseJsonConfig(readTextFile(absoluteConfigPath));
        BuildPathSettings defaults = defaultsFromManifest(manifest, absoluteConfigPath);
        BuildPathSettings settings = defaults;
        const std::filesystem::path projectDirectory = projectDirectoryFromDefaults(defaults);

        if (const JsonValue* paths = manifest.find(pathsField); paths != nullptr && !paths->isNull())
        {
            applyPathObject(*paths, projectDirectory, settings);
        }

        if (!projectDirectory.empty())
        {
            settings = loadLocalSettings(projectDirectory, settings, &logger_);
        }

        settings = normalizeSettings(settings, defaults);
        return repairTransferredGameDirectory(settings, projectDirectory);
    }

    BuildPathSettings BuildPathSettingsService::saveForConfig(
        const std::filesystem::path& configPath,
        const BuildPathSettings& settings) const
    {
        if (configPath.empty())
        {
            throw std::invalid_argument("Build config path is required.");
        }

        const std::filesystem::path absoluteConfigPath = std::filesystem::absolute(configPath);
        if (!std::filesystem::exists(absoluteConfigPath) || !std::filesystem::is_regular_file(absoluteConfigPath))
        {
            throw std::invalid_argument("Build config file does not exist.");
        }

        recoverJsonStateFile(absoluteConfigPath, L"project manifest", &logger_);
        JsonValue manifest = parseJsonConfig(readTextFile(absoluteConfigPath));
        BuildPathSettings defaults = defaultsFromManifest(manifest, absoluteConfigPath);
        const std::filesystem::path projectDirectory = projectDirectoryFromDefaults(defaults);
        BuildPathSettings normalized = normalizeSettings(settings, defaults);

        validateSettingsForSave(normalized);

        AtomicFileStore fileStore;
        const std::filesystem::path markerDirectory = projectDirectory.empty()
            ? absoluteConfigPath.parent_path()
            : projectDirectory / L".fluxora";
        ProjectStateTransaction transaction(
            fileStore,
            markerDirectory,
            L"build path settings update",
            &logger_);

        const std::filesystem::path localSettingsPath = localPathSettingsFile(projectDirectory);
        transaction.trackFile(
            localSettingsPath,
            L"profile path settings",
            ProjectStateValidation::JsonObject);
        writeJsonStateFile(
            localSettingsPath,
            serializeLocalSettings(normalized, projectDirectory),
            L"profile path settings");

        JsonValue::Object object = manifest.asObject();
        object.insert_or_assign(
            L"gamePath",
            JsonValue::string(pathTextForStorage(normalized.gameDirectory, projectDirectory)));
        object.insert_or_assign(std::wstring(pathsField), settingsJsonObject(normalized, projectDirectory));
        transaction.trackFile(
            absoluteConfigPath,
            L"project manifest",
            ProjectStateValidation::JsonObject);
        writeJsonStateFile(
            absoluteConfigPath,
            serializeJson(JsonValue::object(std::move(object))),
            L"project manifest");
        transaction.commit();

        logger_.write(LogLevel::Info, "Build path settings updated.");
        return normalized;
    }

    BuildPathSettings BuildPathSettingsService::loadForProjectDirectory(
        const std::filesystem::path& projectDirectory) const
    {
        if (projectDirectory.empty())
        {
            throw std::invalid_argument("Project directory is required.");
        }

        BuildPathSettings defaults = defaultSettingsForProjectDirectory(projectDirectory);
        BuildPathSettings settings = loadLocalSettings(
            std::filesystem::absolute(projectDirectory).lexically_normal(),
            defaults,
            &logger_);
        settings = normalizeSettings(settings, defaults);
        return repairTransferredGameDirectory(
            settings,
            std::filesystem::absolute(projectDirectory).lexically_normal());
    }

    std::filesystem::path BuildPathSettingsService::modsDirectory(
        const std::filesystem::path& projectDirectory) const
    {
        return loadForProjectDirectory(projectDirectory).modsDirectory;
    }

    std::filesystem::path BuildPathSettingsService::profilesDirectory(
        const std::filesystem::path& projectDirectory) const
    {
        return loadForProjectDirectory(projectDirectory).profilesDirectory;
    }

    std::filesystem::path BuildPathSettingsService::downloadsDirectory(
        const std::filesystem::path& projectDirectory) const
    {
        return loadForProjectDirectory(projectDirectory).downloadsDirectory;
    }

    std::filesystem::path BuildPathSettingsService::overwriteDirectory(
        const std::filesystem::path& projectDirectory) const
    {
        return loadForProjectDirectory(projectDirectory).overwriteDirectory;
    }

    bool BuildPathSettingsService::isInitialized() const noexcept
    {
        return initialized_;
    }
}
