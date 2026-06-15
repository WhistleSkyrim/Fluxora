#pragma once

#include "FluxoraCore/Services/IService.hpp"

#include <filesystem>

namespace fluxora
{
    class Logger;

    struct BuildPathSettings
    {
        std::filesystem::path gameDirectory;
        std::filesystem::path modsDirectory;
        std::filesystem::path profilesDirectory;
        std::filesystem::path downloadsDirectory;
        std::filesystem::path overwriteDirectory;
    };

    class BuildPathSettingsService final : public IService
    {
    public:
        explicit BuildPathSettingsService(Logger& logger) noexcept;

        void initialize() override;
        void shutdown() override;

        [[nodiscard]] BuildPathSettings loadForConfig(
            const std::filesystem::path& configPath) const;

        [[nodiscard]] BuildPathSettings saveForConfig(
            const std::filesystem::path& configPath,
            const BuildPathSettings& settings) const;

        [[nodiscard]] BuildPathSettings loadForProjectDirectory(
            const std::filesystem::path& projectDirectory) const;

        [[nodiscard]] std::filesystem::path modsDirectory(
            const std::filesystem::path& projectDirectory) const;

        [[nodiscard]] std::filesystem::path profilesDirectory(
            const std::filesystem::path& projectDirectory) const;

        [[nodiscard]] std::filesystem::path downloadsDirectory(
            const std::filesystem::path& projectDirectory) const;

        [[nodiscard]] std::filesystem::path overwriteDirectory(
            const std::filesystem::path& projectDirectory) const;

        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        Logger& logger_;
        bool initialized_{false};
    };
}
