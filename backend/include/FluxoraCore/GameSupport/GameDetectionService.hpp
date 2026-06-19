#pragma once

#include "FluxoraCore/GameSupport/GameSupportRegistry.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fluxora
{
    enum class DetectionSource
    {
        None,
        ManualPath,
        SteamHint,
        GogHint,
        EpicHint,
        ExecutableName,
        InstallFolderAlias,
        NexusDomainHint,
        RequiredFiles,
        Unsupported,
        Ambiguous
    };

    struct GameDetectionStoreHint
    {
        DetectionSource source{DetectionSource::None};
        std::filesystem::path installPath;
        std::wstring label;
    };

    struct GameDetectionCandidate
    {
        GameId gameId;
        std::wstring displayName;
        DetectionSource source{DetectionSource::None};
        DetectionConfidence confidence{DetectionConfidence::None};
        std::filesystem::path selectedInstallPath;
        std::filesystem::path canonicalInstallPath;
        std::filesystem::path selectedExecutable;
        std::vector<std::wstring> matchedFiles;
        std::vector<std::wstring> missingFiles;
        std::vector<std::wstring> warnings;
        const IGameSupport* support{nullptr};
        const GameDefinition* definition{nullptr};
    };

    struct GameDetectionRequest
    {
        std::optional<GameId> manualGameId;
        std::filesystem::path installPath;
        std::vector<GameDetectionStoreHint> storeHints;
        std::vector<std::filesystem::path> executablePaths;
        std::vector<std::wstring> domainHints;
        std::vector<std::wstring> nameHints;
    };

    struct GameDetectionResult
    {
        bool detected{false};
        GameId gameId;
        std::wstring displayName;
        DetectionSource source{DetectionSource::None};
        DetectionConfidence confidence{DetectionConfidence::None};
        std::filesystem::path selectedInstallPath;
        std::filesystem::path canonicalInstallPath;
        std::filesystem::path selectedExecutable;
        std::vector<std::wstring> matchedFiles;
        std::vector<std::wstring> missingFiles;
        std::vector<std::wstring> warnings;
        std::vector<GameDetectionCandidate> ambiguousCandidates;
        const IGameSupport* support{nullptr};
        const GameDefinition* definition{nullptr};
    };

    class GameDetectionService final
    {
    public:
        explicit GameDetectionService(const GameSupportRegistry& registry) noexcept;

        [[nodiscard]] GameDetectionResult detect(const GameDetectionRequest& request) const;

        [[nodiscard]] static std::wstring detectionSourceName(DetectionSource source);
        [[nodiscard]] static std::wstring detectionConfidenceName(DetectionConfidence confidence);

    private:
        [[nodiscard]] GameDetectionResult unsupported(std::wstring warning) const;
        [[nodiscard]] GameDetectionResult fromCandidate(GameDetectionCandidate candidate) const;

        const GameSupportRegistry& registry_;
    };
}
