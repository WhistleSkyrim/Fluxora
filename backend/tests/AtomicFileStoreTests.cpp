#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Storage/AtomicFileStore.hpp"
#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>

namespace fluxora::tests
{
    TEST(AtomicFileStoreTests, SimulatedCrashDuringManifestWriteRecoversOldManifest)
    {
        TempDirectory temp;
        const std::filesystem::path manifest = temp.path() / L"manifest.json";
        writeTextFile(manifest, R"({"schemaVersion":"1","name":"old"})");

        AtomicFileStore store;
        EXPECT_THROW(
            store.writeTextFile(
                manifest,
                R"({"schemaVersion":"1","name":"new"})",
                AtomicFileWriteOptions{
                    L"project manifest",
                    ProjectStateValidation::JsonObject,
                    {},
                    true,
                    AtomicWriteFailurePoint::AfterTempFileValidated
                }),
            std::runtime_error);

        const AtomicFileRecoveryResult recovered = store.recoverFile(
            manifest,
            AtomicFileWriteOptions{
                L"project manifest",
                ProjectStateValidation::JsonObject
            });

        EXPECT_EQ(readTextFile(manifest), R"({"schemaVersion":"1","name":"old"})");
        EXPECT_EQ(recovered.action, AtomicFileRecoveryAction::RemovedStaleTemp);
    }

    TEST(AtomicFileStoreTests, SimulatedCrashDuringPluginListWriteRecoversOldPluginList)
    {
        TempDirectory temp;
        const std::filesystem::path plugins = temp.path() / L"plugins.txt";
        writeTextFile(plugins, "*Skyrim.esm\n");

        AtomicFileStore store;
        EXPECT_THROW(
            store.writeTextFile(
                plugins,
                "*Skyrim.esm\n*SkyUI.esp\n",
                AtomicFileWriteOptions{
                    L"plugin list state",
                    ProjectStateValidation::Utf8Text,
                    {},
                    true,
                    AtomicWriteFailurePoint::BeforeReplace
                }),
            std::runtime_error);

        static_cast<void>(store.recoverFile(
            plugins,
            AtomicFileWriteOptions{
                L"plugin list state",
                ProjectStateValidation::Utf8Text
            }));

        EXPECT_EQ(readTextFile(plugins), "*Skyrim.esm\n");
    }

    TEST(AtomicFileStoreTests, DiskFullSimulationKeepsPreviousState)
    {
        TempDirectory temp;
        const std::filesystem::path manifest = temp.path() / L"manifest.json";
        writeTextFile(manifest, R"({"schemaVersion":"1","name":"old"})");

        AtomicFileStore store;
        try
        {
            store.writeTextFile(
                manifest,
                R"({"schemaVersion":"1","name":"new"})",
                AtomicFileWriteOptions{
                    L"project manifest",
                    ProjectStateValidation::JsonObject,
                    {},
                    true,
                    AtomicWriteFailurePoint::None,
                    8
                });
            FAIL() << "Expected disk-full simulation to throw.";
        }
        catch (const std::runtime_error& exception)
        {
            EXPECT_NE(std::string(exception.what()).find("Disk is full"), std::string::npos);
        }

        EXPECT_EQ(readTextFile(manifest), R"({"schemaVersion":"1","name":"old"})");
    }

    TEST(AtomicFileStoreTests, BackupPathUsesCompactSiblingName)
    {
        TempDirectory temp;
        const std::filesystem::path manifest =
            temp.path() / L"Very Long Installed Mod Metadata Manifest Name.json";

        const std::filesystem::path backup = AtomicFileStore::backupPathFor(manifest);

        EXPECT_EQ(backup.parent_path(), manifest.parent_path());
        EXPECT_LT(backup.filename().wstring().size(), manifest.filename().wstring().size());
    }

    TEST(AtomicFileStoreTests, CorruptedManifestRestoresPreviousBackup)
    {
        TempDirectory temp;
        const std::filesystem::path manifest = temp.path() / L"manifest.json";

        AtomicFileStore store;
        store.writeTextFile(
            manifest,
            R"({"schemaVersion":"1","name":"old"})",
            AtomicFileWriteOptions{
                L"project manifest",
                ProjectStateValidation::JsonObject
            });
        store.writeTextFile(
            manifest,
            R"({"schemaVersion":"1","name":"new"})",
            AtomicFileWriteOptions{
                L"project manifest",
                ProjectStateValidation::JsonObject
            });

        writeTextFile(manifest, R"({"schemaVersion":)");
        const AtomicFileRecoveryResult recovered = store.recoverFile(
            manifest,
            AtomicFileWriteOptions{
                L"project manifest",
                ProjectStateValidation::JsonObject
            });

        EXPECT_EQ(recovered.action, AtomicFileRecoveryAction::RestoredBackup);
        EXPECT_EQ(readTextFile(manifest), R"({"schemaVersion":"1","name":"old"})");
    }

    TEST(AtomicFileStoreTests, RecoveryLogsRestoredBackup)
    {
        TempDirectory temp;
        const std::filesystem::path manifest = temp.path() / L"manifest.json";

        Logger logger;
        logger.initialize();
        ASSERT_TRUE(logger.isInitialized());

        AtomicFileStore store;
        store.writeTextFile(
            manifest,
            R"({"schemaVersion":"1","name":"old"})",
            AtomicFileWriteOptions{
                L"project manifest",
                ProjectStateValidation::JsonObject
            });
        store.writeTextFile(
            manifest,
            R"({"schemaVersion":"1","name":"new"})",
            AtomicFileWriteOptions{
                L"project manifest",
                ProjectStateValidation::JsonObject
            });
        writeTextFile(manifest, R"({"schemaVersion":)");

        static_cast<void>(store.recoverFile(
            manifest,
            AtomicFileWriteOptions{
                L"project manifest",
                ProjectStateValidation::JsonObject
            },
            &logger));
        logger.shutdown();

        const std::string log = readTextFile(logger.logPath());
        EXPECT_NE(log.find("recoveryAction=restoredBackup"), std::string::npos);
        EXPECT_NE(log.find("backup=\""), std::string::npos);
    }
}
