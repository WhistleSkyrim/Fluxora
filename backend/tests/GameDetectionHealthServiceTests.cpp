#include "FluxoraCore/GameSupport/GameDetectionService.hpp"
#include "FluxoraCore/GameSupport/DefinitionBackedGameSupport.hpp"
#include "FluxoraCore/GameSupport/GameHealthCheckService.hpp"
#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace fluxora::tests
{
    namespace
    {
        void writeSkyrimRequiredFiles(const std::filesystem::path& game)
        {
            writeTextFile(game / L"SkyrimSE.exe", "MZ");
            writeTextFile(game / L"Data" / L"Skyrim.esm", "master");
        }

        void writeSkyrimOptionalExecutables(const std::filesystem::path& game)
        {
            writeTextFile(game / L"SkyrimSELauncher.exe", "MZ");
            writeTextFile(game / L"skse64_loader.exe", "MZ");
        }

        [[nodiscard]] bool contains(
            const std::vector<std::wstring>& values,
            std::wstring_view expected)
        {
            return std::find_if(
                values.begin(),
                values.end(),
                [expected](const std::wstring& value)
                {
                    return value == expected;
                }) != values.end();
        }

        [[nodiscard]] GameDetectionResult detectSkyrimManual(
            const GameSupportRegistry& registry,
            const std::filesystem::path& game)
        {
            GameDetectionService detection(registry);
            GameDetectionRequest request;
            request.manualGameId = GameId::parseOrThrow(L"skyrimse");
            request.installPath = game;
            return detection.detect(request);
        }

        [[nodiscard]] GameDefinition healthTestDefinition(
            std::wstring definitionVersion,
            std::vector<std::wstring> requiredFiles)
        {
            GameDefinition definition;
            definition.schemaVersion = L"1";
            definition.definitionVersion = std::move(definitionVersion);
            definition.id = GameId::parseOrThrow(L"health-test-game");
            definition.displayName = L"Health Test Game";
            definition.defaultProfileName = L"Default";
            definition.dataFolder = L"Data";
            definition.requiredFiles = requiredFiles;
            definition.executables.push_back(GameExecutableDefinition{
                L"game",
                L"Game",
                ExecutableName::parseOrThrow(L"Game.exe"),
                GameExecutableRole::Primary,
                GameExecutableWorkingDirectoryKind::GameRoot
            });
            definition.executableRoles.primary = ExecutableName::parseOrThrow(L"Game.exe");
            definition.uiTemplateId = UiTemplateId::parseOrThrow(L"health-test");
            definition.contentLayoutRules.dataFolder = L"Data";
            definition.healthRules.requiredFiles = std::move(requiredFiles);
            return definition;
        }

        [[nodiscard]] GameDefinition ambiguousDetectionDefinition(
            std::wstring id,
            std::wstring displayName)
        {
            GameDefinition definition;
            definition.schemaVersion = L"1";
            definition.definitionVersion = L"1.0.0";
            definition.id = GameId::parseOrThrow(id);
            definition.displayName = std::move(displayName);
            definition.defaultProfileName = L"Default";
            definition.dataFolder = L"Data";
            definition.requiredFiles = {L"SkyrimSE.exe"};
            definition.healthRules.requiredFiles = definition.requiredFiles;
            definition.executables.push_back(GameExecutableDefinition{
                L"game",
                definition.displayName,
                ExecutableName::parseOrThrow(L"SkyrimSE.exe"),
                GameExecutableRole::Primary,
                GameExecutableWorkingDirectoryKind::GameRoot
            });
            definition.executableRoles.primary = ExecutableName::parseOrThrow(L"SkyrimSE.exe");
            definition.uiTemplateId = UiTemplateId::parseOrThrow(id);
            definition.detectionHints.executableNames = {
                ExecutableName::parseOrThrow(L"SkyrimSE.exe")
            };
            definition.contentLayoutRules.dataFolder = L"Data";
            return definition;
        }
    }

    TEST(GameDetectionHealthServiceTests, ManualValidSkyrimPathDetectsAsSkyrimAndIsHealthy)
    {
        TempDirectory temp;
        const std::filesystem::path game = temp.path() / L"Skyrim Special Edition";
        writeSkyrimRequiredFiles(game);
        writeSkyrimOptionalExecutables(game);

        GameSupportRegistry registry;
        registry.loadEmbeddedDefinitions();

        const GameDetectionResult detection = detectSkyrimManual(registry, game);

        ASSERT_TRUE(detection.detected);
        EXPECT_EQ(detection.gameId.value(), L"skyrimse");
        EXPECT_EQ(detection.displayName, L"Skyrim Special Edition");
        EXPECT_EQ(detection.source, DetectionSource::ManualPath);
        EXPECT_EQ(detection.confidence, DetectionConfidence::Explicit);
        EXPECT_TRUE(contains(detection.matchedFiles, L"SkyrimSE.exe"));
        EXPECT_TRUE(contains(detection.matchedFiles, L"Data/Skyrim.esm"));

        const GameHealthCheckResult health = GameHealthCheckService().check(detection);
        EXPECT_EQ(health.status, HealthStatus::Healthy);
        EXPECT_TRUE(health.allowsAutomation());
        EXPECT_TRUE(health.missingFiles.empty());
    }

    TEST(GameDetectionHealthServiceTests, ManualInvalidSkyrimPathFailsHealthWithBlockingReason)
    {
        TempDirectory temp;
        const std::filesystem::path game = temp.path() / L"Skyrim Special Edition";
        std::filesystem::create_directories(game);

        GameSupportRegistry registry;
        registry.loadEmbeddedDefinitions();

        const GameDetectionResult detection = detectSkyrimManual(registry, game);
        ASSERT_TRUE(detection.detected);

        const GameHealthCheckResult health = GameHealthCheckService().check(detection);

        EXPECT_EQ(health.status, HealthStatus::Broken);
        EXPECT_FALSE(health.allowsAutomation());
        EXPECT_TRUE(contains(health.missingFiles, L"SkyrimSE.exe"));
        EXPECT_TRUE(contains(health.missingFiles, L"Data/Skyrim.esm"));
        ASSERT_FALSE(health.findings.empty());
        EXPECT_TRUE(std::any_of(
            health.findings.begin(),
            health.findings.end(),
            [](const GameHealthFinding& finding)
            {
                return finding.severity == HealthSeverity::Blocker &&
                    finding.code == L"missing-required-file";
            }));
    }

    TEST(GameDetectionHealthServiceTests, StoreHintsProduceConfidenceMetadata)
    {
        TempDirectory temp;
        const std::filesystem::path game = temp.path() / L"SteamLibrary" / L"Skyrim Special Edition";
        writeSkyrimRequiredFiles(game);
        writeSkyrimOptionalExecutables(game);

        GameSupportRegistry registry;
        registry.loadEmbeddedDefinitions();

        for (DetectionSource source : {DetectionSource::SteamHint, DetectionSource::GogHint, DetectionSource::EpicHint})
        {
            GameDetectionRequest request;
            request.storeHints.push_back(GameDetectionStoreHint{source, game, L"test-store"});

            const GameDetectionResult detection = GameDetectionService(registry).detect(request);

            ASSERT_TRUE(detection.detected);
            EXPECT_EQ(detection.gameId.value(), L"skyrimse");
            EXPECT_EQ(detection.source, source);
            EXPECT_EQ(detection.confidence, DetectionConfidence::High);
            EXPECT_TRUE(contains(detection.matchedFiles, L"SkyrimSE.exe"));
        }
    }

    TEST(GameDetectionHealthServiceTests, NonCriticalWarningsDoNotBlockAutomation)
    {
        TempDirectory temp;
        const std::filesystem::path game = temp.path() / L"Skyrim Special Edition";
        writeSkyrimRequiredFiles(game);

        GameSupportRegistry registry;
        registry.loadEmbeddedDefinitions();

        const GameDetectionResult detection = detectSkyrimManual(registry, game);
        ASSERT_TRUE(detection.detected);

        const GameHealthCheckResult health = GameHealthCheckService().check(detection);

        EXPECT_EQ(health.status, HealthStatus::Warning);
        EXPECT_TRUE(health.allowsAutomation());
        EXPECT_FALSE(health.warnings.empty());
        EXPECT_TRUE(std::any_of(
            health.findings.begin(),
            health.findings.end(),
            [](const GameHealthFinding& finding)
            {
                return finding.severity == HealthSeverity::Warning &&
                    finding.code == L"missing-optional-executable";
            }));
    }

    TEST(GameDetectionHealthServiceTests, HealthCacheInvalidatesWhenDefinitionVersionChanges)
    {
        TempDirectory temp;
        const std::filesystem::path game = temp.path() / L"Health Test Game";
        writeTextFile(game / L"Game.exe", "MZ");
        writeTextFile(game / L"Data" / L"Required.dat", "required");

        GameDefinition firstDefinition =
            healthTestDefinition(L"1.0.0", {L"Data/Required.dat"});
        DefinitionBackedGameSupport firstSupport(firstDefinition);

        const GameHealthCheckResult first = GameHealthCheckService().check(GameHealthCheckRequest{
            &firstSupport,
            &firstDefinition,
            game
        });
        ASSERT_EQ(first.status, HealthStatus::Healthy);
        EXPECT_TRUE(first.missingFiles.empty());

        GameDefinition secondDefinition =
            healthTestDefinition(L"2.0.0", {L"Data/MissingAfterVersionBump.dat"});
        DefinitionBackedGameSupport secondSupport(secondDefinition);

        const GameHealthCheckResult second = GameHealthCheckService().check(GameHealthCheckRequest{
            &secondSupport,
            &secondDefinition,
            game
        });

        EXPECT_EQ(second.status, HealthStatus::Broken);
        EXPECT_TRUE(contains(second.missingFiles, L"Data/MissingAfterVersionBump.dat"));
    }

    TEST(GameDetectionHealthServiceTests, AmbiguousDetectionNeverDefaultsToSkyrim)
    {
        TempDirectory temp;
        const std::filesystem::path game = temp.path() / L"Ambiguous Game";
        writeTextFile(game / L"SkyrimSE.exe", "MZ");

        GameSupportRegistry registry;
        registry.replaceDefinitions({
            ambiguousDetectionDefinition(L"skyrimse", L"Skyrim Special Edition"),
            ambiguousDetectionDefinition(L"skyrim-compatible-test", L"Skyrim-Compatible Test Game")
        });

        GameDetectionRequest request;
        request.installPath = game;

        const GameDetectionResult detection = GameDetectionService(registry).detect(request);

        EXPECT_FALSE(detection.detected);
        EXPECT_TRUE(detection.gameId.empty());
        EXPECT_EQ(detection.source, DetectionSource::Ambiguous);
        EXPECT_EQ(detection.confidence, DetectionConfidence::High);
        ASSERT_EQ(detection.ambiguousCandidates.size(), 2U);
        EXPECT_TRUE(std::any_of(
            detection.ambiguousCandidates.begin(),
            detection.ambiguousCandidates.end(),
            [](const GameDetectionCandidate& candidate)
            {
                return candidate.gameId.value() == L"skyrimse";
            }));
        EXPECT_TRUE(std::any_of(
            detection.ambiguousCandidates.begin(),
            detection.ambiguousCandidates.end(),
            [](const GameDetectionCandidate& candidate)
            {
                return candidate.gameId.value() == L"skyrim-compatible-test";
            }));
    }
}
