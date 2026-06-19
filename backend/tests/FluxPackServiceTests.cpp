#include "FluxoraCore/Services/AppSettingsService.hpp"
#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/DownloadService.hpp"
#include "FluxoraCore/Services/FluxPackService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Services/ProjectService.hpp"
#include "FluxoraCore/Services/TemplateService.hpp"
#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"
#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace fluxora::tests
{
    namespace
    {
        std::string projectManifest()
        {
            return "{"
                "\"schemaVersion\":\"1\","
                "\"name\":\"FluxPack Test Build\","
                "\"templateId\":\"skyrimse\","
                "\"gameName\":\"Skyrim Special Edition\","
                "\"gamePath\":\"../Skyrim Special Edition\","
                "\"installRoot\":\"../Builds\","
                "\"projectDirectory\":\"../Builds/FluxPack Test Build\","
                "\"dataDirectory\":\"Data\","
                "\"nexusDomain\":\"skyrimspecialedition\","
                "\"defaultProfile\":\"Default\""
                "}";
        }

        std::string toUtf8(const std::wstring& value)
        {
#ifdef _WIN32
            if (value.empty())
            {
                return {};
            }

            const int size = WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            std::string out(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                value.data(),
                static_cast<int>(value.size()),
                out.data(),
                size,
                nullptr,
                nullptr);
            return out;
#else
            return std::string(value.begin(), value.end());
#endif
        }
    }

    TEST(FluxPackServiceTests, ExportProjectWritesRecipeSectionsAndInspectSummary)
    {
#ifndef _WIN32
        GTEST_SKIP() << "FluxPack export uses Windows instance metadata in this build.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        const std::filesystem::path installRoot = temp.path() / L"Builds";
        const std::filesystem::path project = installRoot / L"FluxPack Test Build";
        const std::filesystem::path game = temp.path() / L"Skyrim Special Edition";
        const std::filesystem::path config = temp.path() / L"configs" / L"FluxPack Test Build.json";
        const std::filesystem::path mods = project / L"mods";
        const std::filesystem::path profiles = project / L"profiles";
        const std::filesystem::path downloads = project / L"downloads";
        const std::filesystem::path overwrite = project / L"overwrite";

        writeTextFile(game / L"SkyrimSE.exe", "MZ");
        writeTextFile(game / L"Data" / L"Skyrim.esm", "master");
        writeTextFile(config, projectManifest());

        const std::filesystem::path skyUi = mods / L"SkyUI";
        const std::filesystem::path nemesis = mods / L"Nemesis Output";
        const std::filesystem::path patch = mods / L"My Custom Patch";
        writeTextFile(skyUi / L"interface" / L"skyui.swf", "ui");
        writeTextFile(nemesis / L"meshes" / L"actors" / L"behavior.hkx", "generated");
        writeTextFile(patch / L"Data" / L"MyPatch.esp", "patch");
        writeTextFile(profiles / L"Default" / L"plugins.txt", "*Skyrim.esm\n*MyPatch.esp\n");
        writeTextFile(profiles / L"Default" / L"loadorder.txt", "Skyrim.esm\nMyPatch.esp\n");
        writeTextFile(overwrite / L"SKSE" / L"Plugins" / L"Example.ini", "[General]\nEnabled=1\n");
        writeTextFile(downloads / L"Old MO2 Archive.7z.meta", "[General]\nuninstalled=true\n");
        writeTextFile(downloads / L"SkyUI.7z", "source archive");
        writeTextFile(
            downloads / L"SkyUI.7z.fluxora.json",
            "{"
            "\"source\":\"nxm://skyrimspecialedition/mods/3863/files/123\","
            "\"gameDomain\":\"skyrimspecialedition\","
            "\"modId\":\"3863\","
            "\"fileId\":\"123\","
            "\"nexusModName\":\"SkyUI\","
            "\"version\":\"5.2\""
            "}");

        Logger logger;
        logger.initialize();
        AppSettingsService settings(logger);
        settings.initialize();
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        projects.initialize();
        BuildPathSettingsService pathSettings(logger);
        pathSettings.initialize();
        DownloadService downloadService(logger, settings, pathSettings);
        downloadService.initialize();
        const BuildPathSettings savedPaths = pathSettings.saveForConfig(
            config,
            BuildPathSettings{
                game,
                mods,
                profiles,
                downloads,
                overwrite
            });
        EXPECT_EQ(normalized(savedPaths.modsDirectory), normalized(mods));

        InstanceMetadataStore::ensureInstance(project, L"skyrimse");
        InstanceMetadataStore::registerInstalledMods(
            project,
            {
                InstalledModImportRecord{
                    skyUi,
                    L"SkyUI",
                    L"5.2",
                    true,
                    ModSourceRecord{
                    L"nexus",
                    L"skyrimspecialedition",
                    L"3863",
                    L"123",
                    L"https://www.nexusmods.com/skyrimspecialedition/mods/3863",
                    {},
                    L"5.2"
                }
                },
                InstalledModImportRecord{nemesis, L"Nemesis Output", {}, true, {}},
                InstalledModImportRecord{patch, L"My Custom Patch", L"1.0", true, {}}
            });
        InstanceMetadataStore::replaceProfileOrderItems(
            project,
            L"Default",
            {
                ProfileOrderImportItemRecord{L"mod", L"SkyUI", {}},
                ProfileOrderImportItemRecord{L"mod", L"My Custom Patch", {}}
            });

        FluxPackService service(logger, projects, downloadService, pathSettings);
        service.initialize();

        const std::filesystem::path output = temp.path() / L"FluxPack Test Build.fluxpack";
        const FluxPackSummary exported = service.exportProject(FluxPackExportRequest{
            config,
            output,
            true
        });

        EXPECT_EQ(exported.buildName, L"FluxPack Test Build");
        EXPECT_EQ(exported.sourceArchiveCount, 1U);
        EXPECT_EQ(exported.generatedAssetCount, 1U);
        EXPECT_EQ(exported.customPatchCount, 1U);
        EXPECT_GE(exported.customConfigCount, 3U);
        EXPECT_EQ(exported.installStepCount, 4U);
        EXPECT_TRUE(exported.generatedAssetsIncluded);
        EXPECT_TRUE(exported.installPlanAvailable);
        EXPECT_TRUE(std::filesystem::is_regular_file(output));

        const std::string manifest = readTextFile(output);
        EXPECT_NE(manifest.find("\"format\":\"FluxPack\""), std::string::npos);
        EXPECT_NE(manifest.find("\"gamePath\""), std::string::npos);
        EXPECT_NE(manifest.find("\"sourceArchives\""), std::string::npos);
        EXPECT_NE(manifest.find("\"generatedAssets\""), std::string::npos);
        EXPECT_NE(manifest.find("\"customPatches\""), std::string::npos);
        EXPECT_NE(manifest.find("\"customConfigs\""), std::string::npos);
        EXPECT_NE(manifest.find("\"source-archives\""), std::string::npos);
        EXPECT_NE(manifest.find("\"status\":\"matched-local-download\""), std::string::npos);
        EXPECT_NE(manifest.find("\"url\":\"nxm://skyrimspecialedition/mods/3863/files/123\""), std::string::npos);
        EXPECT_EQ(manifest.find("Old MO2 Archive.7z.meta"), std::string::npos);
        EXPECT_NE(manifest.find("\"text\":\"*Skyrim.esm\\n*MyPatch.esp\\n\""), std::string::npos);

        const FluxPackSummary inspected = service.inspectFluxPack(output);
        EXPECT_EQ(inspected.buildName, L"FluxPack Test Build");
        EXPECT_EQ(inspected.sourceArchiveCount, 1U);
        EXPECT_EQ(inspected.generatedAssetCount, 1U);
        EXPECT_EQ(inspected.customPatchCount, 1U);
        EXPECT_GE(inspected.customConfigCount, 3U);
        EXPECT_EQ(inspected.installStepCount, 4U);
        EXPECT_TRUE(inspected.generatedAssetsIncluded);
        EXPECT_TRUE(inspected.installPlanAvailable);

        service.shutdown();
        downloadService.shutdown();
        projects.shutdown();
        templates.shutdown();
        pathSettings.shutdown();
        settings.shutdown();
        logger.shutdown();
#endif
    }

    TEST(FluxPackServiceTests, InstallFluxPackCreatesProjectAndAppliesEmbeddedConfigs)
    {
#ifndef _WIN32
        GTEST_SKIP() << "FluxPack install project creation uses Windows game detection in this build.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        const std::filesystem::path installRoot = temp.path() / L"Installed";
        const std::filesystem::path game = temp.path() / L"Skyrim Special Edition";
        const std::filesystem::path fluxPack = temp.path() / L"Foundation.fluxpack";
        std::filesystem::create_directories(installRoot);
        writeTextFile(game / L"SkyrimSE.exe", "MZ");
        writeTextFile(game / L"Data" / L"Skyrim.esm", "master");
        const std::string fluxPackJson =
            std::string("{")
            + "\"format\":\"FluxPack\","
            + "\"formatVersion\":1,"
            + "\"build\":{"
            + "\"name\":\"Foundation Edition\","
            + "\"templateId\":\"skyrimse\","
            + "\"gameName\":\"Skyrim Special Edition\","
            + "\"gamePath\":\"" + toUtf8(game.generic_wstring()) + "\","
            + "\"defaultProfile\":\"Default\""
            + "},"
            + "\"policies\":{"
            + "\"generatedAssets\":\"confirm-before-including\""
            + "},"
            + "\"sourceArchives\":[],"
            + "\"generatedAssets\":[],"
            + "\"customPatches\":[],"
            + "\"customConfigs\":["
            + "{"
            + "\"relativePath\":\"profiles/Default/plugins.txt\","
            + "\"size\":20,"
            + "\"hash\":{\"algorithm\":\"sha256\",\"value\":\"\",\"status\":\"unavailable\"},"
            + "\"embedsText\":true,"
            + "\"text\":\"*Skyrim.esm\\n\""
            + "}"
            + "],"
            + "\"installPlan\":{"
            + "\"version\":1,"
            + "\"defaultProfile\":\"Default\","
            + "\"stages\":[{\"id\":\"source-archives\",\"title\":\"Download\",\"policy\":\"reference-only\",\"requires\":[]}],"
            + "\"profileOrder\":[],"
            + "\"targetPaths\":{}"
            + "}"
            + "}";
        writeTextFile(
            fluxPack,
            fluxPackJson);

        Logger logger;
        logger.initialize();
        AppSettingsService settings(logger);
        settings.initialize();
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        projects.initialize();
        BuildPathSettingsService pathSettings(logger);
        pathSettings.initialize();
        DownloadService downloadService(logger, settings, pathSettings);
        downloadService.initialize();
        FluxPackService service(logger, projects, downloadService, pathSettings);
        service.initialize();

        std::vector<FluxPackInstallProgress> progress;
        const FluxPackInstallResult result = service.installFluxPack(FluxPackInstallRequest{
            fluxPack,
            installRoot,
            [&progress](const FluxPackInstallProgress& update)
            {
                progress.push_back(update);
            }
        });

        EXPECT_EQ(result.buildName, L"Foundation Edition");
        EXPECT_TRUE(std::filesystem::is_regular_file(result.configPath));
        EXPECT_TRUE(std::filesystem::is_directory(result.projectDirectory));
        EXPECT_EQ(result.totalSourceCount, 0U);
        EXPECT_EQ(result.installedSourceCount, 0U);
        EXPECT_EQ(result.pendingSourceCount, 0U);
        EXPECT_EQ(result.failedSourceCount, 0U);
        EXPECT_EQ(result.appliedConfigCount, 1U);
        EXPECT_FALSE(result.hasWarnings);
        EXPECT_EQ(readTextFile(result.projectDirectory / L"profiles" / L"Default" / L"plugins.txt"), "*Skyrim.esm\n");
        ASSERT_FALSE(progress.empty());
        EXPECT_EQ(progress.back().phase, L"complete");
        EXPECT_EQ(progress.back().overallPercent, 100);

        service.shutdown();
        downloadService.shutdown();
        projects.shutdown();
        templates.shutdown();
        pathSettings.shutdown();
        settings.shutdown();
        logger.shutdown();
#endif
    }

    TEST(FluxPackServiceTests, InstallFluxPackReportsNexusDownloadFailuresAsErrorsAndCleansPlaceholder)
    {
#ifndef _WIN32
        GTEST_SKIP() << "FluxPack install project creation uses Windows game detection in this build.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        const std::filesystem::path installRoot = temp.path() / L"Installed";
        const std::filesystem::path game = temp.path() / L"Skyrim Special Edition";
        const std::filesystem::path fluxPack = temp.path() / L"Foundation.fluxpack";
        std::filesystem::create_directories(installRoot);
        writeTextFile(game / L"SkyrimSE.exe", "MZ");
        writeTextFile(game / L"Data" / L"Skyrim.esm", "master");
        const std::string fluxPackJson =
            std::string("{")
            + "\"format\":\"FluxPack\","
            + "\"formatVersion\":1,"
            + "\"build\":{"
            + "\"name\":\"Foundation Edition\","
            + "\"templateId\":\"skyrimse\","
            + "\"gameName\":\"Skyrim Special Edition\","
            + "\"gamePath\":\"" + toUtf8(game.generic_wstring()) + "\","
            + "\"defaultProfile\":\"Default\""
            + "},"
            + "\"policies\":{\"generatedAssets\":\"confirm-before-including\"},"
            + "\"sourceArchives\":[{"
            + "\"folderName\":\"Nexus Source\","
            + "\"displayName\":\"Nexus Source\","
            + "\"version\":\"1.0\","
            + "\"enabled\":true,"
            + "\"requiresDownload\":true,"
            + "\"source\":{"
            + "\"provider\":\"nexus\","
            + "\"gameDomain\":\"skyrimspecialedition\","
            + "\"remoteModId\":\"3863\","
            + "\"remoteFileId\":\"123\","
            + "\"url\":\"nxm://skyrimspecialedition/mods/3863/files/123\""
            + "}"
            + "}],"
            + "\"generatedAssets\":[],"
            + "\"customPatches\":[],"
            + "\"customConfigs\":[],"
            + "\"installPlan\":{"
            + "\"version\":1,"
            + "\"defaultProfile\":\"Default\","
            + "\"stages\":[{\"id\":\"source-archives\",\"title\":\"Download\",\"policy\":\"reference-only\",\"requires\":[]}],"
            + "\"profileOrder\":[],"
            + "\"targetPaths\":{}"
            + "}"
            + "}";
        writeTextFile(fluxPack, fluxPackJson);

        Logger logger;
        logger.initialize();
        AppSettingsService settings(logger);
        settings.initialize();
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        projects.initialize();
        BuildPathSettingsService pathSettings(logger);
        pathSettings.initialize();
        DownloadService downloadService(logger, settings, pathSettings);
        downloadService.initialize();
        FluxPackService service(logger, projects, downloadService, pathSettings);
        service.initialize();

        std::vector<FluxPackInstallProgress> progress;
        const FluxPackInstallResult result = service.installFluxPack(FluxPackInstallRequest{
            fluxPack,
            installRoot,
            [&progress](const FluxPackInstallProgress& update)
            {
                progress.push_back(update);
            }
        });

        EXPECT_EQ(result.totalSourceCount, 1U);
        EXPECT_EQ(result.installedSourceCount, 0U);
        EXPECT_EQ(result.pendingSourceCount, 0U);
        EXPECT_EQ(result.failedSourceCount, 1U);
        EXPECT_TRUE(result.hasWarnings);
        const std::filesystem::path downloadsDirectory = result.projectDirectory / L"downloads";
        if (std::filesystem::exists(downloadsDirectory))
        {
            for (const auto& entry : std::filesystem::directory_iterator(downloadsDirectory))
            {
                EXPECT_NE(entry.path().extension().wstring(), L".nxm");
                const std::wstring fileName = entry.path().filename().wstring();
                EXPECT_FALSE(fileName.size() == 11 && fileName.rfind(L".fb", 0) == 0);
            }
        }

        const FluxPackInstallProgress* failedUpdate = nullptr;
        for (const FluxPackInstallProgress& update : progress)
        {
            EXPECT_EQ(update.pendingSourceCount, 0U);
            if (update.phase == L"sources" && update.failedSourceCount == 1U)
            {
                failedUpdate = &update;
            }
        }

        ASSERT_NE(failedUpdate, nullptr);
        EXPECT_EQ(failedUpdate->currentStep, L"Ошибка загрузки");
        ASSERT_EQ(failedUpdate->providers.size(), 1U);
        EXPECT_EQ(failedUpdate->providers.front().providerId, L"nexus");
        EXPECT_EQ(failedUpdate->providers.front().pendingCount, 0U);
        EXPECT_EQ(failedUpdate->providers.front().failedCount, 1U);
        EXPECT_NE(failedUpdate->providers.front().statusText.find(L"Ошибка загрузки"), std::wstring::npos);
        EXPECT_EQ(failedUpdate->providers.front().statusText.find(L"Ожидает"), std::wstring::npos);

        service.shutdown();
        downloadService.shutdown();
        projects.shutdown();
        templates.shutdown();
        pathSettings.shutdown();
        settings.shutdown();
        logger.shutdown();
#endif
    }

    TEST(FluxPackServiceTests, InstallFluxPackUsesSourceBuildArchiveBeforeNexusDownload)
    {
#ifndef _WIN32
        GTEST_SKIP() << "FluxPack install project creation uses Windows game detection in this build.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        const std::filesystem::path sourceProject = temp.path() / L"Transferred Foundation";
        const std::filesystem::path sourceGame = sourceProject / L"stock game";
        const std::filesystem::path sourceArchive = sourceProject / L"downloads" / L"SkyUI.bsa";
        const std::filesystem::path installRoot = temp.path() / L"Installed";
        const std::filesystem::path fluxPack = temp.path() / L"Foundation.fluxpack";
        std::filesystem::create_directories(installRoot);
        writeTextFile(sourceGame / L"SkyrimSE.exe", "MZ");
        writeTextFile(sourceGame / L"Data" / L"Skyrim.esm", "master");
        writeTextFile(sourceArchive, "archive");

        const std::string fluxPackJson =
            std::string("{")
            + "\"format\":\"FluxPack\","
            + "\"formatVersion\":1,"
            + "\"build\":{"
            + "\"name\":\"Foundation Edition\","
            + "\"templateId\":\"skyrimse\","
            + "\"gameName\":\"Skyrim Special Edition\","
            + "\"gamePath\":\"" + toUtf8(sourceGame.generic_wstring()) + "\","
            + "\"projectDirectoryHint\":\"" + toUtf8(sourceProject.generic_wstring()) + "\","
            + "\"defaultProfile\":\"Default\""
            + "},"
            + "\"policies\":{\"generatedAssets\":\"confirm-before-including\"},"
            + "\"sourceArchives\":[{"
            + "\"folderName\":\"SkyUI\","
            + "\"displayName\":\"SkyUI\","
            + "\"version\":\"5.2\","
            + "\"enabled\":true,"
            + "\"archiveHash\":{\"algorithm\":\"sha256\",\"value\":\"\",\"status\":\"matched-local-download\"},"
            + "\"archiveFileName\":\"SkyUI.bsa\","
            + "\"archiveSize\":7,"
            + "\"requiresDownload\":true,"
            + "\"source\":{"
            + "\"provider\":\"nexus\","
            + "\"gameDomain\":\"skyrimspecialedition\","
            + "\"remoteModId\":\"3863\","
            + "\"remoteFileId\":\"123\","
            + "\"url\":\"nxm://skyrimspecialedition/mods/3863/files/123\","
            + "\"latestVersion\":\"5.2\""
            + "}"
            + "}],"
            + "\"generatedAssets\":[],"
            + "\"customPatches\":[],"
            + "\"customConfigs\":[],"
            + "\"installPlan\":{"
            + "\"version\":1,"
            + "\"defaultProfile\":\"Default\","
            + "\"stages\":[{\"id\":\"source-archives\",\"title\":\"Download\",\"policy\":\"reference-only\",\"requires\":[]}],"
            + "\"profileOrder\":[],"
            + "\"targetPaths\":{}"
            + "}"
            + "}";
        writeTextFile(fluxPack, fluxPackJson);

        Logger logger;
        logger.initialize();
        AppSettingsService settings(logger);
        settings.initialize();
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        projects.initialize();
        BuildPathSettingsService pathSettings(logger);
        pathSettings.initialize();
        DownloadService downloadService(logger, settings, pathSettings);
        downloadService.initialize();
        FluxPackService service(logger, projects, downloadService, pathSettings);
        service.initialize();

        std::vector<FluxPackInstallProgress> progress;
        const FluxPackInstallResult result = service.installFluxPack(FluxPackInstallRequest{
            fluxPack,
            installRoot,
            [&progress](const FluxPackInstallProgress& update)
            {
                progress.push_back(update);
            }
        });

        EXPECT_EQ(result.totalSourceCount, 1U);
        EXPECT_EQ(result.installedSourceCount, 1U);
        EXPECT_EQ(result.pendingSourceCount, 0U);
        EXPECT_EQ(result.failedSourceCount, 0U);
        EXPECT_FALSE(result.hasWarnings);

        const BuildPathSettings installedPaths = pathSettings.loadForConfig(result.configPath);
        EXPECT_TRUE(std::filesystem::is_regular_file(installedPaths.modsDirectory / L"SkyUI" / L"SkyUI.bsa"));
        EXPECT_TRUE(std::filesystem::is_regular_file(result.projectDirectory / L"downloads" / L"SkyUI.bsa"));
        EXPECT_FALSE(std::filesystem::exists(result.projectDirectory / L"downloads" / L"skyrimspecialedition-3863-123.nxm"));

        const std::vector<InstalledModRecord> installedMods =
            InstanceMetadataStore::listInstalledMods(result.projectDirectory, installedPaths.modsDirectory);
        const InstalledModRecord* skyUi = nullptr;
        for (const InstalledModRecord& mod : installedMods)
        {
            if (mod.folderName == L"SkyUI")
            {
                skyUi = &mod;
                break;
            }
        }

        ASSERT_NE(skyUi, nullptr);
        EXPECT_EQ(skyUi->source.provider, L"nexus");
        EXPECT_EQ(skyUi->source.gameDomain, L"skyrimspecialedition");
        EXPECT_EQ(skyUi->source.remoteModId, L"3863");
        EXPECT_EQ(skyUi->source.remoteFileId, L"123");
        EXPECT_EQ(skyUi->source.url, L"nxm://skyrimspecialedition/mods/3863/files/123");

        bool copiedLocalArchive = false;
        for (const FluxPackInstallProgress& update : progress)
        {
            EXPECT_EQ(update.failedSourceCount, 0U);
            if (update.currentStep == L"Копируем источник")
            {
                copiedLocalArchive = true;
            }
        }
        EXPECT_TRUE(copiedLocalArchive);

        service.shutdown();
        downloadService.shutdown();
        projects.shutdown();
        templates.shutdown();
        pathSettings.shutdown();
        settings.shutdown();
        logger.shutdown();
#endif
    }

    TEST(FluxPackServiceTests, InstallFluxPackCreatesLocalGameDirectoryWhenSourceBuildWasDeleted)
    {
#ifndef _WIN32
        GTEST_SKIP() << "FluxPack install project creation uses Windows instance metadata in this build.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        const std::filesystem::path sourceProject = temp.path() / L"Source Foundation";
        const std::filesystem::path sourceGame = sourceProject / L"stock game";
        const std::filesystem::path installRoot = temp.path() / L"Installed";
        const std::filesystem::path fluxPack = temp.path() / L"Foundation.fluxpack";
        std::filesystem::create_directories(installRoot);
        writeTextFile(sourceGame / L"SkyrimSE.exe", "MZ");
        writeTextFile(sourceGame / L"Data" / L"Skyrim.esm", "master");

        const std::string fluxPackJson =
            std::string("{")
            + "\"format\":\"FluxPack\","
            + "\"formatVersion\":1,"
            + "\"build\":{"
            + "\"name\":\"Foundation Edition\","
            + "\"templateId\":\"skyrimse\","
            + "\"gameName\":\"Skyrim Special Edition\","
            + "\"gamePath\":\"" + toUtf8(sourceGame.generic_wstring()) + "\","
            + "\"projectDirectoryHint\":\"" + toUtf8(sourceProject.generic_wstring()) + "\","
            + "\"defaultProfile\":\"Default\""
            + "},"
            + "\"policies\":{\"generatedAssets\":\"confirm-before-including\"},"
            + "\"sourceArchives\":[],"
            + "\"generatedAssets\":[],"
            + "\"customPatches\":[],"
            + "\"customConfigs\":[],"
            + "\"installPlan\":{"
            + "\"version\":1,"
            + "\"defaultProfile\":\"Default\","
            + "\"stages\":[{\"id\":\"source-archives\",\"title\":\"Download\",\"policy\":\"reference-only\",\"requires\":[]}],"
            + "\"profileOrder\":[],"
            + "\"targetPaths\":{}"
            + "}"
            + "}";
        writeTextFile(fluxPack, fluxPackJson);
        std::filesystem::remove_all(sourceProject);
        ASSERT_FALSE(std::filesystem::exists(sourceGame));

        Logger logger;
        logger.initialize();
        AppSettingsService settings(logger);
        settings.initialize();
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        projects.initialize();
        BuildPathSettingsService pathSettings(logger);
        pathSettings.initialize();
        DownloadService downloadService(logger, settings, pathSettings);
        downloadService.initialize();
        FluxPackService service(logger, projects, downloadService, pathSettings);
        service.initialize();

        const FluxPackInstallResult result = service.installFluxPack(FluxPackInstallRequest{
            fluxPack,
            installRoot,
            {}
        });

        const std::filesystem::path localGame = result.projectDirectory / L"stock game";
        EXPECT_EQ(result.buildName, L"Foundation Edition");
        EXPECT_TRUE(std::filesystem::is_regular_file(result.configPath));
        EXPECT_TRUE(std::filesystem::is_directory(localGame));
        EXPECT_FALSE(std::filesystem::exists(sourceProject));

        const BuildPathSettings savedPaths = pathSettings.loadForConfig(result.configPath);
        EXPECT_EQ(normalized(savedPaths.gameDirectory), normalized(localGame));
        const std::string manifest = readTextFile(result.configPath);
        EXPECT_NE(manifest.find("\"gamePath\":\"stock game\""), std::string::npos);
        EXPECT_NE(manifest.find("\"gameDirectory\":\"stock game\""), std::string::npos);

        service.shutdown();
        downloadService.shutdown();
        projects.shutdown();
        templates.shutdown();
        pathSettings.shutdown();
        settings.shutdown();
        logger.shutdown();
#endif
    }

    TEST(FluxPackServiceTests, ExportProjectWritesGamePathFromPathSettingsWhenManifestGamePathIsMissing)
    {
#ifndef _WIN32
        GTEST_SKIP() << "FluxPack export uses Windows game detection in this build.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        const std::filesystem::path installRoot = temp.path() / L"Builds";
        const std::filesystem::path project = installRoot / L"Foundation Edition";
        const std::filesystem::path game = project / L"Stock Game";
        const std::filesystem::path config = temp.path() / L"configs" / L"Foundation Edition.json";
        writeTextFile(game / L"SkyrimSE.exe", "MZ");
        writeTextFile(game / L"Data" / L"Skyrim.esm", "master");
        std::filesystem::create_directories(project / L"mods");
        std::filesystem::create_directories(project / L"profiles");
        std::filesystem::create_directories(project / L"downloads");
        std::filesystem::create_directories(project / L"overwrite");
        writeTextFile(
            config,
            std::string("{")
            + "\"schemaVersion\":\"1\","
            + "\"name\":\"Foundation Edition\","
            + "\"templateId\":\"skyrimse\","
            + "\"gameName\":\"Skyrim Special Edition\","
            + "\"installRoot\":\"" + toUtf8(installRoot.generic_wstring()) + "\","
            + "\"projectDirectory\":\"" + toUtf8(project.generic_wstring()) + "\","
            + "\"dataDirectory\":\"Data\","
            + "\"nexusDomain\":\"skyrimspecialedition\","
            + "\"defaultProfile\":\"Default\","
            + "\"paths\":{"
            + "\"gameDirectory\":\"Stock Game\","
            + "\"modsDirectory\":\"mods\","
            + "\"profilesDirectory\":\"profiles\","
            + "\"downloadsDirectory\":\"downloads\","
            + "\"overwriteDirectory\":\"overwrite\""
            + "}"
            + "}");

        Logger logger;
        logger.initialize();
        AppSettingsService settings(logger);
        settings.initialize();
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        projects.initialize();
        BuildPathSettingsService pathSettings(logger);
        pathSettings.initialize();
        DownloadService downloadService(logger, settings, pathSettings);
        downloadService.initialize();
        InstanceMetadataStore::ensureInstance(project, L"skyrimse");

        FluxPackService service(logger, projects, downloadService, pathSettings);
        service.initialize();

        const std::filesystem::path output = temp.path() / L"Foundation Edition.fluxpack";
        const FluxPackSummary exported = service.exportProject(FluxPackExportRequest{
            config,
            output,
            false
        });

        EXPECT_EQ(exported.buildName, L"Foundation Edition");
        const std::string manifest = readTextFile(output);
        EXPECT_NE(manifest.find("\"gamePath\":\""), std::string::npos);
        EXPECT_EQ(manifest.find("\"gamePath\":\"\""), std::string::npos);
        EXPECT_NE(manifest.find("Stock Game"), std::string::npos);

        service.shutdown();
        downloadService.shutdown();
        projects.shutdown();
        templates.shutdown();
        pathSettings.shutdown();
        settings.shutdown();
        logger.shutdown();
#endif
    }

    TEST(FluxPackServiceTests, InstallLegacyFluxPackRecoversGamePathFromProjectDirectoryHint)
    {
#ifndef _WIN32
        GTEST_SKIP() << "FluxPack install project creation uses Windows game detection in this build.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        const std::filesystem::path projectHint = temp.path() / L"Exported Foundation";
        const std::filesystem::path game = projectHint / L"Stock Game";
        const std::filesystem::path installRoot = temp.path() / L"Installed";
        const std::filesystem::path fluxPack = temp.path() / L"Legacy.fluxpack";
        std::filesystem::create_directories(installRoot);
        writeTextFile(game / L"SkyrimSE.exe", "MZ");
        writeTextFile(game / L"Data" / L"Skyrim.esm", "master");
        std::filesystem::create_directories(projectHint / L"mods");
        std::filesystem::create_directories(projectHint / L"profiles");
        std::filesystem::create_directories(projectHint / L"downloads");
        std::filesystem::create_directories(projectHint / L"overwrite");

        const std::string fluxPackJson =
            std::string("{")
            + "\"format\":\"FluxPack\","
            + "\"formatVersion\":1,"
            + "\"build\":{"
            + "\"name\":\"Foundation Edition\","
            + "\"templateId\":\"skyrimse\","
            + "\"gameName\":\"Skyrim Special Edition\","
            + "\"projectDirectoryHint\":\"" + toUtf8(projectHint.generic_wstring()) + "\","
            + "\"defaultProfile\":\"Default\""
            + "},"
            + "\"policies\":{\"generatedAssets\":\"confirm-before-including\"},"
            + "\"sourceArchives\":[],"
            + "\"generatedAssets\":[],"
            + "\"customPatches\":[],"
            + "\"customConfigs\":[],"
            + "\"installPlan\":{"
            + "\"version\":1,"
            + "\"defaultProfile\":\"Default\","
            + "\"stages\":[{\"id\":\"source-archives\",\"title\":\"Download\",\"policy\":\"reference-only\",\"requires\":[]}],"
            + "\"profileOrder\":[],"
            + "\"targetPaths\":{}"
            + "}"
            + "}";
        writeTextFile(fluxPack, fluxPackJson);

        Logger logger;
        logger.initialize();
        AppSettingsService settings(logger);
        settings.initialize();
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        projects.initialize();
        BuildPathSettingsService pathSettings(logger);
        pathSettings.initialize();
        DownloadService downloadService(logger, settings, pathSettings);
        downloadService.initialize();
        FluxPackService service(logger, projects, downloadService, pathSettings);
        service.initialize();

        const FluxPackInstallResult result = service.installFluxPack(FluxPackInstallRequest{
            fluxPack,
            installRoot,
            {}
        });

        EXPECT_EQ(result.buildName, L"Foundation Edition");
        EXPECT_TRUE(std::filesystem::is_regular_file(result.configPath));
        const std::filesystem::path localGame = result.projectDirectory / L"stock game";
        EXPECT_TRUE(std::filesystem::is_directory(localGame));
        const BuildPathSettings savedPaths = pathSettings.loadForConfig(result.configPath);
        EXPECT_EQ(normalized(savedPaths.gameDirectory), normalized(localGame));
        const std::string manifest = readTextFile(result.configPath);
        EXPECT_NE(manifest.find("\"gamePath\":\"stock game\""), std::string::npos);
        EXPECT_EQ(manifest.find(toUtf8(projectHint.generic_wstring())), std::string::npos);

        service.shutdown();
        downloadService.shutdown();
        projects.shutdown();
        templates.shutdown();
        pathSettings.shutdown();
        settings.shutdown();
        logger.shutdown();
#endif
    }
}
