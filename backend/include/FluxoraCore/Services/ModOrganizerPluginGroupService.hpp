#pragma once

#include "FluxoraCore/Services/TemplateService.hpp"
#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"

#include <filesystem>
#include <vector>

namespace fluxora
{
    class ModOrganizerPluginGroupService final
    {
    public:
        ModOrganizerPluginGroupService() = delete;

        [[nodiscard]] static std::vector<ProfilePluginOrderImportItemRecord> read(
            const std::filesystem::path& profileDirectory,
            const BuildTemplate& resolvedTemplate);
    };
}
