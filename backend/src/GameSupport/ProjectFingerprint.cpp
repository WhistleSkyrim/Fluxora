#include "FluxoraCore/GameSupport/ProjectFingerprint.hpp"

#include "FluxoraCore/Support/JsonWriter.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <system_error>

namespace fluxora
{
    namespace
    {
        constexpr std::wstring_view fingerprintAlgorithm = L"fluxora-project-v1";
        constexpr std::wstring_view definitionBundleVersion = L"embedded-1";

        [[nodiscard]] std::filesystem::path canonicalOrAbsolute(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }

            std::error_code error;
            std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
            if (!error && !resolved.empty())
            {
                return resolved.lexically_normal();
            }

            error.clear();
            resolved = std::filesystem::absolute(path, error);
            return error ? path.lexically_normal() : resolved.lexically_normal();
        }

        [[nodiscard]] std::wstring utcTimestamp()
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t time = std::chrono::system_clock::to_time_t(now);
            std::tm utc{};
#ifdef _WIN32
            gmtime_s(&utc, &time);
#else
            gmtime_r(&time, &utc);
#endif

            std::wostringstream out;
            out << std::put_time(&utc, L"%Y-%m-%dT%H:%M:%SZ");
            return out.str();
        }

        [[nodiscard]] std::wstring storeSourceName(DetectionSource source)
        {
            switch (source)
            {
            case DetectionSource::SteamHint:
            case DetectionSource::GogHint:
            case DetectionSource::EpicHint:
            case DetectionSource::ManualPath:
                return GameDetectionService::detectionSourceName(source);
            default:
                return GameDetectionService::detectionSourceName(source);
            }
        }
    }

    ProjectFingerprint createProjectFingerprint(
        const GameDetectionResult& detection,
        const GameHealthCheckResult& health,
        const std::filesystem::path& selectedInstallPath,
        const std::optional<std::filesystem::path>& selectedExecutable)
    {
        ProjectFingerprint fingerprint;
        fingerprint.gameId = detection.gameId.value();
        fingerprint.gameDisplayName = detection.displayName;
        if (detection.definition != nullptr)
        {
            fingerprint.gameDefinitionVersion = detection.definition->definitionVersion;
            fingerprint.supportModuleVersion = detection.definition->definitionVersion;
        }
        fingerprint.definitionBundleVersion = std::wstring(definitionBundleVersion);
        fingerprint.selectedInstallPath = selectedInstallPath.empty()
            ? detection.selectedInstallPath
            : selectedInstallPath;
        fingerprint.canonicalInstallPath = detection.canonicalInstallPath.empty()
            ? canonicalOrAbsolute(fingerprint.selectedInstallPath)
            : detection.canonicalInstallPath;
        if (selectedExecutable.has_value())
        {
            fingerprint.selectedExecutable = selectedExecutable.value();
        }
        else
        {
            fingerprint.selectedExecutable = detection.selectedExecutable;
        }
        fingerprint.detectedStoreSource = storeSourceName(detection.source);
        fingerprint.detectionSource = GameDetectionService::detectionSourceName(detection.source);
        fingerprint.detectionConfidence = GameDetectionService::detectionConfidenceName(detection.confidence);
        fingerprint.healthStatusAtCreation = GameHealthCheckService::healthStatusName(health.status);
        fingerprint.gameVersion = {};
        fingerprint.timestamp = utcTimestamp();
        return fingerprint;
    }

    void writeProjectFingerprint(JsonWriter& writer, const ProjectFingerprint& fingerprint)
    {
        writer.beginObject();
        writer.field(L"algorithm", fingerprintAlgorithm);
        writer.field(L"gameId", fingerprint.gameId);
        writer.field(L"gameDisplayName", fingerprint.gameDisplayName);
        writer.field(L"gameDefinitionVersion", fingerprint.gameDefinitionVersion);
        writer.field(L"definitionBundleVersion", fingerprint.definitionBundleVersion);
        writer.field(L"supportModuleVersion", fingerprint.supportModuleVersion);
        writer.field(L"selectedInstallPath", fingerprint.selectedInstallPath.wstring());
        writer.field(L"canonicalInstallPath", fingerprint.canonicalInstallPath.wstring());
        writer.field(L"selectedExecutable", fingerprint.selectedExecutable.wstring());
        writer.field(L"detectedStoreSource", fingerprint.detectedStoreSource);
        writer.field(L"detectionSource", fingerprint.detectionSource);
        writer.field(L"detectionConfidence", fingerprint.detectionConfidence);
        writer.field(L"healthStatusAtCreation", fingerprint.healthStatusAtCreation);
        writer.field(L"gameVersion", fingerprint.gameVersion);
        writer.field(L"timestamp", fingerprint.timestamp);
        writer.endObject();
    }
}
