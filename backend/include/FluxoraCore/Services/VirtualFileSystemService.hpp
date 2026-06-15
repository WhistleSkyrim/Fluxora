#pragma once

#include "FluxoraCore/Services/ExecutableService.hpp"
#include "FluxoraCore/Services/IService.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace fluxora
{
    class Logger;
    class ProfileOrderService;
    class BuildPathSettingsService;

    // Launches a game executable behind a user-mode virtual file system, the same
    // idea Mod Organizer 2 uses: the mods are never copied into the game folder.
    // Instead this service describes the merged "virtual data directory" (every
    // enabled mod, in load order, plus the writable overwrite overlay), then
    // starts the game with FluxoraVfs.dll injected. The injected DLL hooks the
    // file system so Windows reports the mod files as if they lived in the game
    // directory, while they physically stay in the instance "mods" folder.
    class VirtualFileSystemService final : public IService
    {
    public:
        VirtualFileSystemService(
            Logger& logger,
            ExecutableService& executables,
            ProfileOrderService& profileOrder,
            const BuildPathSettingsService& pathSettings) noexcept;

        void initialize() override;
        void shutdown() override;

        // Builds the virtual data directory for the build's active profile and
        // launches the requested executable inside it. Falls back to a plain
        // launch when there is nothing to virtualize or the hook is unavailable.
        [[nodiscard]] GameExecutableLaunchResult launchExecutable(
            const std::filesystem::path& configPath,
            std::wstring_view executableId) const;

        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        Logger& logger_;
        ExecutableService& executables_;
        ProfileOrderService& profileOrder_;
        const BuildPathSettingsService& pathSettings_;
        bool initialized_{false};
    };
}
