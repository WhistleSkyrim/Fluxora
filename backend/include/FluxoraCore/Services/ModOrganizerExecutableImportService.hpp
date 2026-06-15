#pragma once

#include "FluxoraCore/Services/ExecutableService.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fluxora
{
    struct ModOrganizerExecutableCopyRoot
    {
        std::filesystem::path sourceDirectory;
        std::filesystem::path destinationDirectory;
        std::optional<std::filesystem::path> onlyFile;
    };

    struct ModOrganizerExecutableImportContext
    {
        std::filesystem::path sourceDirectory;
        std::filesystem::path modsDirectory;
        std::filesystem::path profilesDirectory;
        std::filesystem::path downloadsDirectory;
        std::filesystem::path overwriteDirectory;
        std::filesystem::path gamePath;
        std::filesystem::path targetProjectDirectory;
        std::wstring templateId;
        std::wstring scriptExtenderLoaderExecutable;
    };

    struct ModOrganizerExecutableImportPlan
    {
        std::vector<GameExecutable> executables;
        std::vector<ModOrganizerExecutableCopyRoot> copyRoots;
        std::uintmax_t totalCopyBytes{0};
    };

    class ModOrganizerExecutableImportService final
    {
    public:
        [[nodiscard]] static ModOrganizerExecutableImportPlan createPlan(
            const std::map<std::wstring, std::wstring>& organizerIni,
            const ModOrganizerExecutableImportContext& context);
    };
}
