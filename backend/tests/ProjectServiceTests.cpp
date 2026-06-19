#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Services/ProjectService.hpp"
#include "FluxoraCore/Services/TemplateService.hpp"
#include "FluxoraCore/Storage/AtomicFileStore.hpp"
#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"
#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>

namespace fluxora::tests
{
    namespace
    {
        constexpr const char* LegacySkyrimManifest = R"json({
            "schemaVersion": "1",
            "name": "Legacy Skyrim Build",
            "templateId": "skyrimse",
            "gameName": "Skyrim Special Edition",
            "gamePath": "Game",
            "installRoot": "..",
            "projectDirectory": ".",
            "dataDirectory": "Data",
            "defaultProfile": "Default"
        })json";

        constexpr const char* LegacySkyrimManifestWithoutTemplateId = R"json({
            "schemaVersion": "1",
            "name": "Legacy Skyrim Build",
            "gameName": "Skyrim Special Edition",
            "gamePath": "Game",
            "installRoot": "..",
            "projectDirectory": ".",
            "dataDirectory": "Data",
            "defaultProfile": "Default"
        })json";

        constexpr const char* LegacySkyrimExecutableManifestWithoutTemplateId = R"json({
            "schemaVersion": "1",
            "name": "Legacy Skyrim Build",
            "gameName": "Skyrim Special Edition",
            "gamePath": "Game/SkyrimSE.exe",
            "installRoot": "..",
            "projectDirectory": ".",
            "dataDirectory": "Data",
            "defaultProfile": "Default"
        })json";
    }

    TEST(ProjectServiceTests, CreateSkyrimProjectSeedsProfileAndManifestFromSupportRules)
    {
#ifndef _WIN32
        GTEST_SKIP() << "Project creation initializes the Windows instance metadata store.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        const std::filesystem::path game = temp.path() / L"Skyrim Special Edition";
        writeTextFile(game / L"SkyrimSE.exe", "MZ");
        writeTextFile(game / L"Data" / L"Skyrim.esm", "master");
        const std::filesystem::path installRoot = temp.path() / L"Builds";
        std::filesystem::create_directories(installRoot);

        Logger logger;
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        projects.initialize();

        const ProjectDescriptor project = projects.createProject(ProjectCreateRequest{
            L"Skyrim Build",
            L"skyrimse",
            game,
            installRoot
        });

        const std::filesystem::path profile =
            project.projectDirectory / L"profiles" / L"Default";
        const std::string plugins = readTextFile(profile / L"plugins.txt");
        const std::string loadOrder = readTextFile(profile / L"loadorder.txt");
        EXPECT_NE(plugins.find("*Skyrim.esm\n"), std::string::npos);
        EXPECT_NE(plugins.find("*Update.esm\n"), std::string::npos);
        EXPECT_NE(plugins.find("*Dawnguard.esm\n"), std::string::npos);
        EXPECT_NE(plugins.find("*HearthFires.esm\n"), std::string::npos);
        EXPECT_NE(plugins.find("*Dragonborn.esm\n"), std::string::npos);
        EXPECT_NE(loadOrder.find("Skyrim.esm\n"), std::string::npos);
        EXPECT_NE(loadOrder.find("Dragonborn.esm\n"), std::string::npos);

        const std::string manifest = readTextFile(project.configPath);
        EXPECT_NE(manifest.find("\"templateId\":\"skyrimse\""), std::string::npos);
        EXPECT_NE(manifest.find("\"dataDirectory\":\"Data\""), std::string::npos);
        EXPECT_NE(manifest.find("\"pluginExtensions\":[\".esm\",\".esp\",\".esl\"]"), std::string::npos);
        EXPECT_NE(manifest.find("\"loaderExecutable\":\"skse64_loader.exe\""), std::string::npos);
        EXPECT_NE(manifest.find("\"executablePath\":\"SkyrimSE.exe\""), std::string::npos);
        EXPECT_NE(manifest.find("\"gameId\":\"skyrimse\""), std::string::npos);
        EXPECT_NE(manifest.find("\"gameDisplayName\":\"Skyrim Special Edition\""), std::string::npos);
        EXPECT_NE(manifest.find("\"projectFingerprint\""), std::string::npos);
        EXPECT_NE(manifest.find("\"detectionSource\":\"manual-path\""), std::string::npos);
        EXPECT_NE(manifest.find("\"detectionConfidence\":\"explicit\""), std::string::npos);
        EXPECT_NE(manifest.find("\"healthStatusAtCreation\":\"warning\""), std::string::npos);
        EXPECT_NE(manifest.find("\"selectedExecutable\":\"SkyrimSE.exe\""), std::string::npos);

        const ProjectOpenResult opened = projects.openProjectConfig(project.configPath);
        ASSERT_TRUE(opened.project.fingerprint.has_value());
        EXPECT_EQ(opened.project.fingerprint->gameId, L"skyrimse");
        EXPECT_EQ(opened.project.fingerprint->gameDisplayName, L"Skyrim Special Edition");
        EXPECT_EQ(opened.project.fingerprint->selectedExecutable, std::filesystem::path(L"SkyrimSE.exe"));
#endif
    }

    TEST(ProjectServiceTests, CreateSkyrimProjectRejectsMissingRequiredFiles)
    {
#ifndef _WIN32
        GTEST_SKIP() << "Project creation initializes the Windows instance metadata store.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        const std::filesystem::path game = temp.path() / L"Skyrim Special Edition";
        std::filesystem::create_directories(game / L"Data");
        const std::filesystem::path installRoot = temp.path() / L"Builds";
        std::filesystem::create_directories(installRoot);

        Logger logger;
        Logger::setOperationId(L"project-create-bad-path-test");
        logger.initialize();
        const std::filesystem::path operationsLogPath = logger.operationsLogPath();
        ASSERT_FALSE(operationsLogPath.empty());
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        projects.initialize();

        try
        {
            (void)projects.createProject(ProjectCreateRequest{
                L"Broken Skyrim Build",
                L"skyrimse",
                game,
                installRoot
            });
            FAIL() << "Expected project creation to reject the broken Skyrim path.";
        }
        catch (const std::invalid_argument& exception)
        {
            const std::string message = exception.what();
            EXPECT_NE(message.find("Game install is broken and cannot be used."), std::string::npos);
            EXPECT_NE(message.find("Required game file is missing: SkyrimSE.exe"), std::string::npos);
        }

        logger.shutdown();
        Logger::clearOperationId();

        const std::string operationsLog = readTextFile(operationsLogPath);
        EXPECT_NE(operationsLog.find("operationId=project-create-bad-path-test"), std::string::npos);
        EXPECT_NE(operationsLog.find("createProject blocked"), std::string::npos);
        EXPECT_NE(operationsLog.find("healthResult=\"broken\""), std::string::npos);
        EXPECT_NE(operationsLog.find("missingFiles=\""), std::string::npos);
        EXPECT_NE(operationsLog.find("SkyrimSE.exe"), std::string::npos);
        EXPECT_NE(operationsLog.find("Data/Skyrim.esm"), std::string::npos);
#endif
    }

    TEST(ProjectServiceTests, CreateProjectRejectsUnsafeInstallRoot)
    {
#ifndef _WIN32
        GTEST_SKIP() << "Project creation initializes the Windows instance metadata store.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        const std::filesystem::path game = temp.path() / L"Skyrim Special Edition";
        writeTextFile(game / L"SkyrimSE.exe", "MZ");
        writeTextFile(game / L"Data" / L"Skyrim.esm", "master");

        std::wstring windows(MAX_PATH, L'\0');
        const UINT length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
        if (length == 0 || length >= windows.size())
        {
            GTEST_SKIP() << "Windows directory could not be resolved.";
        }
        windows.resize(length);

        Logger logger;
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        projects.initialize();

        try
        {
            (void)projects.createProject(ProjectCreateRequest{
                L"Unsafe Root Build",
                L"skyrimse",
                game,
                std::filesystem::path(windows)
            });
            FAIL() << "Expected project creation to reject an unsafe install root.";
        }
        catch (const std::invalid_argument& exception)
        {
            const std::string message = exception.what();
            EXPECT_NE(message.find("Install root directory is unsafe"), std::string::npos);
            EXPECT_NE(message.find("Writes to system folders are blocked"), std::string::npos);
        }

        EXPECT_FALSE(std::filesystem::exists(temp.path() / L"AppData" / L"Fluxora" / L"Builds"));
#endif
    }

    TEST(ProjectServiceTests, OpenLegacySkyrimManifestPreservesPluginAndLoadOrderState)
    {
#ifndef _WIN32
        GTEST_SKIP() << "Opening a project initializes the Windows instance metadata store.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        const std::filesystem::path projectDirectory = temp.path() / L"Legacy Skyrim Build";
        const std::filesystem::path profile = projectDirectory / L"profiles" / L"Default";
        const std::filesystem::path pluginsPath = profile / L"plugins.txt";
        const std::filesystem::path loadOrderPath = profile / L"loadorder.txt";
        writeTextFile(projectDirectory / L"Game" / L"SkyrimSE.exe", "MZ");
        writeTextFile(projectDirectory / L"Game" / L"Data" / L"Skyrim.esm", "master");
        writeTextFile(pluginsPath, "*Skyrim.esm\n*SkyUI_SE.esp\n");
        writeTextFile(loadOrderPath, "SkyUI_SE.esp\nSkyrim.esm\n");
        const std::filesystem::path configPath = projectDirectory / L"legacy.build.json";
        writeTextFile(configPath, std::string(LegacySkyrimManifest));

        Logger logger;
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        projects.initialize();

        const ProjectOpenResult opened = projects.openProjectConfig(configPath);

        EXPECT_EQ(opened.project.templateId, L"skyrimse");
        EXPECT_EQ(opened.resolvedTemplate.id, L"skyrimse");
        ASSERT_TRUE(opened.project.fingerprint.has_value());
        EXPECT_EQ(opened.project.fingerprint->gameId, L"skyrimse");
        EXPECT_EQ(InstanceMetadataStore::gameId(projectDirectory), L"skyrimse");
        EXPECT_EQ(readTextFile(pluginsPath), "*Skyrim.esm\n*SkyUI_SE.esp\n");
        EXPECT_EQ(readTextFile(loadOrderPath), "SkyUI_SE.esp\nSkyrim.esm\n");

        const std::string migratedManifest = readTextFile(configPath);
        EXPECT_NE(migratedManifest.find("\"gameId\":\"skyrimse\""), std::string::npos);
        EXPECT_NE(migratedManifest.find("\"projectFingerprint\""), std::string::npos);
#endif
    }

    TEST(ProjectServiceTests, OpenLegacySkyrimManifestWithoutTemplateIdMigratesTypedGameFields)
    {
#ifndef _WIN32
        GTEST_SKIP() << "Opening a project initializes the Windows instance metadata store.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        const std::filesystem::path projectDirectory = temp.path() / L"Legacy Skyrim Build";
        const std::filesystem::path profile = projectDirectory / L"profiles" / L"Default";
        const std::filesystem::path pluginsPath = profile / L"plugins.txt";
        const std::filesystem::path loadOrderPath = profile / L"loadorder.txt";
        writeTextFile(projectDirectory / L"Game" / L"SkyrimSE.exe", "MZ");
        writeTextFile(projectDirectory / L"Game" / L"Data" / L"Skyrim.esm", "master");
        writeTextFile(pluginsPath, "*Skyrim.esm\n*SkyUI_SE.esp\n");
        writeTextFile(loadOrderPath, "SkyUI_SE.esp\nSkyrim.esm\n");
        const std::filesystem::path configPath = projectDirectory / L"legacy.build.json";
        writeTextFile(configPath, std::string(LegacySkyrimManifestWithoutTemplateId));

        Logger logger;
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        projects.initialize();

        const ProjectOpenResult opened = projects.openProjectConfig(configPath);

        EXPECT_EQ(opened.project.templateId, L"skyrimse");
        EXPECT_EQ(opened.resolvedTemplate.id, L"skyrimse");
        ASSERT_TRUE(opened.project.fingerprint.has_value());
        EXPECT_EQ(opened.project.fingerprint->gameId, L"skyrimse");
        EXPECT_EQ(InstanceMetadataStore::gameId(projectDirectory), L"skyrimse");
        EXPECT_EQ(readTextFile(pluginsPath), "*Skyrim.esm\n*SkyUI_SE.esp\n");
        EXPECT_EQ(readTextFile(loadOrderPath), "SkyUI_SE.esp\nSkyrim.esm\n");

        const std::string migratedManifest = readTextFile(configPath);
        EXPECT_NE(migratedManifest.find("\"templateId\":\"skyrimse\""), std::string::npos);
        EXPECT_NE(migratedManifest.find("\"gameId\":\"skyrimse\""), std::string::npos);
        EXPECT_NE(migratedManifest.find("\"projectFingerprint\""), std::string::npos);

        const std::filesystem::path backupPath = AtomicFileStore::backupPathFor(configPath);
        ASSERT_TRUE(std::filesystem::exists(backupPath));
        const std::string backupManifest = readTextFile(backupPath);
        EXPECT_EQ(backupManifest.find("\"gameId\":\"skyrimse\""), std::string::npos);
        EXPECT_EQ(backupManifest.find("\"projectFingerprint\""), std::string::npos);
#endif
    }

    TEST(ProjectServiceTests, OpenLegacySkyrimManifestWithBrokenHealthRecordsBlockingFingerprint)
    {
#ifndef _WIN32
        GTEST_SKIP() << "Opening a project initializes the Windows instance metadata store.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        const std::filesystem::path projectDirectory = temp.path() / L"Legacy Skyrim Build";
        writeTextFile(projectDirectory / L"Game" / L"SkyrimSE.exe", "MZ");
        const std::filesystem::path configPath = projectDirectory / L"legacy.build.json";
        writeTextFile(configPath, std::string(LegacySkyrimExecutableManifestWithoutTemplateId));

        Logger logger;
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        projects.initialize();

        const ProjectOpenResult opened = projects.openProjectConfig(configPath);

        EXPECT_EQ(opened.project.templateId, L"skyrimse");
        ASSERT_TRUE(opened.project.fingerprint.has_value());
        EXPECT_EQ(opened.project.fingerprint->gameId, L"skyrimse");
        EXPECT_EQ(opened.project.fingerprint->healthStatusAtCreation, L"broken");

        const std::string migratedManifest = readTextFile(configPath);
        EXPECT_NE(migratedManifest.find("\"gameId\":\"skyrimse\""), std::string::npos);
        EXPECT_NE(migratedManifest.find("\"healthStatusAtCreation\":\"broken\""), std::string::npos);
        EXPECT_EQ(migratedManifest.find("\"healthStatusAtCreation\":\"healthy\""), std::string::npos);

        const std::filesystem::path backupPath = AtomicFileStore::backupPathFor(configPath);
        ASSERT_TRUE(std::filesystem::exists(backupPath));
        const std::string backupManifest = readTextFile(backupPath);
        EXPECT_EQ(backupManifest.find("\"projectFingerprint\""), std::string::npos);
#endif
    }

    TEST(ProjectServiceTests, OpenProjectConfigRecoversInterruptedManifestWrite)
    {
#ifndef _WIN32
        GTEST_SKIP() << "Opening a project initializes the Windows instance metadata store.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        const std::filesystem::path projectDirectory = temp.path() / L"Recovered Skyrim Build";
        const std::filesystem::path configPath = projectDirectory / L"fluxora.build.json";
        writeTextFile(projectDirectory / L"Game" / L"SkyrimSE.exe", "MZ");
        writeTextFile(projectDirectory / L"Game" / L"Data" / L"Skyrim.esm", "master");
        writeTextFile(projectDirectory / L"profiles" / L"Default" / L"plugins.txt", "*Skyrim.esm\n");
        writeTextFile(projectDirectory / L"profiles" / L"Default" / L"loadorder.txt", "Skyrim.esm\n");

        AtomicFileStore store;
        store.writeTextFile(
            configPath,
            std::string(LegacySkyrimManifest),
            AtomicFileWriteOptions{
                L"project manifest",
                ProjectStateValidation::JsonObject
            });
        EXPECT_THROW(
            store.writeTextFile(
                configPath,
                R"json({
                    "schemaVersion": "1",
                    "name": "Interrupted New Name",
                    "templateId": "skyrimse",
                    "gameName": "Skyrim Special Edition",
                    "gamePath": "Game",
                    "installRoot": "..",
                    "projectDirectory": "."
                })json",
                AtomicFileWriteOptions{
                    L"project manifest",
                    ProjectStateValidation::JsonObject,
                    {},
                    true,
                    AtomicWriteFailurePoint::AfterTempFileValidated
                }),
            std::runtime_error);

        Logger logger;
        TemplateService templates(logger);
        templates.initialize();
        ProjectService projects(logger, templates);
        projects.initialize();

        const ProjectOpenResult opened = projects.openProjectConfig(configPath);

        EXPECT_EQ(opened.project.name, L"Legacy Skyrim Build");
        EXPECT_EQ(readTextFile(configPath).find("Interrupted New Name"), std::string::npos);
        for (const auto& entry : std::filesystem::directory_iterator(configPath.parent_path()))
        {
            EXPECT_FALSE(AtomicFileStore::isManagedTempFileFor(configPath, entry.path()))
                << entry.path().string();
        }
#endif
    }
}
