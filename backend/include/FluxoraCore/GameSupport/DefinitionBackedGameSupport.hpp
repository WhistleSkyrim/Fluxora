#pragma once

#include "FluxoraCore/GameSupport/IGameSupport.hpp"

namespace fluxora
{
    class DefinitionBackedGameSupport
        : public IGameSupport,
          public IGameIdentityProvider,
          public IGameDetectionProvider,
          public IGameHealthProvider,
          public IPluginRulesProvider,
          public IExecutableRulesProvider,
          public ILaunchRulesProvider,
          public IVfsRulesProvider,
          public IContentLayoutRulesProvider,
          public IUiRulesProvider,
          public IManifestMigrationProvider
    {
    public:
        explicit DefinitionBackedGameSupport(const GameDefinition& definition);

        [[nodiscard]] const GameDefinition& definition() const noexcept;

        [[nodiscard]] const GameIdentityRules& identity() const noexcept override;
        [[nodiscard]] const CapabilitySet& capabilities() const noexcept override;
        [[nodiscard]] const GameSupportComponents& components() const noexcept override;

        [[nodiscard]] const GameIdentityRules& identityRules() const noexcept override;
        [[nodiscard]] const GameDetectionRules& detectionRules() const noexcept override;
        [[nodiscard]] const HealthSupportRules& healthRules() const noexcept override;
        [[nodiscard]] const PluginSupportRules& pluginRules() const noexcept override;
        [[nodiscard]] const ExecutableSupportRules& executableRules() const noexcept override;
        [[nodiscard]] const LaunchSupportRules& launchRules() const noexcept override;
        [[nodiscard]] const VfsSupportRules& vfsRules() const noexcept override;
        [[nodiscard]] const ContentLayoutSupportRules& contentLayoutRules() const noexcept override;
        [[nodiscard]] const UiSupportRules& uiRules() const noexcept override;
        [[nodiscard]] const ManifestMigrationRules& manifestMigrationRules() const noexcept override;

    private:
        const GameDefinition* definition_{nullptr};
        CompiledGameRules rules_;
        GameSupportComponents components_;
    };
}
