#pragma once

#include "FluxoraCore/GameSupport/GameDetectionService.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fluxora
{
    struct GameHealthFinding
    {
        HealthSeverity severity{HealthSeverity::Info};
        std::wstring code;
        std::wstring message;
        std::filesystem::path path;
        bool critical{false};
    };

    struct GameHealthCheckRequest
    {
        const IGameSupport* support{nullptr};
        const GameDefinition* definition{nullptr};
        std::filesystem::path installPath;
    };

    struct GameHealthCheckResult
    {
        GameId gameId;
        std::wstring displayName;
        HealthStatus status{HealthStatus::Unknown};
        std::vector<GameHealthFinding> findings;
        std::vector<std::wstring> matchedFiles;
        std::vector<std::wstring> missingFiles;
        std::vector<std::wstring> warnings;
        std::wstring summary;

        [[nodiscard]] bool hasBlockers() const noexcept;
        [[nodiscard]] bool allowsAutomation() const noexcept;
    };

    class GameHealthCheckService final
    {
    public:
        [[nodiscard]] GameHealthCheckResult check(const GameHealthCheckRequest& request) const;
        [[nodiscard]] GameHealthCheckResult check(const GameDetectionResult& detection) const;

        [[nodiscard]] static std::wstring healthStatusName(HealthStatus status);
        [[nodiscard]] static std::wstring healthSeverityName(HealthSeverity severity);
    };
}
