#pragma once

#include "FluxoraCore/Services/IService.hpp"
#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fluxora
{
    class AppSettingsService;
    class BuildPathSettingsService;
    class Logger;

    struct ModDescriptor
    {
        std::string id;
        std::string name;
        std::string version;
        bool enabled{false};
    };

    struct InstalledModEntry
    {
        std::filesystem::path id;
        std::wstring name;
        std::wstring version;
        std::wstring latestVersion;
        std::wstring lastCheckedAt;
        std::wstring updateStatus;
        std::wstring conflictStatus;
        int fileCount{0};
        int conflictingFileCount{0};
        int overwrittenFileCount{0};
        int overwritingFileCount{0};
        bool isEnabled{true};
        bool canCheckUpdates{false};
        bool hasUpdate{false};
    };

    class ModService final : public IService
    {
    public:
        ModService(
            Logger& logger,
            AppSettingsService& settings,
            const BuildPathSettingsService& pathSettings) noexcept;

        void initialize() override;
        void shutdown() override;

        void registerMod(ModDescriptor descriptor);

        [[nodiscard]] const std::vector<ModDescriptor>& mods() const noexcept;
        [[nodiscard]] std::vector<InstalledModEntry> listInstalledMods(
            const std::filesystem::path& projectDirectory) const;
        [[nodiscard]] std::vector<InstalledModEntry> checkInstalledModUpdates(
            const std::filesystem::path& projectDirectory) const;
        [[nodiscard]] std::vector<ModFileTreeEntry> listModFileTree(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modPath,
            std::wstring_view relativeDirectory) const;
        void deleteInstalledMod(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modPath) const;
        void setInstalledModEnabled(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& modPath,
            bool isEnabled) const;
        void setAllInstalledModsEnabled(
            const std::filesystem::path& projectDirectory,
            bool isEnabled) const;
        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        Logger& logger_;
        AppSettingsService& settings_;
        const BuildPathSettingsService& pathSettings_;
        std::vector<ModDescriptor> mods_;
        bool initialized_{false};
    };
}
