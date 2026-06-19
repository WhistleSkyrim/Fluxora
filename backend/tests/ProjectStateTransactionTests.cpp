#include "FluxoraCore/Storage/AtomicFileStore.hpp"
#include "FluxoraCore/Storage/ProjectStateTransaction.hpp"
#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace fluxora::tests
{
    TEST(ProjectStateTransactionTests, CommitRemovesMarker)
    {
        TempDirectory temp;
        const std::filesystem::path markerDirectory = temp.path() / L"profile";
        const std::filesystem::path plugins = markerDirectory / L"plugins.txt";

        AtomicFileStore store;
        ProjectStateTransaction transaction(store, markerDirectory, L"profile update");
        transaction.trackFile(plugins, L"plugin list state", ProjectStateValidation::Utf8Text);
        const std::filesystem::path marker = transaction.markerPath();

        ASSERT_TRUE(std::filesystem::exists(marker));
        store.writeTextFile(
            plugins,
            "*Skyrim.esm\n",
            AtomicFileWriteOptions{
                L"plugin list state",
                ProjectStateValidation::Utf8Text
            });
        transaction.commit();

        EXPECT_FALSE(std::filesystem::exists(marker));
    }

    TEST(ProjectStateTransactionTests, RecoversFilesListedInMarker)
    {
        TempDirectory temp;
        const std::filesystem::path markerDirectory = temp.path() / L"profile";
        const std::filesystem::path manifest = markerDirectory / L"manifest.json";
        const std::filesystem::path plugins = markerDirectory / L"plugins.txt";

        AtomicFileStore store;
        store.writeTextFile(
            manifest,
            R"({"schemaVersion":"1","name":"old"})",
            AtomicFileWriteOptions{
                L"project manifest",
                ProjectStateValidation::JsonObject
            });
        store.writeTextFile(
            plugins,
            "*Skyrim.esm\n",
            AtomicFileWriteOptions{
                L"plugin list state",
                ProjectStateValidation::Utf8Text
            });

        ProjectStateTransaction transaction(store, markerDirectory, L"multi-file update");
        transaction.trackFile(manifest, L"project manifest", ProjectStateValidation::JsonObject);
        transaction.trackFile(plugins, L"plugin list state", ProjectStateValidation::Utf8Text);
        const std::filesystem::path marker = transaction.markerPath();

        store.writeTextFile(
            manifest,
            R"({"schemaVersion":"1","name":"new"})",
            AtomicFileWriteOptions{
                L"project manifest",
                ProjectStateValidation::JsonObject
            });
        EXPECT_THROW(
            store.writeTextFile(
                plugins,
                "*Skyrim.esm\n*SkyUI.esp\n",
                AtomicFileWriteOptions{
                    L"plugin list state",
                    ProjectStateValidation::Utf8Text,
                    {},
                    true,
                    AtomicWriteFailurePoint::AfterTempFileValidated
                }),
            std::runtime_error);

        const std::vector<ProjectStateTransactionRecovery> recovered =
            ProjectStateTransaction::recoverDirectory(markerDirectory, store);

        ASSERT_EQ(recovered.size(), 1U);
        EXPECT_EQ(recovered.front().markerPath, marker);
        EXPECT_FALSE(std::filesystem::exists(marker));
        EXPECT_EQ(readTextFile(manifest), R"({"schemaVersion":"1","name":"new"})");
        EXPECT_EQ(readTextFile(plugins), "*Skyrim.esm\n");
        transaction.commit();
    }
}
