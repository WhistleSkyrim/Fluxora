#pragma once

#include "FluxoraCore/Services/IService.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fluxora
{
    class AppSettingsService;
    class BuildPathSettingsService;
    class Logger;

    struct DownloadEntry
    {
        std::wstring id;
        std::wstring name;
        std::wstring fileName;
        std::filesystem::path localPath;
        std::wstring source;
        std::wstring status;
        std::wstring sizeText;
        std::wstring createdAtText;
        int progressPercent{0};
        std::wstring progressText;
        std::wstring etaText;
        std::wstring downloadSpeedText;
        bool isDownloading{false};
        bool hasKnownProgress{false};
        bool canResume{false};
        bool canInstall{false};
        bool canDelete{true};
    };

    struct InstalledMod
    {
        std::filesystem::path id;
        std::wstring name;
        std::wstring version;
        bool isEnabled{true};
    };

    class DownloadService final : public IService
    {
    public:
        DownloadService(
            Logger& logger,
            AppSettingsService& settings,
            const BuildPathSettingsService& pathSettings) noexcept;

        void initialize() override;
        void shutdown() override;

        void registerNxmProtocol(const std::filesystem::path& executablePath) const;
        [[nodiscard]] bool isNxmProtocolRegistered(const std::filesystem::path& executablePath) const;

        [[nodiscard]] std::vector<DownloadEntry> listDownloads(
            const std::filesystem::path& projectDirectory) const;

        std::vector<DownloadEntry> captureNxmLinks(
            const std::filesystem::path& projectDirectory,
            const std::vector<std::wstring>& nxmLinks) const;

        std::vector<DownloadEntry> queueInboundNxmLinks(
            const std::vector<std::wstring>& nxmLinks) const;

        std::vector<DownloadEntry> importInboundNxmLinks(
            const std::filesystem::path& projectDirectory) const;

        DownloadEntry importLocalFile(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& sourcePath) const;

        void deleteDownload(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& downloadPath) const;

        void cancelDownload(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& downloadPath) const;

        DownloadEntry resumeDownload(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& downloadPath) const;

        InstalledMod installDownload(
            const std::filesystem::path& projectDirectory,
            const std::filesystem::path& downloadPath,
            std::wstring_view modName) const;

        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        [[nodiscard]] std::filesystem::path inboundDirectory() const;

        Logger& logger_;
        AppSettingsService& settings_;
        const BuildPathSettingsService& pathSettings_;
        bool initialized_{false};
    };
}
