#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Services/ModOrganizerImportService.hpp"
#include "FluxoraCore/Services/ProjectService.hpp"
#include "FluxoraCore/Services/TemplateService.hpp"
#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"

#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>

namespace fluxora::tests
{
    TEST(ModOrganizerImportServiceTests, ImportCopiesQtByteArrayOverwriteDirectory)
    {
        TempDirectory temp;
        ScopedEnvironmentVariable userProfile(L"USERPROFILE", (temp.path() / L"User").wstring());

        const std::filesystem::path source = temp.path() / L"MO2";
        const std::filesystem::path destinationRoot = temp.path() / L"Imported";

        writeTextFile(source / L"GameRoot" / L"SkyrimSE.exe", "MZ executable stub");
        std::filesystem::create_directories(source / L"GameRoot" / L"Data");
        writeTextFile(source / L"GameRoot" / L"Data" / L"Skyrim.esm", "master");
        writeTextFile(source / L"mods" / L"SkyUI" / L"interface" / L"skyui.swf", "ui");
        writeTextFile(
            source / L"mods" / L"SkyUI" / L"meta.ini",
            "[General]\nname=SkyUI\nversion=1\nmodid=3863\nfileid=123\nnewestVersion=5.2\n");
        writeTextFile(source / L"mods" / L"Address Library" / L"skse" / L"plugins" / L"versionlib.bin", "lib");
        writeTextFile(
            source / L"mods" / L"Address Library" / L"meta.ini",
            "[General]\nname=Address Library\nversion=11\nmodid=32444\n"
            "\n"
            "[installedFiles]\n"
            "1\\modid=32444\n"
            "1\\fileid=392563\n"
            "size=1\n");
        writeTextFile(source / L"profiles" / L"Default" / L"modlist.txt", "+SkyUI\n+Address Library\n");
        writeTextFile(source / L"profiles" / L"Default" / L"plugins.txt", "*Skyrim.esm\n");
        writeTextFile(source / L"external overwrite" / L"SKSE" / L"Plugins" / L"generated.log", "overwrite");

        writeTextFile(
            source / L"ModOrganizer.ini",
            "[General]\n"
            "gameName=Skyrim Special Edition\n"
            "gamePath=GameRoot\n"
            "selected_profile=Default\n"
            "\n"
            "[Settings]\n"
            "overwrite_directory=@ByteArray(external overwrite)\n");

        Logger logger;
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        BuildPathSettingsService pathSettings(logger);
        ModOrganizerImportService importer(logger, templates, projects, pathSettings);

        ModOrganizerImportRequest request;
        request.sourceDirectory = source;
        request.destinationRootDirectory = destinationRoot;
        request.mode = ModOrganizerImportMode::CreateNew;

        const ModOrganizerImportResult result = importer.importInstance(request);

        const std::filesystem::path importedOverwrite =
            result.analysis.targetProjectDirectory / L"overwrite" / L"SKSE" / L"Plugins" / L"generated.log";
        ASSERT_TRUE(std::filesystem::is_regular_file(importedOverwrite));
        EXPECT_EQ(readTextFile(importedOverwrite), "overwrite");

        const BuildPathSettings settings = pathSettings.loadForConfig(result.analysis.targetConfigPath);
        EXPECT_EQ(normalized(settings.gameDirectory), normalized(result.analysis.targetProjectDirectory / L"GameRoot"));
        EXPECT_EQ(normalized(settings.overwriteDirectory), normalized(result.analysis.targetProjectDirectory / L"overwrite"));

        const std::string manifest = readTextFile(result.analysis.targetConfigPath);
        EXPECT_NE(manifest.find("GameRoot"), std::string::npos);
        EXPECT_NE(manifest.find("\"projectFingerprint\""), std::string::npos);
        EXPECT_NE(manifest.find("\"gameId\":\"skyrimse\""), std::string::npos);
        EXPECT_NE(manifest.find("\"healthStatusAtCreation\":\"warning\""), std::string::npos);

        const std::vector<InstalledModRecord> importedMods =
            InstanceMetadataStore::listInstalledMods(result.analysis.targetProjectDirectory);
        const auto skyUi = std::find_if(
            importedMods.begin(),
            importedMods.end(),
            [](const InstalledModRecord& mod)
            {
                return mod.folderName == L"SkyUI";
            });
        ASSERT_NE(skyUi, importedMods.end());
        EXPECT_EQ(skyUi->source.provider, L"nexus");
        EXPECT_EQ(skyUi->source.remoteModId, L"3863");
        EXPECT_EQ(skyUi->source.remoteFileId, L"123");
        EXPECT_EQ(skyUi->source.url, L"nxm://skyrimspecialedition/mods/3863/files/123");

        const auto addressLibrary = std::find_if(
            importedMods.begin(),
            importedMods.end(),
            [](const InstalledModRecord& mod)
            {
                return mod.folderName == L"Address Library";
            });
        ASSERT_NE(addressLibrary, importedMods.end());
        EXPECT_EQ(addressLibrary->source.provider, L"nexus");
        EXPECT_EQ(addressLibrary->source.remoteModId, L"32444");
        EXPECT_EQ(addressLibrary->source.remoteFileId, L"392563");
        EXPECT_EQ(addressLibrary->source.url, L"nxm://skyrimspecialedition/mods/32444/files/392563");
    }

    TEST(ModOrganizerImportServiceTests, ImportRejectsUnsupportedGameInsteadOfUsingFirstTemplate)
    {
        TempDirectory temp;

        const std::filesystem::path source = temp.path() / L"MO2";
        const std::filesystem::path destinationRoot = temp.path() / L"Imported";

        writeTextFile(source / L"mods" / L"Generic Mod" / L"meta.ini", "[General]\nname=Generic Mod\n");
        writeTextFile(source / L"profiles" / L"Default" / L"modlist.txt", "+Generic Mod\n");
        writeTextFile(
            source / L"ModOrganizer.ini",
            "[General]\n"
            "gameName=Definitely Unknown Game\n"
            "gamePath=GameRoot\n"
            "selected_profile=Default\n");

        Logger logger;
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        BuildPathSettingsService pathSettings(logger);
        ModOrganizerImportService importer(logger, templates, projects, pathSettings);

        ModOrganizerImportRequest request;
        request.sourceDirectory = source;
        request.destinationRootDirectory = destinationRoot;
        request.mode = ModOrganizerImportMode::CreateNew;

        EXPECT_THROW((void)importer.importInstance(request), std::invalid_argument);
    }

    TEST(ModOrganizerImportServiceTests, ImportRejectsSupportedGameWithBadHealth)
    {
        TempDirectory temp;

        const std::filesystem::path source = temp.path() / L"MO2";
        const std::filesystem::path destinationRoot = temp.path() / L"Imported";

        std::filesystem::create_directories(source / L"GameRoot");
        writeTextFile(source / L"mods" / L"SkyUI" / L"meta.ini", "[General]\nname=SkyUI\n");
        writeTextFile(source / L"profiles" / L"Default" / L"modlist.txt", "+SkyUI\n");
        writeTextFile(
            source / L"ModOrganizer.ini",
            "[General]\n"
            "gameName=Skyrim Special Edition\n"
            "gamePath=GameRoot\n"
            "selected_profile=Default\n");

        Logger logger;
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        BuildPathSettingsService pathSettings(logger);
        ModOrganizerImportService importer(logger, templates, projects, pathSettings);

        const ModOrganizerImportAnalysis analysis = importer.analyze(source, destinationRoot);
        EXPECT_FALSE(analysis.canImport);
        EXPECT_NE(analysis.statusMessage.find(L"Невозможно импортировать сборку"), std::wstring::npos);

        ModOrganizerImportRequest request;
        request.sourceDirectory = source;
        request.destinationRootDirectory = destinationRoot;
        request.mode = ModOrganizerImportMode::CreateNew;

        EXPECT_THROW((void)importer.importInstance(request), std::invalid_argument);
    }
}
