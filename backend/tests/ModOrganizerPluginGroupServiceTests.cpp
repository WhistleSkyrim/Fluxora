#include "FluxoraCore/Services/ModOrganizerPluginGroupService.hpp"

#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

namespace fluxora::tests
{
    TEST(ModOrganizerPluginGroupServiceTests, ReadsGroupsInResolvedPluginOrder)
    {
        TempDirectory temp;
        const std::filesystem::path profile = temp.path() / L"profiles" / L"Default";
        writeTextFile(
            profile / L"loadorder.txt",
            "Skyrim.esm\n"
            "Update.esm\n"
            "SkyUI.esp\n"
            "Synthesis.esp\n");
        writeTextFile(
            profile / L"plugins.txt",
            "*Skyrim.esm\n"
            "*SkyUI.esp\n"
            "*Legacy.esp\n"
            "*Synthesis.esp\n");
        writeTextFile(
            profile / L"plugingroups.txt",
            "SkyUI.esp|Interface\n"
            "Synthesis.esp|Patchers\n"
            "Legacy.esp|Legacy\n");

        BuildTemplate resolvedTemplate;
        resolvedTemplate.basePlugins = {L"Skyrim.esm", L"Update.esm"};

        const std::vector<ProfilePluginOrderImportItemRecord> items =
            ModOrganizerPluginGroupService::read(profile, resolvedTemplate);

        ASSERT_EQ(items.size(), 8U);
        EXPECT_EQ(items[0].kind, L"plugin");
        EXPECT_EQ(items[0].pluginName, L"Skyrim.esm");
        EXPECT_EQ(items[1].kind, L"plugin");
        EXPECT_EQ(items[1].pluginName, L"Update.esm");
        EXPECT_EQ(items[2].kind, L"separator");
        EXPECT_EQ(items[2].separatorTitle, L"Interface");
        EXPECT_EQ(items[3].kind, L"plugin");
        EXPECT_EQ(items[3].pluginName, L"SkyUI.esp");
        EXPECT_EQ(items[4].kind, L"separator");
        EXPECT_EQ(items[4].separatorTitle, L"Patchers");
        EXPECT_EQ(items[5].kind, L"plugin");
        EXPECT_EQ(items[5].pluginName, L"Synthesis.esp");
        EXPECT_EQ(items[6].kind, L"separator");
        EXPECT_EQ(items[6].separatorTitle, L"Legacy");
        EXPECT_EQ(items[7].kind, L"plugin");
        EXPECT_EQ(items[7].pluginName, L"Legacy.esp");
    }

    TEST(ModOrganizerPluginGroupServiceTests, MissingGroupsReturnsEmptyItems)
    {
        TempDirectory temp;
        const std::filesystem::path profile = temp.path() / L"profiles" / L"Default";
        writeTextFile(profile / L"plugins.txt", "*SkyUI.esp\n");

        BuildTemplate resolvedTemplate;
        const std::vector<ProfilePluginOrderImportItemRecord> items =
            ModOrganizerPluginGroupService::read(profile, resolvedTemplate);

        EXPECT_TRUE(items.empty());
    }
}
