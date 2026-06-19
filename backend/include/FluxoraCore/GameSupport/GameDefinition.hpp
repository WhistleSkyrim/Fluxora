#pragma once

#include "FluxoraCore/GameSupport/GameTypes.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fluxora
{
    enum class GameExecutableRole
    {
        Primary,
        Launcher,
        ScriptExtender
    };

    struct GameExecutableDefinition
    {
        std::wstring id;
        std::wstring displayName;
        ExecutableName name;
        GameExecutableRole role{GameExecutableRole::Primary};
        std::optional<GameExecutableWorkingDirectoryKind> workingDirectory;
    };

    struct GameExecutableRoles
    {
        std::optional<ExecutableName> primary;
        std::optional<ExecutableName> launcher;
        std::optional<ExecutableName> scriptExtender;
    };

    struct GameDetectionHints
    {
        std::vector<ExecutableName> executableNames;
        std::vector<std::wstring> folderNames;
        std::vector<std::wstring> domains;
    };

    struct GamePluginRules
    {
        std::vector<std::wstring> profileFiles;
        std::vector<std::wstring> basePlugins;
    };

    struct GameContentLayoutRules
    {
        std::wstring dataFolder;
        bool supportsRootFiles{false};
        std::wstring rootFileWrapperDirectory;
    };

    struct GameVfsRules
    {
        bool supportsRootBuilder{false};
        std::wstring rootBuilderDirectoryName;
        std::wstring userSettingsDirectoryName;
        std::vector<std::wstring> profileIniFileNames;
        std::vector<std::wstring> saveDirectoryNames;
        std::vector<std::wstring> excludedLaunchCacheDirectories;
    };

    struct GameScriptExtenderRules
    {
        std::wstring name;
        ExecutableName loaderExecutable;
        std::wstring website;
        std::vector<ExecutableName> expectedChildProcessNames;
        std::wstring handoffDisplayName;
        std::uint32_t handoffTimeoutMs{0};
        LaunchTrackingKind launchTrackingKind{LaunchTrackingKind::DirectProcess};
    };

    struct GameLaunchRules
    {
        std::optional<GameScriptExtenderRules> scriptExtender;
    };

    struct GameHealthRules
    {
        std::vector<std::wstring> requiredFiles;
    };

    struct GameDefinition
    {
        std::wstring schemaVersion;
        std::wstring definitionVersion;
        GameId id;
        std::wstring displayName;
        std::wstring summary;
        std::vector<std::wstring> aliases;
        std::vector<std::wstring> domains;
        std::vector<std::wstring> installFolderAliases;
        std::wstring defaultProfileName;
        std::wstring dataFolder;
        std::vector<std::wstring> requiredFiles;
        std::vector<GameExecutableDefinition> executables;
        GameExecutableRoles executableRoles;
        std::vector<NormalizedExtension> archiveExtensions;
        std::vector<NormalizedExtension> pluginExtensions;
        CapabilitySet capabilities;
        UiTemplateId uiTemplateId;
        GameDetectionHints detectionHints;
        GamePluginRules pluginRules;
        GameContentLayoutRules contentLayoutRules;
        GameVfsRules vfsRules;
        GameLaunchRules launchRules;
        GameHealthRules healthRules;
    };
}
