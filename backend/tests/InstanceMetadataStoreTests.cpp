#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"

#include "FluxoraCore/Storage/AtomicFileStore.hpp"
#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace fluxora::tests
{
    namespace
    {
        const InstalledModRecord* findInstalledMod(
            const std::vector<InstalledModRecord>& mods,
            std::wstring_view folderName)
        {
            const auto match = std::find_if(
                mods.begin(),
                mods.end(),
                [folderName](const InstalledModRecord& mod)
                {
                    return mod.folderName == folderName;
                });
            return match == mods.end() ? nullptr : &(*match);
        }

        std::filesystem::path portableManifestPath(const std::filesystem::path& modPath)
        {
            return modPath / L".flow" / L"manifest.json";
        }
    }

    TEST(InstanceMetadataStoreTests, ReplaceProfileOrderMatchesImportedModNamesCaseInsensitively)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / L"project";
        const std::filesystem::path mods = project / L"mods";
        const std::filesystem::path skyUi = mods / L"SkyUI";

        writeTextFile(skyUi / L"interface" / L"skyui.swf", "ui");

        InstanceMetadataStore::ensureInstance(project, L"skyrimse");
        InstanceMetadataStore::registerInstalledMods(
            project,
            {
                InstalledModImportRecord{skyUi, L"SkyUI", {}, true, {}}
            });

        InstanceMetadataStore::replaceProfileOrderItems(
            project,
            L"Default",
            {
                ProfileOrderImportItemRecord{L"mod", L"skyui", {}}
            });

        const std::vector<ProfileOrderItemRecord> order =
            InstanceMetadataStore::listProfileOrderItems(project, L"Default", mods);

        ASSERT_EQ(order.size(), 1U);
        EXPECT_TRUE(order[0].hasMod);
        EXPECT_EQ(order[0].mod.folderName, L"SkyUI");
        EXPECT_EQ(order[0].position, 0);
    }

    TEST(InstanceMetadataStoreTests, SetAllInstalledModsEnabledSkipsExistingManifestRewriteDuringStateFlip)
    {
#ifndef _WIN32
        GTEST_SKIP() << "Fluxora instance metadata storage is implemented for Windows builds.";
#else
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / L"project";
        const std::filesystem::path mods = project / L"mods";
        const std::wstring folderName =
            L"A Patch For Deadly Spell Impacts And Audio Overhaul Compatibility Patch Plus PSI";
        const std::filesystem::path modPath = mods / std::filesystem::path(folderName);
        const std::wstring existingFolderName = L"Existing Manifest Mod";
        const std::filesystem::path existingModPath = mods / std::filesystem::path(existingFolderName);
        writeTextFile(modPath / L"Data" / L"Patch.esp", "plugin");
        writeTextFile(existingModPath / L"Data" / L"Existing.esp", "plugin");

        InstanceMetadataStore::ensureInstance(project, L"skyrimse");
        InstanceMetadataStore::registerInstalledMods(
            project,
            {
                InstalledModImportRecord{modPath, folderName, {}, true, {}},
                InstalledModImportRecord{existingModPath, existingFolderName, {}, true, {}}
            });

        const std::filesystem::path manifest = portableManifestPath(modPath);
        const std::filesystem::path existingManifest = portableManifestPath(existingModPath);
        ASSERT_TRUE(std::filesystem::is_regular_file(manifest));
        ASSERT_TRUE(std::filesystem::is_regular_file(existingManifest));
        ASSERT_TRUE(std::filesystem::remove(manifest));
        const std::string existingManifestBefore = readTextFile(existingManifest);

        InstanceMetadataStore::setAllInstalledModsEnabled(project, false, mods);

        std::vector<InstalledModRecord> records =
            InstanceMetadataStore::listInstalledMods(project, mods);
        const InstalledModRecord* disabled = findInstalledMod(records, folderName);
        ASSERT_NE(disabled, nullptr);
        EXPECT_EQ(disabled->state, L"disabled");
        const InstalledModRecord* existingDisabled = findInstalledMod(records, existingFolderName);
        ASSERT_NE(existingDisabled, nullptr);
        EXPECT_EQ(existingDisabled->state, L"disabled");
        ASSERT_TRUE(std::filesystem::is_regular_file(manifest));
        const std::string disabledManifest = readTextFile(manifest);
        EXPECT_NE(disabledManifest.find(R"("state":"disabled")"), std::string::npos);
        EXPECT_FALSE(std::filesystem::exists(AtomicFileStore::backupPathFor(manifest)));
        EXPECT_EQ(readTextFile(existingManifest), existingManifestBefore);

        InstanceMetadataStore::setAllInstalledModsEnabled(project, true, mods);

        records = InstanceMetadataStore::listInstalledMods(project, mods);
        const InstalledModRecord* enabled = findInstalledMod(records, folderName);
        ASSERT_NE(enabled, nullptr);
        EXPECT_EQ(enabled->state, L"installed");
        EXPECT_EQ(readTextFile(manifest), disabledManifest);
        EXPECT_FALSE(std::filesystem::exists(AtomicFileStore::backupPathFor(manifest)));

        InstanceMetadataStore::setAllInstalledModsEnabled(project, true, mods);

        EXPECT_NE(readTextFile(manifest).find(R"("state":"installed")"), std::string::npos);
#endif
    }

    TEST(InstanceMetadataStoreTests, RegisterInstalledModsCanDeferContentFingerprintUntilFileSummary)
    {
#ifndef _WIN32
        GTEST_SKIP() << "Fluxora instance metadata storage is implemented for Windows builds.";
#else
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / L"project";
        const std::filesystem::path mods = project / L"mods";
        const std::filesystem::path modPath = mods / L"Deferred Fingerprint";
        writeTextFile(modPath / L"interface" / L"menu.swf", "ui");
        writeTextFile(modPath / L"scripts" / L"setup.pex", "script");

        InstanceMetadataStore::ensureInstance(project, L"skyrimse");
        InstanceMetadataStore::registerInstalledMods(
            project,
            {
                InstalledModImportRecord{modPath, L"Deferred Fingerprint", {}, true, {}, false}
            });

        std::vector<InstalledModRecord> records = InstanceMetadataStore::listInstalledMods(project, mods);
        const InstalledModRecord* deferred = findInstalledMod(records, L"Deferred Fingerprint");
        ASSERT_NE(deferred, nullptr);
        EXPECT_TRUE(deferred->contentFingerprint.empty());
        EXPECT_NE(readTextFile(portableManifestPath(modPath)).find(R"("contentFingerprint":"")"), std::string::npos);

        const ModFileSummary summary = InstanceMetadataStore::summarizeModFiles(project, modPath, mods);
        EXPECT_EQ(summary.fileCount, 2);

        records = InstanceMetadataStore::listInstalledMods(project, mods);
        deferred = findInstalledMod(records, L"Deferred Fingerprint");
        ASSERT_NE(deferred, nullptr);
        EXPECT_FALSE(deferred->contentFingerprint.empty());
#endif
    }
}
