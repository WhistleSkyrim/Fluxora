#pragma once

#include "FluxoraCore/GameSupport/GameDefinition.hpp"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace fluxora
{
    struct GameIdentityRules
    {
        GameId id;
        std::wstring displayName;
        std::wstring summary;
        std::wstring definitionVersion;
        std::wstring defaultProfileName;
        UiTemplateId uiTemplateId;
        std::vector<std::wstring> aliases;
        std::vector<std::wstring> domains;
        std::vector<std::wstring> installFolderAliases;
    };

    struct GameDetectionRules
    {
        std::vector<ExecutableName> executableNames;
        std::vector<std::wstring> folderNames;
        std::vector<std::wstring> domains;
        std::vector<std::wstring> requiredFiles;
        std::set<std::wstring> executableNameKeys;
        std::set<std::wstring> folderNameKeys;
        std::set<std::wstring> domainKeys;
        std::set<std::wstring> requiredFileKeys;
    };

    struct PluginSupportRules
    {
        std::vector<NormalizedExtension> pluginExtensions;
        std::vector<std::wstring> profileFiles;
        std::vector<std::wstring> basePlugins;
        std::vector<std::filesystem::path> pluginSearchDirectories;
        std::vector<NormalizedExtension> masterPluginExtensions;
        std::vector<NormalizedExtension> lightPluginExtensions;
        std::wstring activePluginsFileName;
        std::wstring loadOrderFileName;
        std::wstring basePluginSourceLabel;
        std::wstring basePluginLockReason;
        std::set<std::wstring> pluginExtensionKeys;
        std::set<std::wstring> masterPluginExtensionKeys;
        std::set<std::wstring> lightPluginExtensionKeys;
        std::set<std::wstring> basePluginKeys;
        std::set<std::wstring> pluginSearchDirectoryKeys;
    };

    struct ExecutableSupportRules
    {
        std::vector<GameExecutableDefinition> executables;
        GameExecutableRoles roles;
    };

    struct LaunchSupportRules
    {
        GameLaunchRules rules;
    };

    struct VfsSupportRules
    {
        GameVfsRules rules;
    };

    struct ContentLayoutSupportRules
    {
        std::wstring dataFolder;
        bool supportsRootFiles{false};
        std::wstring rootFileWrapperDirectory;
        std::vector<NormalizedExtension> pluginExtensions;
        std::vector<NormalizedExtension> archiveExtensions;
        std::vector<ExecutableName> scriptExtenderLoaders;
        std::vector<std::wstring> gameDataDirectories;
        std::vector<std::filesystem::path> scriptExtenderDataPaths;
        std::set<std::wstring> pluginExtensionKeys;
        std::set<std::wstring> archiveExtensionKeys;
        std::set<std::wstring> scriptExtenderLoaderKeys;
        std::set<std::wstring> gameDataDirectoryKeys;
        std::set<std::wstring> scriptExtenderDataPathKeys;
    };

    struct UiSupportRules
    {
        UiTemplateId templateId;
        std::wstring displayName;
    };

    struct HealthSupportRules
    {
        std::vector<std::wstring> requiredFiles;
        std::set<std::wstring> requiredFileKeys;
    };

    struct ManifestMigrationRules
    {
        bool supportsAutomaticMigration{false};
    };

    struct CompiledGameRules
    {
        GameIdentityRules identity;
        CapabilitySet capabilities;
        GameDetectionRules detection;
        PluginSupportRules plugins;
        ExecutableSupportRules executables;
        LaunchSupportRules launch;
        VfsSupportRules vfs;
        ContentLayoutSupportRules contentLayout;
        UiSupportRules ui;
        HealthSupportRules health;
        ManifestMigrationRules manifestMigration;

        std::set<std::wstring> domainKeys;
        std::set<std::wstring> executableNameKeys;
        std::set<std::wstring> installFolderKeys;
    };

    class IGameIdentityProvider
    {
    public:
        virtual ~IGameIdentityProvider() = default;
        [[nodiscard]] virtual const GameIdentityRules& identityRules() const noexcept = 0;
    };

    class IGameDetectionProvider
    {
    public:
        virtual ~IGameDetectionProvider() = default;
        [[nodiscard]] virtual const GameDetectionRules& detectionRules() const noexcept = 0;
    };

    class IGameHealthProvider
    {
    public:
        virtual ~IGameHealthProvider() = default;
        [[nodiscard]] virtual const HealthSupportRules& healthRules() const noexcept = 0;
    };

    class IPluginRulesProvider
    {
    public:
        virtual ~IPluginRulesProvider() = default;
        [[nodiscard]] virtual const PluginSupportRules& pluginRules() const noexcept = 0;
    };

    class IExecutableRulesProvider
    {
    public:
        virtual ~IExecutableRulesProvider() = default;
        [[nodiscard]] virtual const ExecutableSupportRules& executableRules() const noexcept = 0;
    };

    class ILaunchRulesProvider
    {
    public:
        virtual ~ILaunchRulesProvider() = default;
        [[nodiscard]] virtual const LaunchSupportRules& launchRules() const noexcept = 0;
    };

    class IVfsRulesProvider
    {
    public:
        virtual ~IVfsRulesProvider() = default;
        [[nodiscard]] virtual const VfsSupportRules& vfsRules() const noexcept = 0;
    };

    class IContentLayoutRulesProvider
    {
    public:
        virtual ~IContentLayoutRulesProvider() = default;
        [[nodiscard]] virtual const ContentLayoutSupportRules& contentLayoutRules() const noexcept = 0;
    };

    class IUiRulesProvider
    {
    public:
        virtual ~IUiRulesProvider() = default;
        [[nodiscard]] virtual const UiSupportRules& uiRules() const noexcept = 0;
    };

    class IManifestMigrationProvider
    {
    public:
        virtual ~IManifestMigrationProvider() = default;
        [[nodiscard]] virtual const ManifestMigrationRules& manifestMigrationRules() const noexcept = 0;
    };

    struct GameSupportComponents
    {
        const IGameIdentityProvider* identityProvider{nullptr};
        const IGameDetectionProvider* detectionProvider{nullptr};
        const IGameHealthProvider* healthProvider{nullptr};
        const IPluginRulesProvider* pluginRulesProvider{nullptr};
        const IExecutableRulesProvider* executableRulesProvider{nullptr};
        const ILaunchRulesProvider* launchRulesProvider{nullptr};
        const IVfsRulesProvider* vfsRulesProvider{nullptr};
        const IContentLayoutRulesProvider* contentLayoutRulesProvider{nullptr};
        const IUiRulesProvider* uiRulesProvider{nullptr};
        const IManifestMigrationProvider* manifestMigrationProvider{nullptr};
    };

    class IGameSupport
    {
    public:
        virtual ~IGameSupport() = default;
        [[nodiscard]] virtual const GameIdentityRules& identity() const noexcept = 0;
        [[nodiscard]] virtual const CapabilitySet& capabilities() const noexcept = 0;
        [[nodiscard]] virtual const GameSupportComponents& components() const noexcept = 0;
    };
}
