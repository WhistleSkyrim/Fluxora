#include "FluxoraCore/FluxoraCoreApi.hpp"

#include "FluxoraCore/Core.hpp"
#include "FluxoraCore/GameSupport/GameDetectionService.hpp"
#include "FluxoraCore/GameSupport/GameHealthCheckService.hpp"
#include "FluxoraCore/GameSupport/GameSupportRegistry.hpp"
#include "FluxoraCore/GameSupport/ProjectFingerprint.hpp"
#include "FluxoraCore/Services/AppSettingsService.hpp"
#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/DownloadService.hpp"
#include "FluxoraCore/Services/ExecutableIconService.hpp"
#include "FluxoraCore/Services/ExecutableService.hpp"
#include "FluxoraCore/Services/FluxPackService.hpp"
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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <optional>
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

    struct GameBridgeMetadata
    {
        bool supported{false};
        std::wstring gameId;
        std::wstring displayName;
        std::wstring uiTemplateId;
        fluxora::CapabilitySet capabilities;
        std::vector<std::wstring> archiveExtensions;
        std::vector<std::wstring> requiredFiles;
        fluxora::ContentLayoutSupportRules contentLayout;
        std::vector<fluxora::GameExecutableDefinition> executableDefinitions;
        std::vector<std::wstring> fallbackExecutables;
        std::optional<fluxora::GameScriptExtenderRules> scriptExtenderLaunchRules;
    };

    std::vector<std::wstring> extensionValues(const std::vector<fluxora::NormalizedExtension>& extensions)
    {
        std::vector<std::wstring> values;
        values.reserve(extensions.size());
        for (const fluxora::NormalizedExtension& extension : extensions)
        {
            values.push_back(extension.value());
        }

        return values;
    }

    std::vector<std::wstring> executableNameValues(const std::vector<fluxora::ExecutableName>& names)
    {
        std::vector<std::wstring> values;
        values.reserve(names.size());
        for (const fluxora::ExecutableName& name : names)
        {
            values.push_back(name.displayName());
        }

        return values;
    }

    std::vector<std::wstring> pathValues(const std::vector<std::filesystem::path>& paths)
    {
        std::vector<std::wstring> values;
        values.reserve(paths.size());
        for (const std::filesystem::path& path : paths)
        {
            values.push_back(path.wstring());
        }

        return values;
    }

    fluxora::CapabilitySet capabilitySetFromTemplate(const fluxora::BuildTemplate& tpl)
    {
        fluxora::CapabilitySet set;
        for (const fluxora::TemplateCapability& capability : tpl.capabilities)
        {
            if (capability.id == L"plugins")
            {
                set.enable(fluxora::GameCapability::Plugins);
            }
            else if (capability.id == L"load-order")
            {
                set.enable(fluxora::GameCapability::LoadOrder);
            }
            else if (capability.id == L"root-files")
            {
                set.enable(fluxora::GameCapability::RootFiles);
            }
            else if (capability.id == L"script-extender")
            {
                set.enable(fluxora::GameCapability::ScriptExtender);
            }
            else if (capability.id == L"ini-tweaks")
            {
                set.enable(fluxora::GameCapability::IniProfiles);
            }
            else if (capability.id == L"save-games")
            {
                set.enable(fluxora::GameCapability::SaveProfiles);
            }
            else if (capability.id == L"content-layout")
            {
                set.enable(fluxora::GameCapability::ContentLayoutRules);
            }
        }

        if (!tpl.pluginExtensions.empty())
        {
            set.enable(fluxora::GameCapability::Plugins);
        }

        return set;
    }

    GameBridgeMetadata resolveGameBridgeMetadata(const fluxora::BuildTemplate& tpl)
    {
        GameBridgeMetadata metadata;
        metadata.gameId = tpl.id;
        metadata.displayName = tpl.gameName.empty() ? tpl.displayName : tpl.gameName;
        metadata.uiTemplateId = tpl.id;
        metadata.capabilities = capabilitySetFromTemplate(tpl);
        metadata.contentLayout.dataFolder = tpl.dataDirectory;
        metadata.contentLayout.supportsRootFiles =
            metadata.capabilities.has(fluxora::GameCapability::RootFiles);
        metadata.fallbackExecutables = tpl.executables;

        const fluxora::GameSupportRegistry& registry = fluxora::GameSupportRegistry::embedded();
        const fluxora::GameSupportLookupResult lookup = registry.lookupById(tpl.id);
        if (!lookup.supported || lookup.definition == nullptr)
        {
            return metadata;
        }

        const fluxora::GameDefinition& definition = *lookup.definition;
        metadata.supported = true;
        metadata.gameId = definition.id.value();
        metadata.displayName = definition.displayName;
        metadata.uiTemplateId = definition.uiTemplateId.value();
        metadata.capabilities = definition.capabilities;
        metadata.archiveExtensions = extensionValues(definition.archiveExtensions);
        metadata.requiredFiles = definition.healthRules.requiredFiles.empty()
            ? definition.requiredFiles
            : definition.healthRules.requiredFiles;
        metadata.executableDefinitions = definition.executables;
        metadata.scriptExtenderLaunchRules = definition.launchRules.scriptExtender;

        if (lookup.support != nullptr &&
            lookup.support->components().contentLayoutRulesProvider != nullptr)
        {
            metadata.contentLayout =
                lookup.support->components().contentLayoutRulesProvider->contentLayoutRules();
        }
        else
        {
            metadata.contentLayout.dataFolder = definition.contentLayoutRules.dataFolder.empty()
                ? definition.dataFolder
                : definition.contentLayoutRules.dataFolder;
            metadata.contentLayout.supportsRootFiles = definition.contentLayoutRules.supportsRootFiles;
            metadata.contentLayout.rootFileWrapperDirectory =
                definition.contentLayoutRules.rootFileWrapperDirectory;
            metadata.contentLayout.pluginExtensions = definition.pluginExtensions;
            metadata.contentLayout.archiveExtensions = definition.archiveExtensions;
        }

        return metadata;
    }

    void writeGameCapabilities(fluxora::JsonWriter& writer, const fluxora::CapabilitySet& capabilities)
    {
        writer.beginObject();
        writer.field(L"bits", static_cast<int>(capabilities.bits()));
        writer.field(L"supportsPlugins", capabilities.has(fluxora::GameCapability::Plugins));
        writer.field(L"supportsLoadOrder", capabilities.has(fluxora::GameCapability::LoadOrder));
        writer.field(L"supportsRootFiles", capabilities.has(fluxora::GameCapability::RootFiles));
        writer.field(L"supportsArchives", capabilities.has(fluxora::GameCapability::Archives));
        writer.field(L"supportsScriptExtender", capabilities.has(fluxora::GameCapability::ScriptExtender));
        writer.field(L"supportsIniProfiles", capabilities.has(fluxora::GameCapability::IniProfiles));
        writer.field(L"supportsSaveProfiles", capabilities.has(fluxora::GameCapability::SaveProfiles));
        writer.field(L"supportsGameSpecificVfs", capabilities.has(fluxora::GameCapability::GameSpecificVfs));
        writer.field(L"supportsContentLayoutRules", capabilities.has(fluxora::GameCapability::ContentLayoutRules));
        writer.key(L"enabled").beginArray();
        if (capabilities.has(fluxora::GameCapability::Plugins))
        {
            writer.value(L"plugins");
        }
        if (capabilities.has(fluxora::GameCapability::LoadOrder))
        {
            writer.value(L"loadOrder");
        }
        if (capabilities.has(fluxora::GameCapability::RootFiles))
        {
            writer.value(L"rootFiles");
        }
        if (capabilities.has(fluxora::GameCapability::Archives))
        {
            writer.value(L"archives");
        }
        if (capabilities.has(fluxora::GameCapability::ScriptExtender))
        {
            writer.value(L"scriptExtender");
        }
        if (capabilities.has(fluxora::GameCapability::IniProfiles))
        {
            writer.value(L"iniProfiles");
        }
        if (capabilities.has(fluxora::GameCapability::SaveProfiles))
        {
            writer.value(L"saveProfiles");
        }
        if (capabilities.has(fluxora::GameCapability::GameSpecificVfs))
        {
            writer.value(L"gameSpecificVfs");
        }
        if (capabilities.has(fluxora::GameCapability::ContentLayoutRules))
        {
            writer.value(L"contentLayoutRules");
        }
        writer.endArray();
        writer.endObject();
    }

    std::wstring executableRoleName(fluxora::GameExecutableRole role)
    {
        switch (role)
        {
        case fluxora::GameExecutableRole::Primary:
            return L"primary";
        case fluxora::GameExecutableRole::Launcher:
            return L"launcher";
        case fluxora::GameExecutableRole::ScriptExtender:
            return L"scriptExtender";
        }

        return L"primary";
    }

    std::wstring workingDirectoryKindName(
        const std::optional<fluxora::GameExecutableWorkingDirectoryKind>& kind)
    {
        if (!kind.has_value())
        {
            return {};
        }

        switch (kind.value())
        {
        case fluxora::GameExecutableWorkingDirectoryKind::ExecutableDirectory:
            return L"executableDirectory";
        case fluxora::GameExecutableWorkingDirectoryKind::GameRoot:
            return L"gameRoot";
        }

        return {};
    }

    void writeExecutableDisplayMetadata(
        fluxora::JsonWriter& writer,
        std::wstring id,
        std::wstring displayName,
        std::wstring executableName,
        std::wstring role,
        std::wstring workingDirectoryKind,
        bool isPrimary,
        bool isLauncher,
        bool isScriptExtender)
    {
        writer.beginObject();
        writer.field(L"id", id);
        writer.field(L"displayName", displayName);
        writer.field(L"executableName", executableName);
        writer.field(L"role", role);
        writer.field(L"workingDirectoryKind", workingDirectoryKind);
        writer.field(L"isPrimary", isPrimary);
        writer.field(L"isLauncher", isLauncher);
        writer.field(L"isScriptExtender", isScriptExtender);
        writer.endObject();
    }

    void writeExecutableDisplayMetadataList(
        fluxora::JsonWriter& writer,
        const GameBridgeMetadata& metadata)
    {
        writer.beginArray();
        for (const fluxora::GameExecutableDefinition& executable : metadata.executableDefinitions)
        {
            writeExecutableDisplayMetadata(
                writer,
                executable.id,
                executable.displayName,
                executable.name.displayName(),
                executableRoleName(executable.role),
                workingDirectoryKindName(executable.workingDirectory),
                executable.role == fluxora::GameExecutableRole::Primary,
                executable.role == fluxora::GameExecutableRole::Launcher,
                executable.role == fluxora::GameExecutableRole::ScriptExtender);
        }

        if (metadata.executableDefinitions.empty())
        {
            int index = 1;
            for (const std::wstring& executableName : metadata.fallbackExecutables)
            {
                writeExecutableDisplayMetadata(
                    writer,
                    L"executable-" + std::to_wstring(index),
                    executableName,
                    executableName,
                    index == 1 ? L"primary" : L"",
                    L"",
                    index == 1,
                    false,
                    false);
                ++index;
            }
        }

        writer.endArray();
    }

    void writeLaunchTrackingMetadata(
        fluxora::JsonWriter& writer,
        fluxora::LaunchTrackingKind kind,
        const std::vector<std::wstring>& expectedChildProcessNames,
        const std::wstring& handoffDisplayName,
        std::uint32_t handoffTimeoutMs)
    {
        writer.beginObject();
        writer.field(L"kind", fluxora::launchTrackingKindName(kind));
        writer.stringArray(L"expectedChildProcessNames", expectedChildProcessNames);
        writer.field(L"handoffDisplayName", handoffDisplayName);
        writer.field(L"handoffTimeoutMs", static_cast<int>(handoffTimeoutMs));
        writer.endObject();
    }

    void writeLaunchTrackingMetadata(
        fluxora::JsonWriter& writer,
        const GameBridgeMetadata& metadata)
    {
        if (metadata.scriptExtenderLaunchRules.has_value())
        {
            const fluxora::GameScriptExtenderRules& scriptExtender =
                metadata.scriptExtenderLaunchRules.value();
            writeLaunchTrackingMetadata(
                writer,
                scriptExtender.launchTrackingKind,
                executableNameValues(scriptExtender.expectedChildProcessNames),
                scriptExtender.handoffDisplayName,
                scriptExtender.handoffTimeoutMs);
            return;
        }

        writeLaunchTrackingMetadata(
            writer,
            fluxora::LaunchTrackingKind::DirectProcess,
            {},
            {},
            0);
    }

    void writeContentLayoutSummary(
        fluxora::JsonWriter& writer,
        const GameBridgeMetadata& metadata)
    {
        const bool supported =
            metadata.capabilities.has(fluxora::GameCapability::ContentLayoutRules);
        const std::vector<std::wstring> archiveExtensions =
            metadata.archiveExtensions.empty()
                ? extensionValues(metadata.contentLayout.archiveExtensions)
                : metadata.archiveExtensions;

        writer.beginObject();
        writer.field(L"supported", supported);
        writer.field(L"hasWarnings", false);
        writer.field(L"hasBlockers", false);
        writer.field(L"dataFolder", metadata.contentLayout.dataFolder);
        writer.field(L"supportsRootFiles", metadata.contentLayout.supportsRootFiles);
        writer.field(L"rootFileWrapperDirectory", metadata.contentLayout.rootFileWrapperDirectory);
        writer.stringArray(L"pluginExtensions", extensionValues(metadata.contentLayout.pluginExtensions));
        writer.stringArray(L"archiveExtensions", archiveExtensions);
        writer.stringArray(L"scriptExtenderLoaders", executableNameValues(metadata.contentLayout.scriptExtenderLoaders));
        writer.stringArray(L"gameDataDirectories", metadata.contentLayout.gameDataDirectories);
        writer.stringArray(L"scriptExtenderDataPaths", pathValues(metadata.contentLayout.scriptExtenderDataPaths));
        writer.field(
            L"summary",
            supported
                ? L"Content placement is driven by the selected game definition."
                : L"Content layout rules are not enabled for this game.");
        writer.key(L"details").beginArray();
        if (!metadata.contentLayout.dataFolder.empty())
        {
            writer.value(L"Game data content is placed under " + metadata.contentLayout.dataFolder + L".");
        }
        if (metadata.contentLayout.supportsRootFiles)
        {
            writer.value(L"Root files can be staged through the configured wrapper directory.");
        }
        if (!archiveExtensions.empty())
        {
            writer.value(L"Archive extensions are exposed separately from plugin extensions.");
        }
        writer.endArray();
        writer.stringArray(L"warnings", {});
        writer.stringArray(L"blockers", {});
        writer.endObject();
    }

    void writeHealthFinding(fluxora::JsonWriter& writer, const fluxora::GameHealthFinding& finding)
    {
        writer.beginObject();
        writer.field(L"severity", fluxora::GameHealthCheckService::healthSeverityName(finding.severity));
        writer.field(L"code", finding.code);
        writer.field(L"message", finding.message);
        writer.field(L"path", finding.path.wstring());
        writer.field(L"critical", finding.critical);
        writer.endObject();
    }

    void writeGameHealthSummary(fluxora::JsonWriter& writer, const fluxora::GameHealthCheckResult& health)
    {
        writer.beginObject();
        writer.field(L"gameId", health.gameId.value());
        writer.field(L"displayName", health.displayName);
        writer.field(L"status", fluxora::GameHealthCheckService::healthStatusName(health.status));
        writer.field(L"summary", health.summary);
        writer.field(L"hasBlockers", health.hasBlockers());
        writer.field(L"allowsAutomation", health.allowsAutomation());
        writer.stringArray(L"matchedFiles", health.matchedFiles);
        writer.stringArray(L"missingFiles", health.missingFiles);
        writer.stringArray(L"warnings", health.warnings);
        writer.key(L"findings").beginArray();
        for (const fluxora::GameHealthFinding& finding : health.findings)
        {
            writeHealthFinding(writer, finding);
        }
        writer.endArray();
        writer.endObject();
    }

    bool healthStatusHasBlockers(std::wstring status)
    {
        status = fluxora::toAsciiLower(fluxora::trimAscii(status));
        return status == L"broken" || status == L"unsupported";
    }

    void writeFallbackGameHealthSummary(
        fluxora::JsonWriter& writer,
        const fluxora::ProjectDescriptor& project,
        const GameBridgeMetadata& metadata)
    {
        const bool hasFingerprint = project.fingerprint.has_value();
        const std::wstring status = hasFingerprint && !project.fingerprint->healthStatusAtCreation.empty()
            ? project.fingerprint->healthStatusAtCreation
            : L"unknown";
        const std::wstring gameId = hasFingerprint && !project.fingerprint->gameId.empty()
            ? project.fingerprint->gameId
            : metadata.gameId;
        const std::wstring displayName = hasFingerprint && !project.fingerprint->gameDisplayName.empty()
            ? project.fingerprint->gameDisplayName
            : metadata.displayName;

        writer.beginObject();
        writer.field(L"gameId", gameId);
        writer.field(L"displayName", displayName);
        writer.field(L"status", status);
        writer.field(
            L"summary",
            hasFingerprint
                ? L"Health status recorded when the project was created."
                : L"Health status is unavailable for this project.");
        writer.field(L"hasBlockers", healthStatusHasBlockers(status));
        writer.field(L"allowsAutomation", !healthStatusHasBlockers(status) && status != L"unknown");
        writer.stringArray(L"matchedFiles", {});
        writer.stringArray(L"missingFiles", {});
        writer.stringArray(L"warnings", {});
        writer.key(L"findings").beginArray().endArray();
        writer.endObject();
    }

    void writeProjectHealthSummary(
        fluxora::JsonWriter& writer,
        const fluxora::ProjectDescriptor& project,
        const GameBridgeMetadata& metadata,
        const std::filesystem::path& gamePath,
        bool allowFilesystemCheck)
    {
        if (allowFilesystemCheck && metadata.supported && !metadata.gameId.empty() && !gamePath.empty())
        {
            try
            {
                const fluxora::GameSupportRegistry& registry = fluxora::GameSupportRegistry::embedded();
                fluxora::GameDetectionService detectionService(registry);
                fluxora::GameDetectionRequest request;
                request.manualGameId = fluxora::GameId::parseOrThrow(metadata.gameId);
                request.installPath = gamePath;

                const fluxora::GameDetectionResult detection = detectionService.detect(request);
                if (detection.detected)
                {
                    writeGameHealthSummary(writer, fluxora::GameHealthCheckService().check(detection));
                    return;
                }
            }
            catch (const std::exception&)
            {
            }
        }

        writeFallbackGameHealthSummary(writer, project, metadata);
    }

    void writeProjectFingerprint(
        fluxora::JsonWriter& writer,
        const std::optional<fluxora::ProjectFingerprint>& fingerprint)
    {
        if (fingerprint.has_value())
        {
            fluxora::writeProjectFingerprint(writer, fingerprint.value());
        }
        else
        {
            writer.nullValue();
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
        writer.key(L"executableDisplayMetadata");
        writeExecutableDisplayMetadata(
            writer,
            executable.id,
            executable.displayName,
            std::filesystem::path(executable.executablePath).filename().wstring(),
            L"",
            executable.workingDirectory.empty() ? L"executableDirectory" : L"",
            false,
            false,
            false);
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
        writer.field(L"launchTrackingKind", fluxora::launchTrackingKindName(result.launchTrackingKind));
        writer.stringArray(L"expectedChildProcessNames", result.expectedChildProcessNames);
        writer.field(L"handoffDisplayName", result.handoffDisplayName);
        writer.field(L"handoffTimeoutMs", static_cast<int>(result.handoffTimeoutMs));
        writer.key(L"launchTrackingMetadata");
        writeLaunchTrackingMetadata(
            writer,
            result.launchTrackingKind,
            result.expectedChildProcessNames,
            result.handoffDisplayName,
            result.handoffTimeoutMs);
        writer.key(L"executableDisplayMetadata");
        writeExecutableDisplayMetadata(
            writer,
            result.executable.id,
            result.executable.displayName,
            std::filesystem::path(result.executable.executablePath).filename().wstring(),
            L"",
            result.executable.workingDirectory.empty() ? L"executableDirectory" : L"",
            false,
            false,
            false);
        writer.field(L"processId", static_cast<int>(result.processId));
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

    void writeFluxPackSummary(fluxora::JsonWriter& writer, const fluxora::FluxPackSummary& summary)
    {
        writer.beginObject();
        writer.field(L"outputPath", summary.outputPath.wstring());
        writer.field(L"buildName", summary.buildName);
        writer.field(L"formatVersion", summary.formatVersion);
        writer.field(L"manifestBytes", summary.manifestBytes);
        writer.field(L"sourceArchiveCount", summary.sourceArchiveCount);
        writer.field(L"generatedAssetCount", summary.generatedAssetCount);
        writer.field(L"customPatchCount", summary.customPatchCount);
        writer.field(L"customConfigCount", summary.customConfigCount);
        writer.field(L"installStepCount", summary.installStepCount);
        writer.field(L"generatedAssetsIncluded", summary.generatedAssetsIncluded);
        writer.field(L"installPlanAvailable", summary.installPlanAvailable);
        writer.endObject();
    }

    std::wstring serializeFluxPackSummary(const fluxora::FluxPackSummary& summary)
    {
        fluxora::JsonWriter writer;
        writeFluxPackSummary(writer, summary);
        return writer.str();
    }

    std::wstring serializeFluxPackInstallResult(const fluxora::FluxPackInstallResult& result)
    {
        fluxora::JsonWriter writer;
        writer.beginObject();
        writer.key(L"summary");
        writeFluxPackSummary(writer, result.summary);
        writer.field(L"configPath", result.configPath.wstring());
        writer.field(L"projectDirectory", result.projectDirectory.wstring());
        writer.field(L"buildName", result.buildName);
        writer.field(L"totalSourceCount", result.totalSourceCount);
        writer.field(L"installedSourceCount", result.installedSourceCount);
        writer.field(L"pendingSourceCount", result.pendingSourceCount);
        writer.field(L"failedSourceCount", result.failedSourceCount);
        writer.field(L"appliedConfigCount", result.appliedConfigCount);
        writer.field(L"appliedProfileOrderItemCount", result.appliedProfileOrderItemCount);
        writer.field(L"hasWarnings", result.hasWarnings);
        writer.endObject();
        return writer.str();
    }

    void writeResolvedTemplate(fluxora::JsonWriter& writer, const fluxora::BuildTemplate& tpl)
    {
        const GameBridgeMetadata metadata = resolveGameBridgeMetadata(tpl);
        writer.beginObject();
        writer.field(L"id", tpl.id);
        writer.field(L"displayName", tpl.displayName);
        writer.field(L"gameName", tpl.gameName);
        writer.field(L"summary", tpl.summary);
        writer.field(L"uiTemplateId", metadata.uiTemplateId);
        writer.field(L"baseTemplateId", tpl.baseTemplateId);
        writer.field(L"defaultProfile", tpl.defaultProfileName);
        writer.field(L"dataDirectory", tpl.dataDirectory);
        writer.field(L"nexusDomain", tpl.nexusDomain);
        writer.stringArray(L"folders", tpl.folders);
        writer.stringArray(L"profileFiles", tpl.profileFiles);
        writer.stringArray(L"basePlugins", tpl.basePlugins);
        writer.stringArray(L"pluginExtensions", tpl.pluginExtensions);
        writer.stringArray(L"archiveExtensions", metadata.archiveExtensions);
        writer.stringArray(L"requiredFiles", metadata.requiredFiles);
        writer.stringArray(L"executables", tpl.executables);
        writeCapabilities(writer, tpl);
        writer.key(L"gameCapabilities");
        writeGameCapabilities(writer, metadata.capabilities);
        writer.key(L"contentLayoutSummary");
        writeContentLayoutSummary(writer, metadata);
        writer.key(L"executableDisplayMetadata");
        writeExecutableDisplayMetadataList(writer, metadata);
        writer.key(L"launchTrackingMetadata");
        writeLaunchTrackingMetadata(writer, metadata);
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

        const GameBridgeMetadata metadata = resolveGameBridgeMetadata(result.resolvedTemplate);

        writer.beginObject();
        writer.field(L"id", project.configPath.wstring());
        writer.field(L"name", project.name);
        writer.field(L"templateId", project.templateId);
        writer.field(L"uiTemplateId", metadata.uiTemplateId);
        writer.field(L"gameName", project.gameName);
        writer.field(L"gamePath", pathSettings.gameDirectory.wstring());
        writer.field(L"installRootDirectory", project.installRootDirectory.wstring());
        writer.field(L"projectDirectory", project.projectDirectory.wstring());
        writer.field(L"configPath", project.configPath.wstring());
        writer.key(L"gameCapabilities");
        writeGameCapabilities(writer, metadata.capabilities);
        writer.key(L"gameHealthSummary");
        writeProjectHealthSummary(
            writer,
            project,
            metadata,
            pathSettings.gameDirectory,
            !tolerateExecutableErrors);
        writer.key(L"projectFingerprint");
        writeProjectFingerprint(writer, project.fingerprint);
        writer.key(L"contentLayoutSummary");
        writeContentLayoutSummary(writer, metadata);
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
            const GameBridgeMetadata metadata = resolveGameBridgeMetadata(tpl);
            writer.beginObject();
            writer.field(L"id", tpl.id);
            writer.field(L"displayName", tpl.displayName);
            writer.field(L"gameName", tpl.gameName);
            writer.field(L"summary", tpl.summary);
            writer.field(L"uiTemplateId", metadata.uiTemplateId);
            writer.key(L"gameCapabilities");
            writeGameCapabilities(writer, metadata.capabilities);
            writer.stringArray(L"archiveExtensions", metadata.archiveExtensions);
            writer.stringArray(L"requiredFiles", metadata.requiredFiles);
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

    std::wstring placementTargetName(fluxora::PlacementTarget target)
    {
        switch (target)
        {
        case fluxora::PlacementTarget::GameRoot:
            return L"gameRoot";
        case fluxora::PlacementTarget::Data:
            return L"data";
        case fluxora::PlacementTarget::Profile:
            return L"profile";
        case fluxora::PlacementTarget::Overwrite:
            return L"overwrite";
        case fluxora::PlacementTarget::Blocked:
            return L"blocked";
        }

        return L"unknown";
    }

    std::wstring contentAreaName(fluxora::ContentArea area)
    {
        switch (area)
        {
        case fluxora::ContentArea::GameRoot:
            return L"gameRoot";
        case fluxora::ContentArea::Data:
            return L"data";
        case fluxora::ContentArea::Profile:
            return L"profile";
        case fluxora::ContentArea::Ini:
            return L"ini";
        case fluxora::ContentArea::Saves:
            return L"saves";
        case fluxora::ContentArea::Overwrite:
            return L"overwrite";
        }

        return L"unknown";
    }

    std::wstring contentLayoutClassificationName(fluxora::ContentLayoutClassification classification)
    {
        switch (classification)
        {
        case fluxora::ContentLayoutClassification::GameData:
            return L"gameData";
        case fluxora::ContentLayoutClassification::GameRoot:
            return L"gameRoot";
        case fluxora::ContentLayoutClassification::Plugin:
            return L"plugin";
        case fluxora::ContentLayoutClassification::Archive:
            return L"archive";
        case fluxora::ContentLayoutClassification::ScriptExtender:
            return L"scriptExtender";
        case fluxora::ContentLayoutClassification::Config:
            return L"config";
        case fluxora::ContentLayoutClassification::Ini:
            return L"ini";
        case fluxora::ContentLayoutClassification::Save:
            return L"save";
        case fluxora::ContentLayoutClassification::ToolExecutable:
            return L"toolExecutable";
        case fluxora::ContentLayoutClassification::Documentation:
            return L"documentation";
        case fluxora::ContentLayoutClassification::Screenshots:
            return L"screenshots";
        case fluxora::ContentLayoutClassification::Unknown:
            return L"unknown";
        case fluxora::ContentLayoutClassification::Unsafe:
            return L"unsafe";
        }

        return L"unknown";
    }

    void writePlacementPlanSummary(
        fluxora::JsonWriter& writer,
        const fluxora::ContentLayoutSummary& summary)
    {
        writer.beginObject();
        writer.field(L"supported", summary.supported);
        writer.field(L"hasWarnings", summary.hasWarnings);
        writer.field(L"hasBlockers", summary.hasBlockers);
        writer.field(L"totalEntries", static_cast<std::uintmax_t>(summary.totalEntries));
        writer.field(L"plannedEntries", static_cast<std::uintmax_t>(summary.plannedEntries));
        writer.field(L"gameDataEntries", static_cast<std::uintmax_t>(summary.gameDataEntries));
        writer.field(L"gameRootEntries", static_cast<std::uintmax_t>(summary.gameRootEntries));
        writer.field(L"pluginEntries", static_cast<std::uintmax_t>(summary.pluginEntries));
        writer.field(L"archiveEntries", static_cast<std::uintmax_t>(summary.archiveEntries));
        writer.field(L"scriptExtenderEntries", static_cast<std::uintmax_t>(summary.scriptExtenderEntries));
        writer.field(L"unknownEntries", static_cast<std::uintmax_t>(summary.unknownEntries));
        writer.field(L"unsafeEntries", static_cast<std::uintmax_t>(summary.unsafeEntries));
        writer.endObject();
    }

    void writePlacementPlanEntry(
        fluxora::JsonWriter& writer,
        const fluxora::PlacementPlanEntry& entry)
    {
        writer.beginObject();
        writer.field(L"sourcePath", entry.sourcePath.path().generic_wstring());
        writer.field(L"target", placementTargetName(entry.target));
        writer.field(L"contentArea", contentAreaName(entry.contentArea));
        writer.field(L"targetRelativePath", entry.targetRelativePath.path().generic_wstring());
        writer.field(L"classification", contentLayoutClassificationName(entry.classification));
        writer.field(L"explanation", entry.explanation);
        writer.field(L"manualOverrideAllowed", entry.manualOverrideAllowed);
        writer.key(L"safeManualTargets").beginArray();
        for (fluxora::PlacementTarget target : entry.safeManualTargets)
        {
            writer.value(placementTargetName(target));
        }
        writer.endArray();
        writer.endObject();
    }

    void writePlacementFinding(
        fluxora::JsonWriter& writer,
        const fluxora::ValidationFinding& finding)
    {
        writer.beginObject();
        writer.field(L"severity", fluxora::GameHealthCheckService::healthSeverityName(finding.severity));
        writer.field(
            L"path",
            finding.path.has_value()
                ? finding.path->path().generic_wstring()
                : std::wstring{});
        writer.field(L"classification", contentLayoutClassificationName(finding.classification));
        writer.field(L"message", finding.message);
        writer.field(L"blocksInstall", finding.blocksInstall);
        writer.endObject();
    }

    std::wstring serializePlacementPlan(const fluxora::PlacementPlan& plan)
    {
        fluxora::JsonWriter writer;
        writer.beginObject();
        writer.field(L"gameId", plan.gameId.value());
        writer.field(L"gameDisplayName", plan.gameDisplayName);
        writer.field(L"rootFileWrapperDirectory", plan.rootFileWrapperDirectory);
        writer.field(L"canInstall", plan.canInstall());
        writer.key(L"summary");
        writePlacementPlanSummary(writer, plan.summary);
        writer.key(L"entries").beginArray();
        for (const fluxora::PlacementPlanEntry& entry : plan.entries)
        {
            writePlacementPlanEntry(writer, entry);
        }
        writer.endArray();
        writer.key(L"validationFindings").beginArray();
        for (const fluxora::ValidationFinding& finding : plan.validationFindings)
        {
            writePlacementFinding(writer, finding);
        }
        writer.endArray();
        writer.field(L"explanationSummary", plan.userExplanation.summary);
        writer.stringArray(L"explanationDetails", plan.userExplanation.details);
        writer.endObject();
        return writer.str();
    }

    void writeFomodFileEntry(fluxora::JsonWriter& writer, const fluxora::FomodFileEntry& file)
    {
        writer.beginObject();
        writer.field(L"source", file.source);
        writer.field(L"destination", file.destination);
        writer.field(L"isFolder", file.isFolder);
        writer.field(L"alwaysInstall", file.alwaysInstall);
        writer.field(L"installIfUsable", file.installIfUsable);
        writer.field(L"priority", file.priority);
        writer.endObject();
    }

    void writeFomodFileDependencyState(
        fluxora::JsonWriter& writer,
        const fluxora::FomodFileDependencyState& dependency)
    {
        writer.beginObject();
        writer.field(L"file", dependency.file);
        writer.field(L"exists", dependency.exists);
        writer.endObject();
    }

    void writeFomodDependency(fluxora::JsonWriter& writer, const fluxora::FomodDependencyNode& dependency)
    {
        writer.beginObject();
        writer.field(L"kind", dependency.kind);
        writer.field(L"operator", dependency.op);
        writer.field(L"file", dependency.file);
        writer.field(L"state", dependency.state);
        writer.field(L"flag", dependency.flag);
        writer.field(L"value", dependency.value);
        writer.field(L"version", dependency.version);
        writer.key(L"children").beginArray();
        for (const fluxora::FomodDependencyNode& child : dependency.children)
        {
            writeFomodDependency(writer, child);
        }
        writer.endArray();
        writer.endObject();
    }

    void writeFomodOption(fluxora::JsonWriter& writer, const fluxora::FomodOption& option)
    {
        writer.beginObject();
        writer.field(L"id", option.id);
        writer.field(L"name", option.name);
        writer.field(L"description", option.description);
        writer.field(L"imagePath", option.imagePath);
        writer.field(L"type", option.type);
        writer.field(L"defaultType", option.defaultType);
        writer.key(L"flags").beginArray();
        for (const fluxora::FomodConditionFlag& flag : option.flags)
        {
            writer.beginObject();
            writer.field(L"name", flag.name);
            writer.field(L"value", flag.value);
            writer.endObject();
        }
        writer.endArray();
        writer.key(L"typePatterns").beginArray();
        for (const fluxora::FomodTypePattern& pattern : option.typePatterns)
        {
            writer.beginObject();
            writer.key(L"dependencies");
            writeFomodDependency(writer, pattern.dependencies);
            writer.field(L"type", pattern.type);
            writer.endObject();
        }
        writer.endArray();
        writer.endObject();
    }

    void writeFomodGroup(fluxora::JsonWriter& writer, const fluxora::FomodGroup& group)
    {
        writer.beginObject();
        writer.field(L"id", group.id);
        writer.field(L"name", group.name);
        writer.field(L"type", group.type);
        writer.key(L"options").beginArray();
        for (const fluxora::FomodOption& option : group.options)
        {
            writeFomodOption(writer, option);
        }
        writer.endArray();
        writer.endObject();
    }

    void writeFomodStep(fluxora::JsonWriter& writer, const fluxora::FomodStep& step)
    {
        writer.beginObject();
        writer.field(L"id", step.id);
        writer.field(L"name", step.name);
        writer.key(L"visible");
        if (step.visible.has_value())
        {
            writeFomodDependency(writer, step.visible.value());
        }
        else
        {
            writer.nullValue();
        }
        writer.key(L"groups").beginArray();
        for (const fluxora::FomodGroup& group : step.groups)
        {
            writeFomodGroup(writer, group);
        }
        writer.endArray();
        writer.endObject();
    }

    std::wstring serializeFomodInstaller(const fluxora::FomodInstallerDescriptor& descriptor)
    {
        fluxora::JsonWriter writer;
        writer.beginObject();
        writer.field(L"isFomod", descriptor.isFomod);
        writer.field(L"moduleName", descriptor.moduleName);
        writer.field(L"moduleVersion", descriptor.moduleVersion);
        writer.field(L"moduleId", descriptor.moduleId);
        writer.field(L"moduleImagePath", descriptor.moduleImagePath);
        writer.field(L"memoryKey", descriptor.memoryKey);
        writer.field(L"hasPreviousSelection", descriptor.hasPreviousSelection);
        writer.stringArray(L"previousSelectedOptionIds", descriptor.previousSelectedOptionIds);
        writer.key(L"fileDependencies").beginArray();
        for (const fluxora::FomodFileDependencyState& dependency : descriptor.fileDependencyStates)
        {
            writeFomodFileDependencyState(writer, dependency);
        }
        writer.endArray();
        writer.key(L"requiredFiles").beginArray();
        for (const fluxora::FomodFileEntry& file : descriptor.requiredFiles)
        {
            writeFomodFileEntry(writer, file);
        }
        writer.endArray();
        writer.key(L"steps").beginArray();
        for (const fluxora::FomodStep& step : descriptor.steps)
        {
            writeFomodStep(writer, step);
        }
        writer.endArray();
        writer.key(L"conditionalFilePatterns").beginArray();
        for (const fluxora::FomodConditionalFilePattern& pattern : descriptor.conditionalFilePatterns)
        {
            writer.beginObject();
            writer.key(L"dependencies");
            writeFomodDependency(writer, pattern.dependencies);
            writer.key(L"files").beginArray();
            for (const fluxora::FomodFileEntry& file : pattern.files)
            {
                writeFomodFileEntry(writer, file);
            }
            writer.endArray();
            writer.endObject();
        }
        writer.endArray();
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

    std::wstring serializeFluxPackInstallProgress(const fluxora::FluxPackInstallProgress& progress)
    {
        fluxora::JsonWriter writer;
        writer.beginObject();
        writer.field(L"phase", progress.phase);
        writer.field(L"currentStep", progress.currentStep);
        writer.field(L"currentItem", progress.currentItem);
        writer.field(L"statusMessage", progress.statusMessage);
        writer.field(L"overallPercent", progress.overallPercent);
        writer.field(L"totalSourceCount", progress.totalSourceCount);
        writer.field(L"installedSourceCount", progress.installedSourceCount);
        writer.field(L"pendingSourceCount", progress.pendingSourceCount);
        writer.field(L"failedSourceCount", progress.failedSourceCount);
        writer.key(L"providers").beginArray();
        for (const fluxora::FluxPackProviderProgress& provider : progress.providers)
        {
            writer.beginObject();
            writer.field(L"providerId", provider.providerId);
            writer.field(L"displayName", provider.displayName);
            writer.field(L"totalCount", provider.totalCount);
            writer.field(L"completedCount", provider.completedCount);
            writer.field(L"pendingCount", provider.pendingCount);
            writer.field(L"failedCount", provider.failedCount);
            writer.field(L"currentItem", provider.currentItem);
            writer.field(L"statusText", provider.statusText);
            writer.field(L"progressPercent", provider.progressPercent);
            writer.endObject();
        }
        writer.endArray();
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

    fluxora::PluginRuleContext resolvePluginRuleContextForTemplate(const wchar_t* templateId)
    {
        if (isBlank(templateId))
        {
            throw std::invalid_argument("Template id is required.");
        }

        const fluxora::GameSupportRegistry& registry = fluxora::GameSupportRegistry::embedded();
        const fluxora::GameSupportLookupResult lookup = registry.lookupById(templateId);
        if (!lookup.supported || lookup.support == nullptr)
        {
            throw std::invalid_argument("Plugin management is not supported by the selected game.");
        }

        const fluxora::GameSupportComponents& components = lookup.support->components();
        return fluxora::PluginRuleContext{
            components.pluginRulesProvider,
            &lookup.support->capabilities(),
            nullptr,
            lookup.support->identity().defaultProfileName,
            lookup.support->identity().id.value()
        };
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

    std::string textForLog(std::wstring_view value)
    {
        if (value.empty())
        {
            return {};
        }

#ifdef _WIN32
        const int requiredLength = WideCharToMultiByte(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (requiredLength > 0)
        {
            std::string out(static_cast<std::size_t>(requiredLength), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                out.data(),
                requiredLength,
                nullptr,
                nullptr);
            return out;
        }
#endif

        std::string fallback;
        fallback.reserve(value.size());
        for (wchar_t ch : value)
        {
            fallback.push_back(ch >= 0 && ch < 0x80 ? static_cast<char>(ch) : '?');
        }
        return fallback;
    }

    std::string pathForLog(const std::filesystem::path& path)
    {
        return textForLog(path.wstring());
    }

    bool tryParseExistingModInstallMode(int value, fluxora::ExistingModInstallMode& mode)
    {
        switch (value)
        {
        case 0:
            mode = fluxora::ExistingModInstallMode::FailIfExists;
            return true;
        case 1:
            mode = fluxora::ExistingModInstallMode::Replace;
            return true;
        case 2:
            mode = fluxora::ExistingModInstallMode::Merge;
            return true;
        default:
            return false;
        }
    }

    const char* existingModInstallModeForLog(fluxora::ExistingModInstallMode mode)
    {
        switch (mode)
        {
        case fluxora::ExistingModInstallMode::Replace:
            return "replace";
        case fluxora::ExistingModInstallMode::Merge:
            return "merge";
        case fluxora::ExistingModInstallMode::FailIfExists:
        default:
            return "failIfExists";
        }
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

    void logBridge(fluxora::LogLevel level, std::string_view message) noexcept
    {
        try
        {
            if (currentCore != nullptr && currentCore->logger().isInitialized())
            {
                currentCore->logger().write(fluxora::LogChannel::Bridge, level, "NativeApi", message);
            }
        }
        catch (...)
        {
        }
    }

    void logOperation(fluxora::LogLevel level, std::string_view category, std::string_view message) noexcept
    {
        try
        {
            if (currentCore != nullptr && currentCore->logger().isInitialized())
            {
                currentCore->logger().writeOperation(level, category, message);
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
        logBridge(
            isInvalidArgument ? fluxora::LogLevel::Warning : fluxora::LogLevel::Error,
            std::string("Native API call failed: ") + message);
        logOperation(
            isInvalidArgument ? fluxora::LogLevel::Warning : fluxora::LogLevel::Error,
            "NativeApi",
            std::string("Native operation failed: ") + message);
        return isInvalidArgument
            ? FluxoraCoreResultInvalidArgument
            : FluxoraCoreResultCoreError;
    }

    int installDownloadWithMode(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath,
        const wchar_t* modName,
        int existingModMode,
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

            fluxora::ExistingModInstallMode mode = fluxora::ExistingModInstallMode::FailIfExists;
            if (!tryParseExistingModInstallMode(existingModMode, mode))
            {
                lastError = L"Existing mod install mode is invalid.";
                return FluxoraCoreResultInvalidArgument;
            }

            logBridge(fluxora::LogLevel::Info, "fluxora_install_download started.");
            logOperation(
                fluxora::LogLevel::Info,
                "Downloads",
                std::string("Install download requested. projectDirectory=\"") +
                    pathForLog(std::filesystem::path(projectDirectory)) + "\", downloadPath=\"" +
                    pathForLog(std::filesystem::path(downloadPath)) + "\", modName=\"" +
                    textForLog(modName) + "\", existingModMode=\"" +
                    existingModInstallModeForLog(mode) + "\"");
            const std::wstring json = serializeInstalledMod(
                core().downloads().installDownload(
                    std::filesystem::path(projectDirectory),
                    std::filesystem::path(downloadPath),
                    modName,
                    mode));
            logOperation(fluxora::LogLevel::Info, "Downloads", "Install download completed.");
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
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

    int fluxora_set_operation_context(const wchar_t* operationId)
    {
        if (isBlank(operationId))
        {
            fluxora::Logger::clearOperationId();
        }
        else
        {
            fluxora::Logger::setOperationId(operationId);
        }

        return FluxoraCoreResultOk;
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

            logBridge(fluxora::LogLevel::Info, "fluxora_create_project started.");
            logOperation(
                fluxora::LogLevel::Info,
                "Project",
                std::string("Create project requested. name=\"") + textForLog(projectName) +
                    "\", template=\"" + textForLog(templateId) +
                    "\", gamePath=\"" + pathForLog(request.gamePath) +
                    "\", installRoot=\"" + pathForLog(request.installRootDirectory) + "\"");
            const fluxora::ProjectDescriptor project = core().projects().createProject(request);
            const fluxora::ProjectOpenResult result{
                project,
                core().templates().resolve(project.templateId)
            };
            logOperation(
                fluxora::LogLevel::Info,
                "Project",
                std::string("Create project completed. projectDirectory=\"") +
                    pathForLog(project.projectDirectory) + "\", configPath=\"" +
                    pathForLog(project.configPath) + "\"");
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

            logBridge(fluxora::LogLevel::Info, "fluxora_rename_project started.");
            logOperation(
                fluxora::LogLevel::Info,
                "Project",
                std::string("Rename project requested. configPath=\"") +
                    pathForLog(std::filesystem::path(configPath)) + "\", newName=\"" +
                    textForLog(newName) + "\"");
            const fluxora::ProjectOpenResult result = core().projects().renameProject(
                std::filesystem::path(configPath),
                newName);
            logOperation(
                fluxora::LogLevel::Info,
                "Project",
                std::string("Rename project completed. configPath=\"") +
                    pathForLog(result.project.configPath) + "\", name=\"" +
                    textForLog(result.project.name) + "\"");
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

            logBridge(fluxora::LogLevel::Info, "fluxora_delete_project started.");
            logOperation(
                fluxora::LogLevel::Info,
                "Project",
                std::string("Delete project requested. configPath=\"") +
                    pathForLog(std::filesystem::path(configPath)) + "\"");
            core().projects().deleteProject(std::filesystem::path(configPath));
            logOperation(fluxora::LogLevel::Info, "Project", "Delete project completed.");
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

            logBridge(fluxora::LogLevel::Info, "fluxora_delete_project_with_progress started.");
            logOperation(
                fluxora::LogLevel::Info,
                "Project",
                std::string("Delete project requested. configPath=\"") +
                    pathForLog(std::filesystem::path(configPath)) + "\"");
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
            logOperation(fluxora::LogLevel::Info, "Project", "Delete project completed.");
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

            logBridge(fluxora::LogLevel::Info, "fluxora_save_build_path_settings started.");
            logOperation(
                fluxora::LogLevel::Info,
                "BuildPaths",
                std::string("Save build path settings requested. configPath=\"") +
                    pathForLog(std::filesystem::path(configPath)) + "\"");
            const fluxora::BuildPathSettings saved =
                core().buildPathSettings().saveForConfig(
                    std::filesystem::path(configPath),
                    parseBuildPathSettingsJson(settingsJson));
            logOperation(
                fluxora::LogLevel::Info,
                "BuildPaths",
                std::string("Save build path settings completed. gameDirectory=\"") +
                    pathForLog(saved.gameDirectory) + "\", modsDirectory=\"" +
                    pathForLog(saved.modsDirectory) + "\", downloadsDirectory=\"" +
                    pathForLog(saved.downloadsDirectory) + "\"");
            return writeToBuffer(serializeBuildPathSettings(saved), jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_export_fluxpack(
        const wchar_t* configPath,
        const wchar_t* outputPath,
        int includeGeneratedAssets,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(configPath) || isBlank(outputPath))
            {
                lastError = L"Build config path and FluxPack output path are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            logBridge(fluxora::LogLevel::Info, "fluxora_export_fluxpack started.");
            const fluxora::FluxPackSummary summary = core().fluxPacks().exportProject(
                fluxora::FluxPackExportRequest{
                    std::filesystem::path(configPath),
                    std::filesystem::path(outputPath),
                    includeGeneratedAssets != 0
                });
            return writeToBuffer(serializeFluxPackSummary(summary), jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_inspect_fluxpack(
        const wchar_t* fluxPackPath,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(fluxPackPath))
            {
                lastError = L"FluxPack path is required.";
                return FluxoraCoreResultInvalidArgument;
            }

            logBridge(fluxora::LogLevel::Info, "fluxora_inspect_fluxpack started.");
            const fluxora::FluxPackSummary summary =
                core().fluxPacks().inspectFluxPack(std::filesystem::path(fluxPackPath));
            return writeToBuffer(serializeFluxPackSummary(summary), jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_install_fluxpack(
        const wchar_t* fluxPackPath,
        const wchar_t* installRootDirectory,
        FluxoraCoreProgressCallback progressCallback,
        void* progressUserData,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        try
        {
            if (isBlank(fluxPackPath) || isBlank(installRootDirectory))
            {
                lastError = L"FluxPack path and install root directory are required.";
                return FluxoraCoreResultInvalidArgument;
            }

            logBridge(fluxora::LogLevel::Info, "fluxora_install_fluxpack started.");
            const fluxora::FluxPackInstallResult result =
                core().fluxPacks().installFluxPack(fluxora::FluxPackInstallRequest{
                    std::filesystem::path(fluxPackPath),
                    std::filesystem::path(installRootDirectory),
                    [progressCallback, progressUserData](const fluxora::FluxPackInstallProgress& progress)
                    {
                        if (progressCallback != nullptr)
                        {
                            const std::wstring json = serializeFluxPackInstallProgress(progress);
                            progressCallback(json.c_str(), progressUserData);
                        }
                    }
                });
            return writeToBuffer(serializeFluxPackInstallResult(result), jsonBuffer, jsonBufferLength);
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

            logBridge(fluxora::LogLevel::Info, "fluxora_import_mod_organizer_instance started.");
            fluxora::ModOrganizerImportRequest request;
            request.sourceDirectory = std::filesystem::path(sourceDirectory);
            request.destinationRootDirectory = std::filesystem::path(destinationRootDirectory);
            request.existingConfigPath = isBlank(existingConfigPath)
                ? std::filesystem::path{}
                : std::filesystem::path(existingConfigPath);
            request.mode = replaceExisting != 0
                ? fluxora::ModOrganizerImportMode::ReplaceExisting
                : fluxora::ModOrganizerImportMode::CreateNew;
            logOperation(
                fluxora::LogLevel::Info,
                "MO2Import",
                std::string("Mod Organizer import requested. source=\"") +
                    pathForLog(request.sourceDirectory) + "\", destinationRoot=\"" +
                    pathForLog(request.destinationRootDirectory) + "\", existingConfig=\"" +
                    pathForLog(request.existingConfigPath) + "\", replaceExisting=" +
                    (replaceExisting != 0 ? "true" : "false"));
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
            logOperation(
                fluxora::LogLevel::Info,
                "MO2Import",
                std::string("Mod Organizer import completed. projectDirectory=\"") +
                    pathForLog(result.project.project.projectDirectory) + "\", configPath=\"" +
                    pathForLog(result.project.project.configPath) + "\"");
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

            logBridge(fluxora::LogLevel::Info, "fluxora_save_game_executables started.");
            logOperation(
                fluxora::LogLevel::Info,
                "Executables",
                std::string("Save executable list requested. configPath=\"") +
                    pathForLog(std::filesystem::path(configPath)) + "\"");
            const std::vector<fluxora::GameExecutable> executables = parseGameExecutablesJson(executablesJson);
            const std::wstring json = serializeGameExecutables(
                core().executables().saveProjectExecutables(
                    std::filesystem::path(configPath),
                    executables));
            logOperation(
                fluxora::LogLevel::Info,
                "Executables",
                std::string("Save executable list completed. count=") + std::to_string(executables.size()));
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
            logBridge(fluxora::LogLevel::Info, "fluxora_launch_game_executable started.");
            logOperation(
                fluxora::LogLevel::Info,
                "Launch",
                std::string("Launch executable requested. configPath=\"") +
                    pathForLog(std::filesystem::path(configPath)) + "\", executableId=\"" +
                    textForLog(executableId) + "\"");
            const std::wstring json = serializeGameExecutableLaunch(
                core().virtualFileSystem().launchExecutable(
                    std::filesystem::path(configPath),
                    executableId));
            logOperation(fluxora::LogLevel::Info, "Launch", "Launch executable completed.");
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

            logBridge(fluxora::LogLevel::Info, "fluxora_delete_installed_mod started.");
            logOperation(
                fluxora::LogLevel::Info,
                "Mods",
                std::string("Delete installed mod requested. projectDirectory=\"") +
                    pathForLog(std::filesystem::path(projectDirectory)) + "\", modPath=\"" +
                    pathForLog(std::filesystem::path(modPath)) + "\"");
            core().mods().deleteInstalledMod(
                std::filesystem::path(projectDirectory),
                std::filesystem::path(modPath));
            logOperation(fluxora::LogLevel::Info, "Mods", "Delete installed mod completed.");
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

            logOperation(
                fluxora::LogLevel::Info,
                "Mods",
                std::string("Set installed mod state requested. projectDirectory=\"") +
                    pathForLog(std::filesystem::path(projectDirectory)) + "\", modPath=\"" +
                    pathForLog(std::filesystem::path(modPath)) + "\", enabled=" +
                    (isEnabled != 0 ? "true" : "false"));
            core().mods().setInstalledModEnabled(
                std::filesystem::path(projectDirectory),
                std::filesystem::path(modPath),
                isEnabled != 0);
            logOperation(fluxora::LogLevel::Info, "Mods", "Set installed mod state completed.");
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

            logOperation(
                fluxora::LogLevel::Info,
                "Mods",
                std::string("Set all installed mods state requested. projectDirectory=\"") +
                    pathForLog(std::filesystem::path(projectDirectory)) + "\", enabled=" +
                    (isEnabled != 0 ? "true" : "false"));
            core().mods().setAllInstalledModsEnabled(
                std::filesystem::path(projectDirectory),
                isEnabled != 0);
            logOperation(fluxora::LogLevel::Info, "Mods", "Set all installed mods state completed.");
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

            const fluxora::PluginRuleContext rules = resolvePluginRuleContextForTemplate(templateId);
            const std::wstring json = serializePlugins(core().plugins().listPlugins(
                std::filesystem::path(projectDirectory),
                rules,
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

            const fluxora::PluginRuleContext rules = resolvePluginRuleContextForTemplate(templateId);
            const std::wstring json = serializePlugins(core().plugins().movePlugin(
                std::filesystem::path(projectDirectory),
                rules,
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

            const fluxora::PluginRuleContext rules = resolvePluginRuleContextForTemplate(templateId);
            const std::wstring json = serializePlugins(core().plugins().createPluginSeparator(
                std::filesystem::path(projectDirectory),
                rules,
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

            const fluxora::PluginRuleContext rules = resolvePluginRuleContextForTemplate(templateId);
            const std::wstring json = serializePlugins(core().plugins().deletePluginSeparator(
                std::filesystem::path(projectDirectory),
                rules,
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

            const fluxora::PluginRuleContext rules = resolvePluginRuleContextForTemplate(templateId);
            const std::wstring json = serializePlugins(core().plugins().setPluginEnabled(
                std::filesystem::path(projectDirectory),
                rules,
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

            logBridge(fluxora::LogLevel::Info, "fluxora_delete_download started.");
            logOperation(
                fluxora::LogLevel::Info,
                "Downloads",
                std::string("Delete download requested. projectDirectory=\"") +
                    pathForLog(std::filesystem::path(projectDirectory)) + "\", downloadPath=\"" +
                    pathForLog(std::filesystem::path(downloadPath)) + "\"");
            core().downloads().deleteDownload(
                std::filesystem::path(projectDirectory),
                std::filesystem::path(downloadPath));
            logOperation(fluxora::LogLevel::Info, "Downloads", "Delete download completed.");
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

            logOperation(
                fluxora::LogLevel::Info,
                "Downloads",
                std::string("Cancel download requested. projectDirectory=\"") +
                    pathForLog(std::filesystem::path(projectDirectory)) + "\", downloadPath=\"" +
                    pathForLog(std::filesystem::path(downloadPath)) + "\"");
            core().downloads().cancelDownload(
                std::filesystem::path(projectDirectory),
                std::filesystem::path(downloadPath));
            logOperation(fluxora::LogLevel::Info, "Downloads", "Cancel download completed.");
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

            logOperation(
                fluxora::LogLevel::Info,
                "Downloads",
                std::string("Resume download requested. projectDirectory=\"") +
                    pathForLog(std::filesystem::path(projectDirectory)) + "\", downloadPath=\"" +
                    pathForLog(std::filesystem::path(downloadPath)) + "\"");
            const fluxora::DownloadEntry download = core().downloads().resumeDownload(
                std::filesystem::path(projectDirectory),
                std::filesystem::path(downloadPath));
            logOperation(fluxora::LogLevel::Info, "Downloads", "Resume download completed.");
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

            logOperation(
                fluxora::LogLevel::Info,
                "Downloads",
                std::string("Import inbound downloads requested. projectDirectory=\"") +
                    pathForLog(std::filesystem::path(projectDirectory)) + "\"");
            const std::wstring json = serializeDownloads(
                core().downloads().importInboundNxmLinks(std::filesystem::path(projectDirectory)));
            logOperation(fluxora::LogLevel::Info, "Downloads", "Import inbound downloads completed.");
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

            logBridge(fluxora::LogLevel::Info, "fluxora_import_download_file started.");
            logOperation(
                fluxora::LogLevel::Info,
                "Downloads",
                std::string("Import download file requested. projectDirectory=\"") +
                    pathForLog(std::filesystem::path(projectDirectory)) + "\", sourcePath=\"" +
                    pathForLog(std::filesystem::path(sourcePath)) + "\"");
            const std::wstring json = serializeDownload(
                core().downloads().importLocalFile(
                    std::filesystem::path(projectDirectory),
                    std::filesystem::path(sourcePath)));
            logOperation(fluxora::LogLevel::Info, "Downloads", "Import download file completed.");
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
        return installDownloadWithMode(
            projectDirectory,
            downloadPath,
            modName,
            0,
            jsonBuffer,
            jsonBufferLength);
    }

    int fluxora_install_download_with_mode(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath,
        const wchar_t* modName,
        int existingModMode,
        wchar_t* jsonBuffer,
        int jsonBufferLength)
    {
        return installDownloadWithMode(
            projectDirectory,
            downloadPath,
            modName,
            existingModMode,
            jsonBuffer,
            jsonBufferLength);
    }

    int fluxora_analyze_download_content_layout(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath,
        int existingModMode,
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

            fluxora::ExistingModInstallMode mode = fluxora::ExistingModInstallMode::FailIfExists;
            if (!tryParseExistingModInstallMode(existingModMode, mode))
            {
                lastError = L"Existing mod install mode is invalid.";
                return FluxoraCoreResultInvalidArgument;
            }

            logBridge(fluxora::LogLevel::Info, "fluxora_analyze_download_content_layout started.");
            logOperation(
                fluxora::LogLevel::Info,
                "Downloads",
                std::string("Analyze download content layout requested. projectDirectory=\"") +
                    pathForLog(std::filesystem::path(projectDirectory)) + "\", downloadPath=\"" +
                    pathForLog(std::filesystem::path(downloadPath)) + "\", existingModMode=\"" +
                    existingModInstallModeForLog(mode) + "\"");
            const std::wstring json = serializePlacementPlan(
                core().downloads().analyzeDownloadContentLayout(
                    std::filesystem::path(projectDirectory),
                    std::filesystem::path(downloadPath),
                    mode));
            logOperation(fluxora::LogLevel::Info, "Downloads", "Analyze download content layout completed.");
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_analyze_fomod_download(
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

            logBridge(fluxora::LogLevel::Info, "fluxora_analyze_fomod_download started.");
            logOperation(
                fluxora::LogLevel::Info,
                "Downloads",
                std::string("Analyze FOMOD download requested. projectDirectory=\"") +
                    pathForLog(std::filesystem::path(projectDirectory)) + "\", downloadPath=\"" +
                    pathForLog(std::filesystem::path(downloadPath)) + "\"");
            const std::wstring json = serializeFomodInstaller(
                core().downloads().analyzeFomodDownload(
                    std::filesystem::path(projectDirectory),
                    std::filesystem::path(downloadPath)));
            logOperation(fluxora::LogLevel::Info, "Downloads", "Analyze FOMOD download completed.");
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_analyze_fomod_download_content_layout(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath,
        int existingModMode,
        const wchar_t* selectedOptionIdsJson,
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

            fluxora::ExistingModInstallMode mode = fluxora::ExistingModInstallMode::FailIfExists;
            if (!tryParseExistingModInstallMode(existingModMode, mode))
            {
                lastError = L"Existing mod install mode is invalid.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::vector<std::wstring> selectedOptionIds = parseStringArrayJson(selectedOptionIdsJson);
            logBridge(fluxora::LogLevel::Info, "fluxora_analyze_fomod_download_content_layout started.");
            logOperation(
                fluxora::LogLevel::Info,
                "Downloads",
                std::string("Analyze FOMOD content layout requested. projectDirectory=\"") +
                    pathForLog(std::filesystem::path(projectDirectory)) + "\", downloadPath=\"" +
                    pathForLog(std::filesystem::path(downloadPath)) + "\", existingModMode=\"" +
                    existingModInstallModeForLog(mode) + "\", selectedOptionCount=" +
                    std::to_string(selectedOptionIds.size()));
            const std::wstring json = serializePlacementPlan(
                core().downloads().analyzeFomodDownloadContentLayout(
                    std::filesystem::path(projectDirectory),
                    std::filesystem::path(downloadPath),
                    mode,
                    selectedOptionIds));
            logOperation(fluxora::LogLevel::Info, "Downloads", "Analyze FOMOD content layout completed.");
            return writeToBuffer(json, jsonBuffer, jsonBufferLength);
        }
        catch (const std::exception& exception)
        {
            return mapException(exception);
        }
    }

    int fluxora_install_fomod_download_with_mode(
        const wchar_t* projectDirectory,
        const wchar_t* downloadPath,
        const wchar_t* modName,
        int existingModMode,
        const wchar_t* selectedOptionIdsJson,
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

            fluxora::ExistingModInstallMode mode = fluxora::ExistingModInstallMode::FailIfExists;
            if (!tryParseExistingModInstallMode(existingModMode, mode))
            {
                lastError = L"Existing mod install mode is invalid.";
                return FluxoraCoreResultInvalidArgument;
            }

            const std::vector<std::wstring> selectedOptionIds = parseStringArrayJson(selectedOptionIdsJson);
            logBridge(fluxora::LogLevel::Info, "fluxora_install_fomod_download started.");
            logOperation(
                fluxora::LogLevel::Info,
                "Downloads",
                std::string("Install FOMOD download requested. projectDirectory=\"") +
                    pathForLog(std::filesystem::path(projectDirectory)) + "\", downloadPath=\"" +
                    pathForLog(std::filesystem::path(downloadPath)) + "\", modName=\"" +
                    textForLog(modName) + "\", existingModMode=\"" +
                    existingModInstallModeForLog(mode) + "\", selectedOptionCount=" +
                    std::to_string(selectedOptionIds.size()));
            const std::wstring json = serializeInstalledMod(
                core().downloads().installFomodDownload(
                    std::filesystem::path(projectDirectory),
                    std::filesystem::path(downloadPath),
                    modName,
                    mode,
                    selectedOptionIds));
            logOperation(fluxora::LogLevel::Info, "Downloads", "Install FOMOD download completed.");
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
