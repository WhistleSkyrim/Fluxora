#include "FluxoraCore/Services/ExecutableService.hpp"

#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/ExecutableIconService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"
#include "FluxoraCore/Support/JsonWriter.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fluxora
{
    namespace
    {
        constexpr std::wstring_view launchExecutablesField = L"launchExecutables";

        struct ProjectExecutableContext
        {
            JsonValue manifest;
            std::filesystem::path configPath;
            std::filesystem::path manifestDirectory;
            std::filesystem::path gamePath;
            std::filesystem::path projectDirectory;
            std::wstring templateId;
            std::wstring dataDirectory;
            std::wstring defaultProfile;
        };

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
            std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("Failed to update build config.");
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

        ProjectExecutableContext readProjectExecutableContext(const std::filesystem::path& configPath)
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

            JsonValue manifest = parseJsonConfig(readTextFile(absoluteConfigPath));
            if (!manifest.isObject())
            {
                throw std::invalid_argument("Build config root must be a JSON object.");
            }

            const std::filesystem::path manifestDirectory = absoluteConfigPath.parent_path();
            std::filesystem::path projectDirectory = resolveManifestPath(
                readStringOrDefault(manifest, L"projectDirectory", manifestDirectory.wstring()),
                manifestDirectory);
            if (projectDirectory.empty())
            {
                projectDirectory = manifestDirectory;
            }

            const std::filesystem::path gamePath =
                resolveManifestPath(readStringOrDefault(manifest, L"gamePath"), projectDirectory);
            std::wstring templateId = readStringOrDefault(manifest, L"templateId");
            std::wstring dataDirectory = readStringOrDefault(manifest, L"dataDirectory", L"Data");
            std::wstring defaultProfile = readStringOrDefault(manifest, L"defaultProfile", L"Default");

            return ProjectExecutableContext{
                std::move(manifest),
                absoluteConfigPath,
                manifestDirectory,
                gamePath,
                projectDirectory,
                std::move(templateId),
                std::move(dataDirectory),
                std::move(defaultProfile)
            };
        }

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

        std::wstring fileNameWithoutExtension(const std::wstring& pathText)
        {
            std::filesystem::path path(pathText);
            std::wstring stem = path.stem().wstring();
            return stem.empty() ? path.filename().wstring() : stem;
        }

        std::wstring toLower(std::wstring value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
            return value;
        }

        bool hasExecutableExtension(const std::wstring& pathText)
        {
            return toLower(std::filesystem::path(pathText).extension().wstring()) == L".exe";
        }

        std::wstring slugifyExecutableId(const GameExecutable& executable, int fallbackIndex)
        {
            std::wstring source = trim(executable.id);
            if (source.empty())
            {
                source = trim(executable.displayName);
            }
            if (source.empty())
            {
                source = fileNameWithoutExtension(executable.executablePath);
            }
            if (source.empty())
            {
                source = L"executable-" + std::to_wstring(fallbackIndex + 1);
            }

            std::wstring id;
            bool previousWasDash = false;
            for (wchar_t character : source)
            {
                const wchar_t lower = static_cast<wchar_t>(std::towlower(character));
                if ((lower >= L'a' && lower <= L'z') || (lower >= L'0' && lower <= L'9'))
                {
                    id.push_back(lower);
                    previousWasDash = false;
                    continue;
                }

                if (!previousWasDash)
                {
                    id.push_back(L'-');
                    previousWasDash = true;
                }
            }

            while (!id.empty() && id.front() == L'-')
            {
                id.erase(id.begin());
            }
            while (!id.empty() && id.back() == L'-')
            {
                id.pop_back();
            }

            return id.empty() ? L"executable-" + std::to_wstring(fallbackIndex + 1) : id;
        }

        std::vector<GameExecutable> normalizeExecutables(const std::vector<GameExecutable>& executables)
        {
            std::vector<GameExecutable> normalized;
            normalized.reserve(executables.size());

            std::map<std::wstring, int> idCounts;
            for (int index = 0; index < static_cast<int>(executables.size()); ++index)
            {
                GameExecutable executable = executables[static_cast<std::size_t>(index)];
                executable.id = trim(executable.id);
                executable.displayName = trim(executable.displayName);
                executable.executablePath = trim(executable.executablePath);
                executable.arguments = trim(executable.arguments);
                executable.workingDirectory = trim(executable.workingDirectory);

                if (executable.executablePath.empty())
                {
                    throw std::invalid_argument("Executable path is required.");
                }
                if (!hasExecutableExtension(executable.executablePath))
                {
                    throw std::invalid_argument("Executable path must point to an .exe file.");
                }
                if (executable.displayName.empty())
                {
                    executable.displayName = fileNameWithoutExtension(executable.executablePath);
                }

                std::wstring baseId = slugifyExecutableId(executable, index);
                int& count = idCounts[baseId];
                ++count;
                executable.id = count == 1 ? baseId : baseId + L"-" + std::to_wstring(count);

                normalized.push_back(std::move(executable));
            }

            return normalized;
        }

        GameExecutable readExecutableObject(const JsonValue& value, int index)
        {
            if (!value.isObject())
            {
                throw std::invalid_argument("Executable entry must be an object.");
            }

            GameExecutable executable{
                readStringOrDefault(value, L"id"),
                readStringOrDefault(value, L"displayName"),
                readStringOrDefault(value, L"executablePath"),
                readStringOrDefault(value, L"arguments"),
                readStringOrDefault(value, L"workingDirectory")
            };

            if (executable.executablePath.empty())
            {
                executable.executablePath = readStringOrDefault(value, L"path");
            }
            if (executable.displayName.empty())
            {
                executable.displayName = readStringOrDefault(value, L"name");
            }
            if (executable.id.empty())
            {
                executable.id = L"executable-" + std::to_wstring(index + 1);
            }

            return executable;
        }

        std::vector<GameExecutable> readExecutablesFromManifest(const JsonValue& manifest)
        {
            if (const JsonValue* launchExecutables = manifest.find(launchExecutablesField);
                launchExecutables != nullptr && !launchExecutables->isNull())
            {
                if (!launchExecutables->isArray())
                {
                    throw std::invalid_argument("Build config launch executables must be an array.");
                }

                std::vector<GameExecutable> executables;
                int index = 0;
                for (const JsonValue& item : launchExecutables->asArray())
                {
                    executables.push_back(readExecutableObject(item, index));
                    ++index;
                }

                return normalizeExecutables(executables);
            }

            return {};
        }

        void writeExecutable(JsonWriter& writer, const GameExecutable& executable)
        {
            writer.beginObject();
            writer.field(L"id", executable.id);
            writer.field(L"displayName", executable.displayName);
            writer.field(L"executablePath", executable.executablePath);
            writer.field(L"arguments", executable.arguments);
            writer.field(L"workingDirectory", executable.workingDirectory);
            writer.endObject();
        }

        void writeExecutablesArray(JsonWriter& writer, const std::vector<GameExecutable>& executables)
        {
            writer.beginArray();
            for (const GameExecutable& executable : executables)
            {
                writeExecutable(writer, executable);
            }
            writer.endArray();
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

        void writeManifestWithExecutables(
            const ProjectExecutableContext& context,
            const std::vector<GameExecutable>& executables)
        {
            JsonWriter writer;
            writer.beginObject();
            for (const auto& [key, value] : context.manifest.asObject())
            {
                if (key == launchExecutablesField)
                {
                    continue;
                }

                writer.key(key);
                writeJsonValue(writer, value);
            }

            writer.key(launchExecutablesField);
            writeExecutablesArray(writer, executables);
            writer.endObject();

            writeTextFile(context.configPath, toUtf8(writer.str()));
        }

        std::optional<std::filesystem::path> tryResolveExistingFile(
            const ProjectExecutableContext& context,
            const std::wstring& pathText)
        {
            std::filesystem::path path(pathText);
            std::vector<std::filesystem::path> candidates;

            if (path.is_absolute())
            {
                candidates.push_back(path);
            }
            else
            {
                if (!context.gamePath.empty())
                {
                    candidates.push_back(context.gamePath / path);
                }
                if (!context.projectDirectory.empty())
                {
                    candidates.push_back(context.projectDirectory / path);
                }
                candidates.push_back(context.manifestDirectory / path);
                candidates.push_back(path);
            }

            for (const auto& candidate : candidates)
            {
                if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate))
                {
                    return std::filesystem::absolute(candidate);
                }
            }

            return std::nullopt;
        }

        std::filesystem::path resolveExistingFile(
            const ProjectExecutableContext& context,
            const std::wstring& pathText)
        {
            if (const std::optional<std::filesystem::path> resolved = tryResolveExistingFile(context, pathText))
            {
                return *resolved;
            }

            throw std::invalid_argument("Executable file does not exist.");
        }

        void resolveExecutableIconPaths(
            const ProjectExecutableContext& context,
            const ExecutableIconService& iconService,
            std::vector<GameExecutable>& executables)
        {
            for (GameExecutable& executable : executables)
            {
                executable.iconPath.clear();
                const std::optional<std::filesystem::path> resolvedPath =
                    tryResolveExistingFile(context, executable.executablePath);
                if (!resolvedPath.has_value())
                {
                    continue;
                }

                executable.iconPath = iconService.resolveIconPath(*resolvedPath).wstring();
            }
        }

        std::filesystem::path resolveWorkingDirectory(
            const ProjectExecutableContext& context,
            const GameExecutable& executable,
            const std::filesystem::path& resolvedExecutablePath)
        {
            if (executable.workingDirectory.empty())
            {
                return resolvedExecutablePath.parent_path();
            }

            std::filesystem::path path(executable.workingDirectory);
            std::vector<std::filesystem::path> candidates;
            if (path.is_absolute())
            {
                candidates.push_back(path);
            }
            else
            {
                if (!context.gamePath.empty())
                {
                    candidates.push_back(context.gamePath / path);
                }
                if (!context.projectDirectory.empty())
                {
                    candidates.push_back(context.projectDirectory / path);
                }
                candidates.push_back(context.manifestDirectory / path);
            }

            for (const auto& candidate : candidates)
            {
                if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate))
                {
                    return std::filesystem::absolute(candidate);
                }
            }

            throw std::invalid_argument("Executable working directory does not exist.");
        }

        std::wstring quoteCommandLineArgument(const std::wstring& value)
        {
            std::wstring quoted = L"\"";
            for (wchar_t character : value)
            {
                if (character == L'"')
                {
                    quoted.push_back(L'\\');
                }
                quoted.push_back(character);
            }
            quoted.push_back(L'"');
            return quoted;
        }
    }

    ExecutableService::ExecutableService(
        Logger& logger,
        ExecutableIconService& iconService,
        const BuildPathSettingsService& pathSettings) noexcept
        : logger_(logger),
          iconService_(iconService),
          pathSettings_(pathSettings)
    {
    }

    void ExecutableService::initialize()
    {
        if (initialized_)
        {
            return;
        }

        initialized_ = true;
        logger_.write(LogLevel::Info, "Executable service initialized.");
    }

    void ExecutableService::shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        initialized_ = false;
        logger_.write(LogLevel::Info, "Executable service shut down.");
    }

    std::vector<GameExecutable> ExecutableService::listProjectExecutables(
        const std::filesystem::path& configPath) const
    {
        ProjectExecutableContext context = readProjectExecutableContext(configPath);
        context.gamePath = pathSettings_.loadForConfig(configPath).gameDirectory;
        std::vector<GameExecutable> executables = readExecutablesFromManifest(context.manifest);
        resolveExecutableIconPaths(context, iconService_, executables);
        return executables;
    }

    std::vector<GameExecutable> ExecutableService::saveProjectExecutables(
        const std::filesystem::path& configPath,
        const std::vector<GameExecutable>& executables) const
    {
        ProjectExecutableContext context = readProjectExecutableContext(configPath);
        context.gamePath = pathSettings_.loadForConfig(configPath).gameDirectory;
        std::vector<GameExecutable> normalized = normalizeExecutables(executables);
        writeManifestWithExecutables(context, normalized);
        resolveExecutableIconPaths(context, iconService_, normalized);
        logger_.write(LogLevel::Info, "Project executable list updated.");
        return normalized;
    }

    ResolvedExecutableLaunch ExecutableService::resolveExecutable(
        const std::filesystem::path& configPath,
        std::wstring_view executableId) const
    {
        if (executableId.empty())
        {
            throw std::invalid_argument("Executable id is required.");
        }

        ProjectExecutableContext context = readProjectExecutableContext(configPath);
        context.gamePath = pathSettings_.loadForConfig(configPath).gameDirectory;
        std::vector<GameExecutable> executables = readExecutablesFromManifest(context.manifest);
        resolveExecutableIconPaths(context, iconService_, executables);
        const auto match = std::find_if(
            executables.begin(),
            executables.end(),
            [executableId](const GameExecutable& executable) { return executable.id == executableId; });
        if (match == executables.end())
        {
            throw std::invalid_argument("Executable was not found.");
        }

        const std::filesystem::path resolvedExecutablePath = resolveExistingFile(context, match->executablePath);
        if (!hasExecutableExtension(resolvedExecutablePath.wstring()))
        {
            throw std::invalid_argument("Executable path must point to an .exe file.");
        }

        const std::filesystem::path resolvedWorkingDirectory =
            resolveWorkingDirectory(context, *match, resolvedExecutablePath);

        std::wstring commandLine = quoteCommandLineArgument(resolvedExecutablePath.wstring());
        if (!match->arguments.empty())
        {
            commandLine.push_back(L' ');
            commandLine.append(match->arguments);
        }

        return ResolvedExecutableLaunch{
            *match,
            resolvedExecutablePath,
            resolvedWorkingDirectory,
            std::move(commandLine),
            context.gamePath,
            context.projectDirectory,
            context.templateId,
            context.dataDirectory,
            context.defaultProfile
        };
    }

    GameExecutableLaunchResult ExecutableService::launchProjectExecutable(
        const std::filesystem::path& configPath,
        std::wstring_view executableId) const
    {
        const ResolvedExecutableLaunch resolved = resolveExecutable(configPath, executableId);

#ifdef _WIN32
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInformation{};
        std::vector<wchar_t> commandLineBuffer(resolved.commandLine.begin(), resolved.commandLine.end());
        commandLineBuffer.push_back(L'\0');

        const BOOL started = CreateProcessW(
            resolved.resolvedExecutablePath.c_str(),
            commandLineBuffer.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            resolved.resolvedWorkingDirectory.c_str(),
            &startupInfo,
            &processInformation);
        if (!started)
        {
            throw std::runtime_error("Failed to launch executable.");
        }

        CloseHandle(processInformation.hThread);
        CloseHandle(processInformation.hProcess);
#else
        throw std::runtime_error("Executable launching is only implemented on Windows.");
#endif

        logger_.write(LogLevel::Info, "Project executable launched.");
        return GameExecutableLaunchResult{
            resolved.executable,
            resolved.resolvedExecutablePath,
            resolved.resolvedWorkingDirectory
        };
    }

    bool ExecutableService::isInitialized() const noexcept
    {
        return initialized_;
    }
}
