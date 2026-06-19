#pragma once

#include "FluxoraCore/Services/IService.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fluxora
{
    class BuildPathSettingsService;
    class DownloadService;
    class Logger;
    class ProjectService;

    struct FluxPackExportRequest
    {
        std::filesystem::path configPath;
        std::filesystem::path outputPath;
        bool includeGeneratedAssets{false};
    };

    struct FluxPackSummary
    {
        std::filesystem::path outputPath;
        std::wstring buildName;
        int formatVersion{1};
        std::uintmax_t manifestBytes{0};
        std::uintmax_t sourceArchiveCount{0};
        std::uintmax_t generatedAssetCount{0};
        std::uintmax_t customPatchCount{0};
        std::uintmax_t customConfigCount{0};
        std::uintmax_t installStepCount{0};
        bool generatedAssetsIncluded{false};
        bool installPlanAvailable{false};
    };

    struct FluxPackProviderProgress
    {
        std::wstring providerId;
        std::wstring displayName;
        std::uintmax_t totalCount{0};
        std::uintmax_t completedCount{0};
        std::uintmax_t pendingCount{0};
        std::uintmax_t failedCount{0};
        std::wstring currentItem;
        std::wstring statusText;
        int progressPercent{0};
    };

    struct FluxPackInstallProgress
    {
        std::wstring phase;
        std::wstring currentStep;
        std::wstring currentItem;
        std::wstring statusMessage;
        int overallPercent{0};
        std::uintmax_t totalSourceCount{0};
        std::uintmax_t installedSourceCount{0};
        std::uintmax_t pendingSourceCount{0};
        std::uintmax_t failedSourceCount{0};
        std::vector<FluxPackProviderProgress> providers;
    };

    struct FluxPackInstallRequest
    {
        std::filesystem::path fluxPackPath;
        std::filesystem::path installRootDirectory;
        std::function<void(const FluxPackInstallProgress&)> progress;
    };

    struct FluxPackInstallResult
    {
        FluxPackSummary summary;
        std::filesystem::path configPath;
        std::filesystem::path projectDirectory;
        std::wstring buildName;
        std::uintmax_t totalSourceCount{0};
        std::uintmax_t installedSourceCount{0};
        std::uintmax_t pendingSourceCount{0};
        std::uintmax_t failedSourceCount{0};
        std::uintmax_t appliedConfigCount{0};
        std::uintmax_t appliedProfileOrderItemCount{0};
        bool hasWarnings{false};
    };

    class FluxPackService final : public IService
    {
    public:
        FluxPackService(
            Logger& logger,
            ProjectService& projects,
            DownloadService& downloads,
            const BuildPathSettingsService& pathSettings) noexcept;

        void initialize() override;
        void shutdown() override;

        [[nodiscard]] FluxPackSummary exportProject(const FluxPackExportRequest& request) const;
        [[nodiscard]] FluxPackSummary inspectFluxPack(const std::filesystem::path& fluxPackPath) const;
        [[nodiscard]] FluxPackInstallResult installFluxPack(const FluxPackInstallRequest& request) const;
        [[nodiscard]] bool isInitialized() const noexcept;

    private:
        Logger& logger_;
        ProjectService& projects_;
        DownloadService& downloads_;
        const BuildPathSettingsService& pathSettings_;
        bool initialized_{false};
    };
}
