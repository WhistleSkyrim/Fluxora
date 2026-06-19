#pragma once

#include "FluxoraCore/GameSupport/DefinitionBackedGameSupport.hpp"

namespace fluxora
{
    class SkyrimSpecialEditionSupport final : public DefinitionBackedGameSupport
    {
    public:
        explicit SkyrimSpecialEditionSupport(const GameDefinition& definition);

        [[nodiscard]] static GameId gameId();
        [[nodiscard]] static bool supportsDefinition(const GameDefinition& definition) noexcept;

        [[nodiscard]] const PluginSupportRules& pluginRules() const noexcept override;
        [[nodiscard]] const ContentLayoutSupportRules& contentLayoutRules() const noexcept override;
        [[nodiscard]] const ManifestMigrationRules& manifestMigrationRules() const noexcept override;

    private:
        PluginSupportRules pluginRules_;
        ContentLayoutSupportRules contentLayoutRules_;
        ManifestMigrationRules manifestMigrationRules_;
    };
}
