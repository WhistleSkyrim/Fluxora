#include "FluxoraCore/FluxoraCoreApi.hpp"

#include "FluxoraCore/Core.hpp"
#include "FluxoraCore/Services/AppSettingsService.hpp"
#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/DownloadService.hpp"
#include "FluxoraCore/Services/ExecutableIconService.hpp"
#include "FluxoraCore/Services/ExecutableService.hpp"
#include "FluxoraCore/Services/ModService.hpp"
#include "FluxoraCore/Services/ModOrganizerImportService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Services/NexusModsAuthService.hpp"
#include "FluxoraCore/Services/PluginService.hpp"
#include "FluxoraCore/Services/ProfileOrderService.hpp"
#include "FluxoraCore/Services/ProjectService.hpp"
#include "FluxoraCore/Services/TemplateService.hpp"
#include "FluxoraCore/Services/VirtualFileSystemService.hpp"
#include "FluxoraCore/Support/JsonReader.hpp"
#include "FluxoraCore/Support/JsonWriter.hpp"

#include <cstring>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
    thread_local std::wstring lastError;
    fluxora::Core* currentCore = nullptr;

    bool isBlank(const wchar_t* value);
    fluxora::Core& core();

    void writeCapabilities(fluxora::JsonWriter& writer, const fluxora::BuildTemplate& tpl)
    {
        writer.key(L"capabilities").beginArray();
        for (const auto& capability : tpl.capabilities)
        {
            writer.beginObject();
            writer.field(L"id", capability.id);
            writer.field(L"displayName", capability.displayName);
            writer.field(L"description", capability.description);
            writer.endObject();
        }
        writer.endArray();
    }

    void writeScriptExtender(fluxora::JsonWriter& writer, const fluxora::BuildTemplate& tpl)
    {
        if (tpl.scriptExtender.has_value())
        {
            const fluxora::ScriptExtender& extender = tpl.scriptExtender.value();
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
    }

    void writeGameExecutable(fluxora::JsonWriter& writer, const fluxora::GameExecutable& executable)
    {
        writer.beginObject();
        writer.field(L"id", executable.id);
        writer.field(L"displayName", executable.displayName);
        writer.field(L"executablePath", executable.executablePath);
        writer.field(L"arguments", executable.arguments);
        writer.field(L"workingDirectory", executable.workingDirectory);
        writer.field(L"iconPath", executable.iconPath);
        writer.endObject();
    }

    void writeGameExecutableList(
        fluxora::JsonWriter& writer,
        const std::vector<fluxora::GameExecutable>& executables)
    {
        writer.beginArray();
        for (const fluxora::GameExecutable& executable : executables)
        {
            writeGameExecutable(writer, executable);
        }
        writer.endArray();
    }

    std::wstring serializeGameExecutables(const std::vector<fluxora::GameExecutable>& executables)
    {
        fluxora::JsonWriter writer;
        writeGameExecutableList(writer, executables);
        return writer.str();
    }

    std::wstring serializeGameExecutableLaunch(const fluxora::GameExecutableLaunchResult& result)
    {
        fluxora::JsonWriter writer;
        writer.beginObject();
        writer.field(L"id", result.executable.id);
        writer.field(L"displayName", result.executable.displayName);
        writer.field(L"executablePath", result.executable.executablePath);
        writer.field(L"arguments", result.executable.arguments);
        writer.field(L"workingDirectory", result.executable.workingDirectory);
        writer.field(L"iconPath", result.executable.iconPath);
        writer.field(L"resolvedExecutablePath", result.resolvedExecutablePath.wstring());
        writer.field(L"resolvedWorkingDirectory", result.resolvedWorkingDirectory.wstring());
        writer.endObject();
        return writer.str();
    }

    void writeBuildPathSettings(
        fluxora::JsonWriter& writer,
        const fluxora::BuildPathSettings& settings)
    {
        writer.beginObject();
        writer.field(L"gameDirectory", settings.gameDirectory.wstring());
        writer.field(L"modsDirectory", settings.modsDirectory.wstring());
        writer.field(L"profilesDirectory", settings.profilesDirectory.wstring());
        writer.field(L"downloadsDirectory", settings.downloadsDirectory.wstring());
        writer.field(L"overwriteDirectory", settings.overwriteDirectory.wstring());
        writer.endObject();
    }

    std::wstring serializeBuildPathSettings(const fluxora::BuildPathSettings& settings)
    {
        fluxora::JsonWriter writer;
        writeBuildPathSettings(writer, settings);
        return writer.str();
    }

    void writeResolvedTemplate(fluxora::JsonWriter& writer, const fluxora::BuildTemplate& tpl)
    {
        writer.beginObject();
        writer.field(L"id", tpl.id);
        writer.field(L"displayName", tpl.displayName);
        writer.field(L"gameName", tpl.gameName);
        writer.field(L"summary", tpl.summary);
        writer.field(L"baseTemplateId", tpl.baseTemplateId);
        writer.field(L"defaultProfile", tpl.defaultProfileName);
        writer.field(L"dataDirectory", tpl.dataDirectory);
        writer.field(L"nexusDomain", tpl.nexusDomain);
        writer.stringArray(L"folders", tpl.folders);
        writer.stringArray(L"profileFiles", tpl.profileFiles);
        writer.stringArray(L"basePlugins", tpl.basePlugins);
        writer.stringArray(L"pluginExtensions", tpl.pluginExtensions);
        writer.stringArray(L"executables", tpl.executables);
        writeCapabilities(writer, tpl);
        writeScriptExtender(writer, tpl);
        writer.endObject();
    }

    std::wstring serializeResolvedTemplate(const fluxora::BuildTemplate& tpl)
    {
        fluxora::JsonWriter writer;
        writeResolvedTemplate(writer, tpl);
        return writer.str();
    }

    void writeOpenedProject(
        fluxora::JsonWriter& writer,
        const fluxora::ProjectOpenResult& result,
        bool tolerateExecutableErrors)
    {
        const fluxora::ProjectDescriptor& project = result.project;
        fluxora::BuildPathSettings pathSettings{
            project.gamePath,
            project.projectDirectory / L"mods",
            project.projectDirectory / L"profiles",
            project.projectDirectory / L"downloads",
            project.projectDirectory / L"overwrite"
        };
        try
        {
            pathSettings = core().buildPathSettings().loadForConfig(project.configPath);
        }
        catch (const std::exception&)
        {
            if (!tolerateExecutableErrors)
            {
                throw;
            }
        }

        writer.beginObject();
        writer.field(L"id", project.configPath.wstring());
        writer.field(L"name", project.name);
        writer.field(L"templateId", project.templateId);
        writer.field(L"gameName", project.gameName);
        writer.field(L"gamePath", pathSettings.gameDirectory.wstring());
        writer.field(L"installRootDirectory", project.installRootDirectory.wstring());
        writer.field(L"projectDirectory", project.projectDirectory.wstring());
        writer.field(L"configPath", project.configPath.wstring());
        writer.key(L"paths");
        writeBuildPathSettings(writer, pathSettings);
        writer.key(L"executables");
        if (tolerateExecutableErrors)
        {
            try
            {
                writeGameExecutableList(writer, core().executables().listProjectExecutables(project.configPath));
            }
            catch (const std::exception&)
            {
                writer.beginArray().endArray();
            }
        }
        else
        {
            writeGameExecutableList(writer, core().executables().listProjectExecutables(project.configPath));
        }
        writer.key(L"template");
        writeResolvedTemplate(writer, result.resolvedTemplate);
        writer.endObject();
    }

    std::wstring serializeOpenedProject(const fluxora::ProjectOpenResult& result)
    {
        fluxora::JsonWriter writer;
        writeOpenedProject(writer, result, false);
        return writer.str();
    }

    std::wstring serializeProjectConfigList(const std::vector<fluxora::ProjectOpenResult>& results)
    {
        fluxora::JsonWriter writer;
        writer.beginArray();
        for (const fluxora::ProjectOpenResult& result : results)
        {
            writeOpenedProject(writer, result, true);
        }
        writer.endArray();
        return writer.str();
    }

    std::wstring serializeGameTemplateList(const std::vector<fluxora::BuildTemplate>& templates)
    {
        fluxora::JsonWriter writer;
        writer.beginArray();
        for (const auto& tpl : templates)
        {
            writer.beginObject();
            writer.field(L"id", tpl.id);
            writer.field(L"displayName", tpl.displayName);
            writer.field(L"gameName", tpl.gameName);
            writer.field(L"summary", tpl.summary);
            writer.endObject();
        }
        writer.endArray();
        return writer.str();
    }

    std::wstring serializeNexusModsAuthStatus(const fluxora::NexusModsAuthStatus& status)
    {
        fluxora::JsonWriter writer;
        writer.beginObject();
        writer.field(L"isConfigured", status.isConfigured);
        writer.field(L"isLinked", status.isLinked);
        writer.field(L"displayName", status.displayName);
        writer.field(L"userId", status.userId);
        writer.field(L"message", status.message);
        writer.field(L"clientId", status.clientId);
        writer.field(L"redirectUri", status.redirectUri);
        writer.endObject();
        return writer.str();
    }

    void writeDownloadEntry(fluxora::JsonWriter& writer, const fluxora::DownloadEntry& download)
    {
        writer.beginObject();
        writer.field(L"id", download.id);
        writer.field(L"name", download.name);
        writer.field(L"fileName", download.fileName);
        writer.field(L"localPath", download.localPath.wstring());
        writer.field(L"source", download.source);
        writer.field(L"status", download.status);
        writer.field(L"sizeText", download.sizeText);
        writer.field(L"createdAtText", download.createdAtText);
        writer.field(L"progressPercent", download.progressPercent);
        writer.field(L"progressText", download.progressText);
        writer.field(L"etaText", download.etaText);
        writer.field(L"downloadSpeedText", download.downloadSpeedText);
        writer.field(L"isDownloading", download.isDownloading);
        writer.field(L"hasKnownProgress", download.hasKnownProgress);
        writer.field(L"canResume", download.canResume);
        writer.field(L"canInstall", download.canInstall);
        writer.field(L"canDelete", download.canDelete);
        writer.endObject();
    }

    std::wstring serializeDownloads(const std::vector<fluxora::DownloadEntry>& downloads)
    {
        fluxora::JsonWriter writer;
        writer.beginArray();
        for (const auto& download : downloads)
        {
            writeDownloadEntry(writer, download);
        }
        writer.endArray();
        return writer.str();
    }

    std::wstring serializeDownload(const fluxora::DownloadEntry& download)
    {
        fluxora::JsonWriter writer;
        writeDownloadEntry(writer, download);
        return writer.str();
    }

    std::wstring serializeInstalledMod(const fluxora::InstalledMod& mod)
    {
        fluxora::JsonWriter writer;
        writer.beginObject();
        writer.field(L"id", mod.id.wstring());
        writer.field(L"name", mod.name);
        writer.field(L"version", mod.version);
        writer.field(L"isEnabled", mod.isEnabled);
        writer.endObject();
        return writer.str();
    }

    void writeInstalledModEntry(fluxora::JsonWriter& writer, const fluxora::InstalledModEntry& mod)
    {
        writer.beginObject();
        writer.field(L"id", mod.id.wstring());
        writer.field(L"name", mod.name);
        writer.field(L"version", mod.version);
        writer.field(L"latestVersion", mod.latestVersion);
        writer.field(L"lastCheckedAt", mod.lastCheckedAt);
        writer.field(L"updateStatus", mod.updateStatus);
        writer.field(L"conflictStatus", mod.conflictStatus);
        writer.field(L"fileCount", mod.fileCount);
        writer.field(L"conflictingFileCount", mod.conflictingFileCount);
        writer.field(L"overwrittenFileCount", mod.overwrittenFileCount);
        writer.field(L"overwritingFileCount", mod.overwritingFileCount);
        writer.field(L"isEnabled", mod.isEnabled);
        writer.field(L"canCheckUpdates", mod.canCheckUpdates);
        writer.field(L"hasUpdate", mod.hasUpdate);
        writer.endObject();
    }

    std::wstring serializeInstalledModList(const std::vector<fluxora::InstalledModEntry>& mods)
    {
        fluxora::JsonWriter writer;
        writer.beginArray();
        for (const auto& mod : mods)
        {
            writeInstalledModEntry(writer, mod);
        }
        writer.endArray();
        return writer.str();
    }

    void writeProfileModOrderItem(fluxora::JsonWriter& writer, const fluxora::ProfileModOrderItem& item)
    {
        const bool isSeparator = item.kind == L"separator";

        writer.beginObject();
        writer.field(L"id", isSeparator ? item.orderId : item.id.wstring());
        writer.field(L"orderId", item.orderId);
        writer.field(L"kind", item.kind);
        writer.field(L"order", item.order);
        writer.field(L"isSeparator", isSeparator);
        writer.field(L"isMod", !isSeparator);
        writer.field(L"modUuid", item.modUuid);
        writer.field(L"separatorTitle", item.separatorTitle);
        writer.field(L"name", item.name);
        writer.field(L"version", item.version);
        writer.field(L"latestVersion", item.latestVersion);
        writer.field(L"lastCheckedAt", item.lastCheckedAt);
        writer.field(L"updateStatus", item.updateStatus);
        writer.field(L"conflictStatus", item.conflictStatus);
        writer.field(L"fileCount", item.fileCount);
        writer.field(L"conflictingFileCount", item.conflictingFileCount);
        writer.field(L"overwrittenFileCount", item.overwrittenFileCount);
        writer.field(L"overwritingFileCount", item.overwritingFileCount);
        writer.field(L"isEnabled", item.isEnabled);
        writer.field(L"canCheckUpdates", item.canCheckUpdates);
        writer.field(L"hasUpdate", item.hasUpdate);
        writer.endObject();
    }

    std::wstring serializeProfileModOrder(const std::vector<fluxora::ProfileModOrderItem>& items)
    {
        fluxora::JsonWriter writer;
        writer.beginArray();
        for (const auto& item : items)
        {
            writeProfileModOrderItem(writer, item);
        }
        writer.endArray();
        return writer.str();
    }

    void writeModFileTreeEntry(fluxora::JsonWriter& writer, const fluxora::ModFileTreeEntry& entry)
    {
        writer.beginObject();
        writer.field(L"name", entry.name);
        writer.field(L"relativePath", entry.relativePath);
        writer.field(L"isDirectory", entry.isDirectory);
        writer.field(L"hasChildren", entry.hasChildren);
        writer.field(L"size", entry.size);
        writer.field(L"conflictState", entry.conflictState);
        writer.stringArray(L"conflictOwners", entry.conflictOwners);
        writer.endObject();
    }

    std::wstring serializeModFileTree(const std::vector<fluxora::ModFileTreeEntry>& entries)
    {
        fluxora::JsonWriter writer;
        writer.beginArray();
        for (const auto& entry : entries)
        {
            writeModFileTreeEntry(writer, entry);
        }
        writer.endArray();
        return writer.str();
    }

    void writePluginEntry(fluxora::JsonWriter& writer, const fluxora::PluginEntry& plugin)
    {
        const bool isSeparator = plugin.kind == L"separator";

        writer.beginObject();
        writer.field(L"id", isSeparator ? plugin.orderId : plugin.name);
        writer.field(L"orderId", plugin.orderId);
        writer.field(L"kind", plugin.kind);
        writer.field(L"order", plugin.order);
        writer.field(L"isSeparator", isSeparator);
        writer.field(L"isPlugin", !isSeparator);
        writer.field(L"name", plugin.name);
        writer.field(L"separatorTitle", plugin.separatorTitle);
        writer.field(L"extension", plugin.extension);
        writer.field(L"sourceMod", plugin.sourceMod);
        writer.field(L"isEnabled", plugin.isEnabled);
        writer.field(L"isMaster", plugin.isMaster);
        writer.field(L"isLight", plugin.isLight);
        writer.field(L"isLocked", plugin.isLocked);
        writer.field(L"lockReason", plugin.lockReason);
        writer.endObject();
    }

    std::wstring serializePlugins(const std::vector<fluxora::PluginEntry>& plugins)
    {
        fluxora::JsonWriter writer;
        writer.beginArray();
        for (const auto& plugin : plugins)
        {
            writePluginEntry(writer, plugin);
        }
        writer.endArray();
        return writer.str();
    }

    std::wstring serializeNxmProtocolStatus(bool isRegistered)
    {
        fluxora::JsonWriter writer;
        writer.beginObject();
        writer.field(L"isRegistered", isRegistered);
        writer.endObject();
        return writer.str();
    }

    std::wstring serializeModOrganizerImportAnalysis(const fluxora::ModOrganizerImportAnalysis& analysis)
    {
        fluxora::JsonWriter writer;
        writer.beginObject();
        writer.field(L"sourceDirectory", analysis.sourceDirectory.wstring());
        writer.field(L"destinationRootDirectory", analysis.destinationRootDirectory.wstring());
        writer.field(L"targetProjectDirectory", analysis.targetProjectDirectory.wstring());
        writer.field(L"targetConfigPath", analysis.targetConfigPath.wstring());
        writer.field(L"projectName", analysis.projectName);
        writer.field(L"profileName", analysis.profileName);
        writer.field(L"templateId", analysis.templateId);
        writer.field(L"gameName", analysis.gameName);
        writer.field(L"gamePath", analysis.gamePath.wstring());
        writer.field(L"totalBytes", analysis.totalBytes);
        writer.field(L"availableBytes", analysis.availableBytes);
        writer.field(L"modCount", analysis.modCount);
        writer.field(L"separatorCount", analysis.separatorCount);
        writer.field(L"hasEnoughSpace", analysis.hasEnoughSpace);
        writer.field(L"willOverwrite", analysis.willOverwrite);
        writer.field(L"canImport", analysis.canImport);
        writer.field(L"statusMessage", analysis.statusMessage);
        writer.field(L"warningMessage", analysis.warningMessage);
        writer.endObject();
        return writer.str();
    }

    std::wstring serializeModOrganizerImportProgress(const fluxora::ModOrganizerImportProgress& progress)
    {
        fluxora::JsonWriter writer;
        writer.beginObject();
        writer.field(L"phase", progress.phase);
        writer.field(L"currentStep", progress.currentStep);
        writer.field(L"currentItem", progress.currentItem);
        writer.field(L"overallPercent", progress.overallPercent);
        writer.field(L"copyPercent", progress.copyPercent);
        writer.field(L"databasePercent", progress.databasePercent);
        writer.field(L"copiedBytes", progress.copiedBytes);
        writer.field(L"totalBytes", progress.totalBytes);
        writer.endObject();
        return writer.str();
    }

    std::wstring serializeProjectDeleteProgress(const fluxora::ProjectDeleteProgress& progress)
    {
        fluxora::JsonWriter writer;
        writer.beginObject();
        writer.field(L"phase", progress.phase);
        writer.field(L"currentStep", progress.currentStep);
        writer.field(L"currentItem", progress.currentItem);
        writer.field(L"overallPercent", progress.overallPercent);
        writer.field(L"deletedBytes", progress.deletedBytes);
        writer.field(L"totalBytes", progress.totalBytes);
        writer.field(L"deletedEntries", progress.deletedEntries);
        writer.field(L"totalEntries", progress.totalEntries);
        writer.endObject();
        return writer.str();
    }

    std::vector<std::wstring> parseStringArrayJson(const wchar_t* json)
    {
        if (isBlank(json))
        {
            return {};
        }

        const fluxora::JsonValue root = fluxora::JsonReader::parse(json);
        if (!root.isArray())
        {
            throw std::invalid_argument("Expected a JSON string array.");
        }

        std::vector<std::wstring> values;
        for (const fluxora::JsonValue& item : root.asArray())
        {
            if (!item.isString())
            {
                throw std::invalid_argument("Expected a JSON string array.");
            }

            values.push_back(item.asString());
        }

        return values;
    }

    std::vector<fluxora::GameExecutable> parseGameExecutablesJson(const wchar_t* json)
    {
        if (isBlank(json))
        {
            return {};
        }

        const fluxora::JsonValue root = fluxora::JsonReader::parse(json);
        if (!root.isArray())
        {
            throw std::invalid_argument("Expected a JSON executable array.");
        }

        std::vector<fluxora::GameExecutable> values;
        for (const fluxora::JsonValue& item : root.asArray())
        {
            if (!item.isObject())
            {
                throw std::invalid_argument("Executable entry must be an object.");
            }

            const auto readString = [&item](std::wstring_view field) -> std::wstring
            {
                const fluxora::JsonValue* value = item.find(field);
                if (value == nullptr || value->isNull())
                {
                    return {};
                }
                if (!value->isString())
                {
                    throw std::invalid_argument("Executable entry has a field with the wrong type.");
                }

                return value->asString();
            };

            fluxora::GameExecutable executable{
                readString(L"id"),
                readString(L"displayName"),
                readString(L"executablePath"),
                readString(L"arguments"),
                readString(L"workingDirectory"),
                readString(L"iconPath")
            };
            values.push_back(std::move(executable));
        }

        return values;
    }

    fluxora::BuildPathSettings parseBuildPathSettingsJson(const wchar_t* json)
    {
        if (isBlank(json))
        {
            throw std::invalid_argument("Build path settings JSON is required.");
        }

        const fluxora::JsonValue root = fluxora::JsonReader::parse(json);
        if (!root.isObject())
        {
            throw std::invalid_argument("Expected a JSON build path settings object.");
        }

        const auto readString = [&root](std::wstring_view field) -> std::wstring
        {
            const fluxora::JsonValue* value = root.find(field);
            if (value == nullptr || value->isNull())
            {
                return {};
            }
            if (!value->isString())
            {
                throw std::invalid_argument("Build path settings have a field with the wrong type.");
            }

            return value->asString();
        };

        return fluxora::BuildPathSettings{
            std::filesystem::path(readString(L"gameDirectory")),
            std::filesystem::path(readString(L"modsDirectory")),
            std::filesystem::path(readString(L"profilesDirectory")),
            std::filesystem::path(readString(L"downloadsDirectory")),
            std::filesystem::path(readString(L"overwriteDirectory"))
        };
    }

    fluxora::BuildTemplate resolveTemplateForPlugins(const wchar_t* templateId)
    {
        if (isBlank(templateId))
        {
            throw std::invalid_argument("Template id is required.");
        }

        return core().templates().resolve(templateId);
    }

    fluxora::Core& core()
    {
        static fluxora::Core instance;
        currentCore = &instance;
        if (!instance.isInitialized())
        {
            instance.initialize();
        }

        return instance;
    }

    bool isBlank(const wchar_t* value)
    {
        return value == nullptr || value[0] == L'\0';
    }

    std::wstring messageToWide(std::string_view message)
    {
        if (message.empty())
        {
            return {};
        }

#ifdef _WIN32
        const int requiredLength = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            message.data(),
            static_cast<int>(message.size()),
            nullptr,
            0);
        if (requiredLength > 0)
        {
            std::wstring value(static_cast<std::size_t>(requiredLength), L'\0');
            MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                message.data(),
                static_cast<int>(message.size()),
                value.data(),
                requiredLength);
            return value;
        }
#endif

        return std::wstring(message.begin(), message.end());
    }

    void logApiException(fluxora::LogLevel level, const std::exception& exception) noexcept
    {
        try
        {
            if (currentCore != nullptr && currentCore->logger().isInitialized())
            {
                currentCore->logger().write(
                    level,
                    "NativeApi",
                    std::string("Unhandled native API exception: ") + exception.what());
            }
        }
        catch (...)
        {
        }
    }

    int writeToBuffer(const std::wstring& value, wchar_t* buffer, int bufferLength)
    {
        if (buffer == nullptr || bufferLength <= 0)
        {
            lastError = L"Output buffer is required.";
            return FluxoraCoreResultInvalidArgument;
        }

        const auto requiredLength = static_cast<int>(value.size() + 1);
        if (requiredLength > bufferLength)
        {
            lastError =
                L"Output buffer is too small. Required length: " +
                std::to_wstring(requiredLength) +
                L", available: " +
                std::to_wstring(bufferLength) +
                L".";
            try
            {
                if (currentCore != nullptr && currentCore->logger().isInitialized())
                {
                    currentCore->logger().write(
                        fluxora::LogLevel::Warning,
                        "NativeApi",
                        "Native API output buffer is too small. required=" +
                            std::to_string(requiredLength) +
                            ", available=" +
                            std::to_string(bufferLength));
                }
            }
            catch (...)
            {
            }
            return FluxoraCoreResultBufferTooSmall;
        }

        std::wmemcpy(buffer, value.c_str(), value.size() + 1);
        lastError.clear();
        return FluxoraCoreResultOk;
    }

    int mapException(const std::exception& exception)
    {
        const char* message = exception.what();
        lastError = messageToWide(std::string_view(message, std::strlen(message)));
        const bool isInvalidArgument = dynamic_cast<const std::invalid_argument*>(&exception) != nullptr;
        logApiException(isInvalidArgument ? fluxora::LogLevel::Warning : fluxora::LogLevel::Error, exception);
        return isInvalidArgument
            ? FluxoraCoreResultInvalidArgument
            : FluxoraCoreResultCoreError;
    }
}

extern "C"
{
    int fluxora_core_is_available()
    {
        try
        {
            core();
            return 1;
        }
        catch (const std::exception& exception)
        {
            mapException(exception);
            return 0;
        }
    }

    int fluxora_preview_project_directory(
        const wchar_t* projectName,
        const wchar_t* installRootDirectory,
        wchar_t* projectDirectoryBuffer,
        int projectDirectoryBufferLength)
    {
        try
        {
            if (isBlank(projectName) || isBlank(installRootDirectory))
            {
                lastError = L"Project name and install root directory are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const auto projectDirectory = core().projects().buildProjectDirectory(
                std::filesystem::path(installRootDirectory),
                projectName);

            return writeToBuffer(projectDirectory.wstring(), projectDirectoryBuffer, projectDirectoryBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_get_game_templates(wchar_t* jsonBuffer, int jsonBufferLength)
    {
        try
        {
            const std::wstring json = serializeGameTemplateList(core().templates().gameTemplates());
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_resolve_template(const wchar_t* templateId, wchar_t* jsonBuffer, int jsonBufferLength)
    {
        try
        {
            if (isBlank(templateId))
            {
                lastError = L"Template id is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const fluxora::BuildTemplate resolved = core().templates().resolve(templateId);
            return writeToBuffer(serializeResolvedTemplate(resolved), jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_create_project(
        const wchar_t* projectName,
        const wchar_t* templateId,
        const wchar_t* gamePath,
        const wchar_t* installRootDirectory,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectName) || isBlank(templateId) || isBlank(gamePath) || isBlank(installRootDirectory))
            {
                lastError = L"Project name, template, game path, and install root directory are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const fluxora::ProjectCreateRequest request{
                projectName,
                templateId,
                std::filesystem::path(gamePath),
                std::filesystem::path(installRootDirectory)
            };

            const fluxora::ProjectDescriptor project = core().projects().createProject(request);
            const fluxora::ProjectOpenResult result{
                project,
                core().templates().resolve(project.templateId)
            };
            return writeToBuffer(serializeOpenedProject(result), jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_list_project_configs(
        const wchar_t* buildConfigsDirectory,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(buildConfigsDirectory))
            {
                lastError = L"Build configs directory is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::vector<fluxora::ProjectOpenResult> results =
                core().projects().listProjectConfigSummaries(std::filesystem::path(buildConfigsDirectory));
            return writeToBuffer(serializeProjectConfigList(results), jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_open_project_config(
        const wchar_t* configPath,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(configPath))
            {
                lastError = L"Build config path is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const fluxora::ProjectOpenResult result = core().projects().openProjectConfig(
                std::filesystem::path(configPath));
            return writeToBuffer(serializeOpenedProject(result), jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_rename_project(
        const wchar_t* configPath,
        const wchar_t* newName,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(configPath) || isBlank(newName))
            {
                lastError = L"Build config path and new project name are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const fluxora::ProjectOpenResult result = core().projects().renameProject(
                std::filesystem::path(configPath),
                newName);
            return writeToBuffer(serializeOpenedProject(result), jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_delete_project(const wchar_t* configPath)
    {
        try
        {
            if (isBlank(configPath))
            {
                lastError = L"Build config path is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            core().projects().deleteProject(std::filesystem::path(configPath));
            return FluxoraCoreResultOk;
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_delete_project_with_progress(
        const wchar_t* configPath,
        FluxoraCoreProgressCallback progressCallback,
        void* progressUserData)
    {
        try
        {
            if (isBlank(configPath))
            {
                lastError = L"Build config path is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            fluxora::ProjectDeleteRequest request;
            request.configPath = std::filesystem::path(configPath);
            if (progressCallback != nullptr)
            {
                request.progress = [progressCallback, progressUserData](const fluxora::ProjectDeleteProgress& progress)
                {
                    const std::wstring json = serializeProjectDeleteProgress(progress);
                    progressCallback(json.c_str(), progressUserData);
                };
            }

            core().projects().deleteProject(request);
            return FluxoraCoreResultOk;
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_get_build_path_settings(
        const wchar_t* configPath,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(configPath))
            {
                lastError = L"Build config path is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const fluxora::BuildPathSettings settings =
                core().buildPathSettings().loadForConfig(std::filesystem::path(configPath));
            return writeToBuffer(serializeBuildPathSettings(settings), jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_save_build_path_settings(
        const wchar_t* configPath,
        const wchar_t* settingsJson,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(configPath) || isBlank(settingsJson))
            {
                lastError = L"Build config path and build path settings are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const fluxora::BuildPathSettings saved =
                core().buildPathSettings().saveForConfig(
                    std::filesystem::path(configPath),
                    parseBuildPathSettingsJson(settingsJson));
            return writeToBuffer(serializeBuildPathSettings(saved), jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_analyze_mod_organizer_instance(
        const wchar_t* sourceDirectory,
        const wchar_t* destinationRootDirectory,
        const wchar_t* existingConfigPath,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(sourceDirectory) || isBlank(destinationRootDirectory))
            {
                lastError = L"Source directory and destination root directory are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const fluxora::ModOrganizerImportAnalysis analysis =
                core().modOrganizerImport().analyze(
                    std::filesystem::path(sourceDirectory),
                    std::filesystem::path(destinationRootDirectory),
                    isBlank(existingConfigPath) ? std::filesystem::path{} : std::filesystem::path(existingConfigPath));
            return writeToBuffer(
                serializeModOrganizerImportAnalysis(analysis),
                jsonBuffer,
                jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_import_mod_organizer_instance(
        const wchar_t* sourceDirectory,
        const wchar_t* destinationRootDirectory,
        const wchar_t* existingConfigPath,
        int replaceExisting,
        FluxoraCoreProgressCallback progressCallback,
        void* progressUserData,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(sourceDirectory) || isBlank(destinationRootDirectory))
            {
                lastError = L"Source directory and destination root directory are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            if (replaceExisting != 0 && isBlank(existingConfigPath))
            {
                lastError = L"Existing build config path is required for overwrite import.";
                return FluxoraCoreResultInvalidArgument;
            }

            fluxora::ModOrganizerImportRequest request;
            request.sourceDirectory = std::filesystem::path(sourceDirectory);
            request.destinationRootDirectory = std::filesystem::path(destinationRootDirectory);
            request.existingConfigPath = isBlank(existingConfigPath)
                ? std::filesystem::path{}
                : std::filesystem::path(existingConfigPath);
            request.mode = replaceExisting != 0
                ? fluxora::ModOrganizerImportMode::ReplaceExisting
                : fluxora::ModOrganizerImportMode::CreateNew;
            if (progressCallback != nullptr)
            {
                request.progress = [progressCallback, progressUserData](const fluxora::ModOrganizerImportProgress& progress)
                {
                    const std::wstring json = serializeModOrganizerImportProgress(progress);
                    progressCallback(json.c_str(), progressUserData);
                };
            }

            const fluxora::ModOrganizerImportResult result =
                core().modOrganizerImport().importInstance(request);
            return writeToBuffer(serializeOpenedProject(result.project), jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_get_game_executables(
        const wchar_t* configPath,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(configPath))
            {
                lastError = L"Build config path is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::wstring json = serializeGameExecutables(
                core().executables().listProjectExecutables(std::filesystem::path(configPath)));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_save_game_executables(
        const wchar_t* configPath,
        const wchar_t* executablesJson,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(configPath))
            {
                lastError = L"Build config path is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::vector<fluxora::GameExecutable> executables = parseGameExecutablesJson(executablesJson);
            const std::wstring json = serializeGameExecutables(
                core().executables().saveProjectExecutables(
                    std::filesystem::path(configPath),
                    executables));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_launch_game_executable(
        const wchar_t* configPath,
        const wchar_t* executableId,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(configPath) || isBlank(executableId))
            {
                lastError = L"Build config path and executable id are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            // Launching always goes through the virtual file system so the game
            // sees the merged mod data directory, exactly like Mod Organizer 2.
            // The service transparently falls back to a plain launch when there
            // is nothing to virtualize.
            const std::wstring json = serializeGameExecutableLaunch(
                core().virtualFileSystem().launchExecutable(
                    std::filesystem::path(configPath),
                    executableId));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_get_executable_icon(
        const wchar_t* executablePath,
        wchar_t* iconPathBuffer,
        int iconPathBufferLength)
    {
        try
        {
            if (isBlank(executablePath))
            {
                lastError = L"Executable path is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::wstring iconPath =
                core().executableIcons().resolveIconPath(std::filesystem::path(executablePath)).wstring();
            return writeToBuffer(iconPath, iconPathBuffer, iconPathBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_get_nexusmods_auth_status(wchar_t* jsonBuffer, int jsonBufferLength)
    {
        try
        {
            const std::wstring json = serializeNexusModsAuthStatus(core().nexusModsAuth().status());
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_connect_nexusmods(wchar_t* jsonBuffer, int jsonBufferLength)
    {
        try
        {
            const std::wstring json = serializeNexusModsAuthStatus(core().nexusModsAuth().connect());
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_disconnect_nexusmods(wchar_t* jsonBuffer, int jsonBufferLength)
    {
        try
        {
            const std::wstring json = serializeNexusModsAuthStatus(core().nexusModsAuth().disconnect());
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_get_app_language(wchar_t* languageBuffer, int languageBufferLength)
    {
        try
        {
            return writeToBuffer(core().settings().loadLanguageCode(), languageBuffer, languageBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_set_app_language(const wchar_t* languageCode)
    {
        try
        {
            if (isBlank(languageCode))
            {
                lastError = L"Language code is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            core().settings().saveLanguageCode(languageCode);
            return FluxoraCoreResultOk;
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_register_nxm_protocol(
        const wchar_t* executablePath,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(executablePath))
            {
                lastError = L"Executable path is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            core().downloads().registerNxmProtocol(std::filesystem::path(executablePath));
            const std::wstring json = serializeNxmProtocolStatus(
                core().downloads().isNxmProtocolRegistered(std::filesystem::path(executablePath)));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_get_installed_mods(
        const wchar_t* projectDirectory,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory))
            {
                lastError = L"Project directory is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::wstring json = serializeInstalledModList(
                core().mods().listInstalledMods(std::filesystem::path(projectDirectory)));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_get_mod_order(
        const wchar_t* projectDirectory,
        const wchar_t* profileName,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory))
            {
                lastError = L"Project directory is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::wstring json = serializeProfileModOrder(
                core().profileOrder().listModOrder(
                    std::filesystem::path(projectDirectory),
                    isBlank(profileName) ? L"" : std::wstring_view(profileName)));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_create_mod_separator(
        const wchar_t* projectDirectory,
        const wchar_t* profileName,
        const wchar_t* title,
        int targetIndex,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(title))
            {
                lastError = L"Project directory and separator title are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::wstring json = serializeProfileModOrder(
                core().profileOrder().createModSeparator(
                    std::filesystem::path(projectDirectory),
                    isBlank(profileName) ? L"" : std::wstring_view(profileName),
                    title,
                    targetIndex));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_delete_mod_separator(
        const wchar_t* projectDirectory,
        const wchar_t* profileName,
        const wchar_t* separatorId,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(separatorId))
            {
                lastError = L"Project directory and separator id are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::wstring json = serializeProfileModOrder(
                core().profileOrder().deleteModSeparator(
                    std::filesystem::path(projectDirectory),
                    isBlank(profileName) ? L"" : std::wstring_view(profileName),
                    separatorId));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_move_mod_order_item(
        const wchar_t* projectDirectory,
        const wchar_t* profileName,
        const wchar_t* orderItemId,
        int targetIndex,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(orderItemId))
            {
                lastError = L"Project directory and order item id are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::wstring json = serializeProfileModOrder(
                core().profileOrder().moveModOrderItem(
                    std::filesystem::path(projectDirectory),
                    isBlank(profileName) ? L"" : std::wstring_view(profileName),
                    orderItemId,
                    targetIndex));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_delete_installed_mod(
        const wchar_t* projectDirectory,
        const wchar_t* modPath)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(modPath))
            {
                lastError = L"Project directory and mod path are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            core().mods().deleteInstalledMod(
                std::filesystem::path(projectDirectory),
                std::filesystem::path(modPath));
            return FluxoraCoreResultOk;
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_set_installed_mod_enabled(
        const wchar_t* projectDirectory,
        const wchar_t* modPath,
        int isEnabled)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(modPath))
            {
                lastError = L"Project directory and mod path are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            core().mods().setInstalledModEnabled(
                std::filesystem::path(projectDirectory),
                std::filesystem::path(modPath),
                isEnabled != 0);
            return FluxoraCoreResultOk;
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_set_all_installed_mods_enabled(
        const wchar_t* projectDirectory,
        int isEnabled)
    {
        try
        {
            if (isBlank(projectDirectory))
            {
                lastError = L"Project directory is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            core().mods().setAllInstalledModsEnabled(
                std::filesystem::path(projectDirectory),
                isEnabled != 0);
            return FluxoraCoreResultOk;
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_check_mod_updates(
        const wchar_t* projectDirectory,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory))
            {
                lastError = L"Project directory is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::wstring json = serializeInstalledModList(
                core().mods().checkInstalledModUpdates(std::filesystem::path(projectDirectory)));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_get_mod_file_tree(
        const wchar_t* projectDirectory,
        const wchar_t* modPath,
        const wchar_t* relativeDirectory,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(modPath))
            {
                lastError = L"Project directory and mod path are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::wstring json = serializeModFileTree(
                core().mods().listModFileTree(
                    std::filesystem::path(projectDirectory),
                    std::filesystem::path(modPath),
                    isBlank(relativeDirectory) ? L"" : std::wstring_view(relativeDirectory)));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_get_plugins(
        const wchar_t* projectDirectory,
        const wchar_t* templateId,
        const wchar_t* profileName,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory))
            {
                lastError = L"Project directory is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const fluxora::BuildTemplate resolved = resolveTemplateForPlugins(templateId);
            const std::wstring json = serializePlugins(core().plugins().listPlugins(
                std::filesystem::path(projectDirectory),
                resolved,
                isBlank(profileName) ? L"" : profileName));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_move_plugin(
        const wchar_t* projectDirectory,
        const wchar_t* templateId,
        const wchar_t* profileName,
        const wchar_t* orderItemId,
        int targetIndex,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(orderItemId))
            {
                lastError = L"Project directory and plugin order item id are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const fluxora::BuildTemplate resolved = resolveTemplateForPlugins(templateId);
            const std::wstring json = serializePlugins(core().plugins().movePlugin(
                std::filesystem::path(projectDirectory),
                resolved,
                isBlank(profileName) ? L"" : profileName,
                orderItemId,
                targetIndex));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_create_plugin_separator(
        const wchar_t* projectDirectory,
        const wchar_t* templateId,
        const wchar_t* profileName,
        const wchar_t* title,
        int targetIndex,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(title))
            {
                lastError = L"Project directory and separator title are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const fluxora::BuildTemplate resolved = resolveTemplateForPlugins(templateId);
            const std::wstring json = serializePlugins(core().plugins().createPluginSeparator(
                std::filesystem::path(projectDirectory),
                resolved,
                isBlank(profileName) ? L"" : profileName,
                title,
                targetIndex));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_delete_plugin_separator(
        const wchar_t* projectDirectory,
        const wchar_t* templateId,
        const wchar_t* profileName,
        const wchar_t* separatorId,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(separatorId))
            {
                lastError = L"Project directory and separator id are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const fluxora::BuildTemplate resolved = resolveTemplateForPlugins(templateId);
            const std::wstring json = serializePlugins(core().plugins().deletePluginSeparator(
                std::filesystem::path(projectDirectory),
                resolved,
                isBlank(profileName) ? L"" : profileName,
                separatorId));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_set_plugin_enabled(
        const wchar_t* projectDirectory,
        const wchar_t* templateId,
        const wchar_t* profileName,
        const wchar_t* pluginName,
        int isEnabled,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(pluginName))
            {
                lastError = L"Project directory and plugin name are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const fluxora::BuildTemplate resolved = resolveTemplateForPlugins(templateId);
            const std::wstring json = serializePlugins(core().plugins().setPluginEnabled(
                std::filesystem::path(projectDirectory),
                resolved,
                isBlank(profileName) ? L"" : profileName,
                pluginName,
                isEnabled != 0));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_get_downloads(
        const wchar_t* projectDirectory,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory))
            {
                lastError = L"Project directory is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::wstring json = serializeDownloads(
                core().downloads().listDownloads(std::filesystem::path(projectDirectory)));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_capture_nxm_links(
        const wchar_t* projectDirectory,
        const wchar_t* nxmLinksJson,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            const std::vector<std::wstring> links = parseStringArrayJson(nxmLinksJson);
            const std::vector<fluxora::DownloadEntry> downloads = isBlank(projectDirectory)
                ? core().downloads().queueInboundNxmLinks(links)
                : core().downloads().captureNxmLinks(std::filesystem::path(projectDirectory), links);
            return writeToBuffer(serializeDownloads(downloads), jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_delete_download(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(downloadPath))
            {
                lastError = L"Project directory and download path are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            core().downloads().deleteDownload(
                std::filesystem::path(projectDirectory),
                std::filesystem::path(downloadPath));
            return FluxoraCoreResultOk;
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_cancel_download(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(downloadPath))
            {
                lastError = L"Project directory and download path are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            core().downloads().cancelDownload(
                std::filesystem::path(projectDirectory),
                std::filesystem::path(downloadPath));
            return FluxoraCoreResultOk;
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_resume_download(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(downloadPath))
            {
                lastError = L"Project directory and download path are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const fluxora::DownloadEntry download = core().downloads().resumeDownload(
                std::filesystem::path(projectDirectory),
                std::filesystem::path(downloadPath));
            return writeToBuffer(serializeDownload(download), jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_import_inbound_downloads(
        const wchar_t* projectDirectory,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory))
            {
                lastError = L"Project directory is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::wstring json = serializeDownloads(
                core().downloads().importInboundNxmLinks(std::filesystem::path(projectDirectory)));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_import_download_file(
        const wchar_t* projectDirectory,
        const wchar_t* sourcePath,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(sourcePath))
            {
                lastError = L"Project directory and source path are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::wstring json = serializeDownload(
                core().downloads().importLocalFile(
                    std::filesystem::path(projectDirectory),
                    std::filesystem::path(sourcePath)));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_install_download(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath,
        const wchar_t* modName,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(projectDirectory) || isBlank(downloadPath) || isBlank(modName))
            {
                lastError = L"Project directory, download path, and mod name are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::wstring json = serializeInstalledMod(
                core().downloads().installDownload(
                    std::filesystem::path(projectDirectory),
                    std::filesystem::path(downloadPath),
                    modName));
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_get_last_error(wchar_t* messageBuffer, int messageBufferLength)
    {
        return writeToBuffer(lastError, messageBuffer, messageBufferLength);
    }
}
