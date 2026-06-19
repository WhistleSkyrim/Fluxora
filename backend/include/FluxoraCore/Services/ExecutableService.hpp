#pragma once

#include "FluxoraCore/GameSupport/IGameSupport.hpp"
#include "FluxoraCore/Services/IService.hpp"

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fluxora
{
    class ExecutableIconService;
    class BuildPathSettingsService;
    class Logger;

    struct GameExecutable
    {
        std::wstring id;
        std::wstring displayName;
        std::wstring executablePath;
        std::wstring arguments;
        std::wstring workingDirectory;
        std::wstring iconPath;
    };

    struct GameExecutableLaunchResult
    {
        GameExecutable executable;
        std::filesystem::path resolvedExecutablePath;
        std::filesystem::path resolvedWorkingDirectory;
        LaunchTrackingKind launchTrackingKind{LaunchTrackingKind::DirectProcess};
        std::vector<std::wstring> expectedChildProcessNames;
        std::wstring handoffDisplayName;
        std::uint32_t handoffTimeoutMs{0};
        std::uint32_t processId{0};
    };

    // Everything needed to start an executable, resolved from the build config but
    // before the process is actually created. The virtual file system launcher
    // reuses this so path/working-directory/template resolution lives in one place.
    struct ResolvedExecutableLaunch
    {
        GameExecutable executable;
        std::filesystem::path resolvedExecutablePath;
        std::filesystem::path resolvedWorkingDirectory;
        std::wstring commandLine;
        std::filesystem::path gamePath;
        std::filesystem::path rootBuilderLaunchCacheDirectory;
        std::filesystem::path projectDirectory;
        GameId gameId;
        std::wstring gameDisplayName;
        std::wstring gameDefinitionVersion;
        CapabilitySet gameCapabilities;
        std::wstring templateId;
        std::wstring dataDirectory;
        std::wstring defaultProfile;
        std::optional<VfsSupportRules> vfsRules;
        std::optional<ContentLayoutSupportRules> contentLayoutRules;
        LaunchTrackingKind launchTrackingKind{LaunchTrackingKind::DirectProcess};
        std::vector<std::wstring> expectedChildProcessNames;
        std::wstring handoffDisplayName;
        std::uint32_t handoffTimeoutMs{0};
    };

    class ExecutableService final : public IService
    {
    public:
        ExecutableService(
            Logger& logger,
            ExecutableIconService& iconService,
            const BuildPathSettingsService& pathSettings) noexcept;

        void initialize() override;
        void shutdown() override;

        [[nodiscard]] std::vector<GameExecutable> listProjectExecutables(
            const std::filesystem::path& configPath) const;

        [[nodiscard]] std::vector<GameExecutable> saveProjectExecutables(
            const std::filesystem::path& configPath,
            const std::vector<GameExecutable>& executables) const;

        [[nodiscard]] GameExecutableLaunchResult launchProjectExecutable(
            const std::filesystem::path& configPath,
            std::wstring_view executableId) const;

        // Resolve (but do not launch) an executable from the build config.
        [[nodiscard]] ResolvedExecutableLaunch resolveExecutable(
            const std::filesystem::path& configPath,
            std::wstring_view executableId) const;

        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        Logger& logger_;
        ExecutableIconService& iconService_;
        const BuildPathSettingsService& pathSettings_;
        bool initialized_{false};
    };
}
