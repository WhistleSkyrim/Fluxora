#pragma once

#include "FluxoraCore/GameSupport/GameHealthCheckService.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace fluxora
{
    struct ProjectFingerprint
    {
        std::wstring gameId;
        std::wstring gameDisplayName;
        std::wstring gameDefinitionVersion;
        std::wstring definitionBundleVersion;
        std::wstring supportModuleVersion;
        std::filesystem::path selectedInstallPath;
        std::filesystem::path canonicalInstallPath;
        std::filesystem::path selectedExecutable;
        std::wstring detectedStoreSource;
        std::wstring detectionSource;
        std::wstring detectionConfidence;
        std::wstring healthStatusAtCreation;
        std::wstring gameVersion;
        std::wstring timestamp;
    };

    class JsonWriter;

    [[nodiscard]] ProjectFingerprint createProjectFingerprint(
        const GameDetectionResult& detection,
        const GameHealthCheckResult& health,
        const std::filesystem::path& selectedInstallPath,
        const std::optional<std::filesystem::path>& selectedExecutable = std::nullopt);

    void writeProjectFingerprint(JsonWriter& writer, const ProjectFingerprint& fingerprint);
}
