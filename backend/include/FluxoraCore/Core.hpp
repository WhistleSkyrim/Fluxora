#pragma once

#include <memory>

namespace fluxora
{
    class HookService;
    class Logger;
    class ModService;
    class ModOrganizerImportService;
    class PluginService;
    class ProfileOrderService;
    class DownloadService;
    class ExecutableService;
    class ExecutableIconService;
    class VirtualFileSystemService;
    class AppSettingsService;
    class BuildPathSettingsService;
    class NexusModsAuthService;
    class ProjectService;
    class TemplateService;

    class Core final
    {
    public:
        Core();
        ~Core();

        Core(const Core&) = delete;
        Core& operator=(const Core&) = delete;

        void initialize();
        void shutdown();

        [[nodiscard]] bool isInitialized() const noexcept;

        [[nodiscard]] HookService& hooks() noexcept;
        [[nodiscard]] Logger& logger() noexcept;
        [[nodiscard]] ModService& mods() noexcept;
        [[nodiscard]] ModOrganizerImportService& modOrganizerImport() noexcept;
        [[nodiscard]] PluginService& plugins() noexcept;
        [[nodiscard]] ProfileOrderService& profileOrder() noexcept;
        [[nodiscard]] DownloadService& downloads() noexcept;
        [[nodiscard]] ExecutableService& executables() noexcept;
        [[nodiscard]] ExecutableIconService& executableIcons() noexcept;
        [[nodiscard]] VirtualFileSystemService& virtualFileSystem() noexcept;
        [[nodiscard]] NexusModsAuthService& nexusModsAuth() noexcept;
        [[nodiscard]] ProjectService& projects() noexcept;
        [[nodiscard]] TemplateService& templates() noexcept;
        [[nodiscard]] AppSettingsService& settings() noexcept;
        [[nodiscard]] BuildPathSettingsService& buildPathSettings() noexcept;

    private:
        std::unique_ptr<Logger> logger_;
        std::unique_ptr<AppSettingsService> settings_;
        std::unique_ptr<BuildPathSettingsService> buildPathSettings_;
        std::unique_ptr<HookService> hooks_;
        std::unique_ptr<ModService> mods_;
        std::unique_ptr<PluginService> plugins_;
        std::unique_ptr<ProfileOrderService> profileOrder_;
        std::unique_ptr<DownloadService> downloads_;
        std::unique_ptr<ExecutableIconService> executableIcons_;
        std::unique_ptr<ExecutableService> executables_;
        std::unique_ptr<NexusModsAuthService> nexusModsAuth_;
        std::unique_ptr<TemplateService> templates_;
        std::unique_ptr<ProjectService> projects_;
        std::unique_ptr<ModOrganizerImportService> modOrganizerImport_;
        std::unique_ptr<VirtualFileSystemService> virtualFileSystem_;
        bool initialized_{false};
    };
}
