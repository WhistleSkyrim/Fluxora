#pragma once

#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fluxora
{
    struct ModOrganizerProfileOrder
    {
        std::vector<ProfileOrderImportItemRecord> items;
        std::map<std::wstring, bool> enabledByFolder;
    };

    class ModOrganizerProfileOrderService final
    {
    public:
        ModOrganizerProfileOrderService() = delete;

        [[nodiscard]] static ModOrganizerProfileOrder read(
            const std::filesystem::path& profileDirectory);
    };
}
