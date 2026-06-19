#pragma once

#include "FluxoraCore/GameSupport/IGameSupport.hpp"

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fluxora
{
    class Logger;

    enum class ContentLayoutInstallMode
    {
        Standard,
        Replace,
        Merge
    };

    enum class ContentLayoutClassification
    {
        GameData,
        GameRoot,
        Plugin,
        Archive,
        ScriptExtender,
        Config,
        Ini,
        Save,
        ToolExecutable,
        Documentation,
        Screenshots,
        Unknown,
        Unsafe
    };

    struct ContentLayoutArchiveEntry
    {
        std::filesystem::path relativePath;
        bool isDirectory{false};
    };

    struct ValidationFinding
    {
        HealthSeverity severity{HealthSeverity::Info};
        std::optional<GameRelativePath> path;
        ContentLayoutClassification classification{ContentLayoutClassification::Unknown};
        std::wstring message;
        bool blocksInstall{false};
    };

    struct UserExplanation
    {
        std::wstring summary;
        std::vector<std::wstring> details;
    };

    struct ManualOverrideOption
    {
        GameRelativePath sourcePath;
        std::vector<PlacementTarget> safeTargets;
        std::wstring reason;
    };

    struct PlacementPlanEntry
    {
        GameRelativePath sourcePath;
        PlacementTarget target{PlacementTarget::Blocked};
        ContentArea contentArea{ContentArea::Data};
        GameRelativePath targetRelativePath;
        ContentLayoutClassification classification{ContentLayoutClassification::Unknown};
        std::wstring explanation;
        bool manualOverrideAllowed{false};
        std::vector<PlacementTarget> safeManualTargets;
    };

    struct ContentLayoutSummary
    {
        bool supported{false};
        bool hasBlockers{false};
        bool hasWarnings{false};
        std::size_t totalEntries{0};
        std::size_t plannedEntries{0};
        std::size_t gameDataEntries{0};
        std::size_t gameRootEntries{0};
        std::size_t pluginEntries{0};
        std::size_t archiveEntries{0};
        std::size_t scriptExtenderEntries{0};
        std::size_t unknownEntries{0};
        std::size_t unsafeEntries{0};
    };

    struct PlacementPlan
    {
        GameId gameId;
        std::wstring gameDisplayName;
        std::wstring rootFileWrapperDirectory;
        ContentLayoutSummary summary;
        std::vector<PlacementPlanEntry> entries;
        std::vector<ValidationFinding> validationFindings;
        UserExplanation userExplanation;
        std::vector<ManualOverrideOption> manualOverrideOptions;

        [[nodiscard]] bool canInstall() const noexcept
        {
            return summary.supported && !summary.hasBlockers;
        }
    };

    struct ContentLayoutAnalysisRequest
    {
        GameId selectedGameId;
        std::wstring selectedGameDisplayName;
        CapabilitySet selectedGameCapabilities;
        const IContentLayoutRulesProvider* rulesProvider{nullptr};
        std::vector<ContentLayoutArchiveEntry> archiveFileTree;
        ContentLayoutInstallMode installMode{ContentLayoutInstallMode::Standard};
        std::optional<std::filesystem::path> userSelectedSubfolder;
        bool hasFomodOutput{false};
        std::wstring archiveContentHash;
        std::wstring gameDefinitionVersion;
        const std::atomic_bool* cancellationRequested{nullptr};
        const Logger* logger{nullptr};
    };

    class ContentLayoutService final
    {
    public:
        [[nodiscard]] PlacementPlan analyze(const ContentLayoutAnalysisRequest& request) const;

        [[nodiscard]] PlacementPlan analyzeDirectory(
            const std::filesystem::path& directory,
            const ContentLayoutAnalysisRequest& request) const;

        void applyPlanToDirectory(
            const std::filesystem::path& directory,
            const PlacementPlan& plan) const;
    };
}
