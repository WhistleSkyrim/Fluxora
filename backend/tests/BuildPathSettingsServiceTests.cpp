#include "FluxoraCore/Services/BuildPathSettingsService.hpp"

#include "FluxoraCore/Services/Logger.hpp"
#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fluxora::tests
{
    namespace
    {
        void expectSamePath(
            const std::filesystem::path& actual,
            const std::filesystem::path& expected)
        {
            EXPECT_EQ(normalized(actual), normalized(expected));
        }
    }

    TEST(BuildPathSettingsServiceTests, LoadForProjectDirectoryUsesDefaultsAndLocalGameFolder)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / L"Foundation Edition";
        writeTextFile(project / L"stock game" / L"SkyrimSE.exe", "MZ");
        writeTextFile(project / L"stock game" / L"Data" / L"Skyrim.esm", "master");

        Logger logger;
        BuildPathSettingsService service(logger);

        const BuildPathSettings settings = service.loadForProjectDirectory(project);

        expectSamePath(settings.gameDirectory, project / L"stock game");
        expectSamePath(settings.modsDirectory, project / L"mods");
        expectSamePath(settings.profilesDirectory, project / L"profiles");
        expectSamePath(settings.downloadsDirectory, project / L"downloads");
        expectSamePath(settings.overwriteDirectory, project / L"overwrite");
    }

    TEST(BuildPathSettingsServiceTests, LoadForProjectDirectoryAppliesLocalPathOverrides)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / L"Foundation Edition";
        writeTextFile(project / L"GameDir" / L"SkyrimSE.exe", "MZ");
        writeTextFile(project / L"GameDir" / L"Data" / L"Skyrim.esm", "master");
        writeTextFile(
            project / L".fluxora" / L"paths.json",
            "{"
            "\"gameDirectory\":\"GameDir\","
            "\"modsDirectory\":\"Custom Mods\","
            "\"profilesDirectory\":\"Custom Profiles\","
            "\"downloadsDirectory\":\"Custom Downloads\","
            "\"overwriteDirectory\":\"Custom Overwrite\""
            "}");

        Logger logger;
        BuildPathSettingsService service(logger);

        const BuildPathSettings settings = service.loadForProjectDirectory(project);

        expectSamePath(settings.gameDirectory, project / L"GameDir");
        expectSamePath(settings.modsDirectory, project / L"Custom Mods");
        expectSamePath(settings.profilesDirectory, project / L"Custom Profiles");
        expectSamePath(settings.downloadsDirectory, project / L"Custom Downloads");
        expectSamePath(settings.overwriteDirectory, project / L"Custom Overwrite");
    }

    TEST(BuildPathSettingsServiceTests, SaveForConfigPersistsPathsAndCreatesMissingDirectories)
    {
        TempDirectory temp;
        const std::filesystem::path configDirectory = temp.path() / L"configs";
        const std::filesystem::path project = temp.path() / L"Foundation Edition";
        const std::filesystem::path config = configDirectory / L"foundation.json";
        writeTextFile(project / L"stock game" / L"SkyrimSE.exe", "MZ");
        writeTextFile(project / L"stock game" / L"Data" / L"Skyrim.esm", "master");
        writeTextFile(
            config,
            "{"
            "\"id\":\"foundation\","
            "\"projectDirectory\":\"../Foundation Edition\","
            "\"gamePath\":\"stock game\""
            "}");

        Logger logger;
        BuildPathSettingsService service(logger);
        const BuildPathSettings requested{
            project / L"stock game",
            project / L"Mod Storage",
            project / L"Profile Storage",
            project / L"Download Storage",
            project / L"Overwrite Storage"
        };

        const BuildPathSettings saved = service.saveForConfig(config, requested);
        const BuildPathSettings loaded = service.loadForConfig(config);

        expectSamePath(saved.gameDirectory, requested.gameDirectory);
        expectSamePath(loaded.gameDirectory, requested.gameDirectory);
        expectSamePath(loaded.modsDirectory, requested.modsDirectory);
        expectSamePath(loaded.profilesDirectory, requested.profilesDirectory);
        expectSamePath(loaded.downloadsDirectory, requested.downloadsDirectory);
        expectSamePath(loaded.overwriteDirectory, requested.overwriteDirectory);

        EXPECT_TRUE(std::filesystem::is_directory(requested.modsDirectory));
        EXPECT_TRUE(std::filesystem::is_directory(requested.profilesDirectory));
        EXPECT_TRUE(std::filesystem::is_directory(requested.downloadsDirectory));
        EXPECT_TRUE(std::filesystem::is_directory(requested.overwriteDirectory));

        const std::string localSettings = readTextFile(project / L".fluxora" / L"paths.json");
        EXPECT_NE(localSettings.find("Mod Storage"), std::string::npos);
        EXPECT_NE(localSettings.find("Profile Storage"), std::string::npos);

        const std::string manifest = readTextFile(config);
        EXPECT_NE(manifest.find("\"paths\""), std::string::npos);
        EXPECT_NE(manifest.find("stock game"), std::string::npos);
    }

    TEST(BuildPathSettingsServiceTests, LoadForConfigRejectsMissingConfig)
    {
        TempDirectory temp;
        Logger logger;
        BuildPathSettingsService service(logger);

        EXPECT_THROW(
            (void)service.loadForConfig(temp.path() / L"missing.json"),
            std::invalid_argument);
    }

#ifdef _WIN32
    TEST(BuildPathSettingsServiceTests, SaveForConfigRejectsSystemFolderOverrides)
    {
        std::wstring windows(MAX_PATH, L'\0');
        const UINT length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
        if (length == 0 || length >= windows.size())
        {
            GTEST_SKIP() << "Windows directory could not be resolved.";
        }
        windows.resize(length);

        TempDirectory temp;
        const std::filesystem::path configDirectory = temp.path() / L"configs";
        const std::filesystem::path project = temp.path() / L"Foundation Edition";
        const std::filesystem::path config = configDirectory / L"foundation.json";
        writeTextFile(project / L"stock game" / L"SkyrimSE.exe", "MZ");
        writeTextFile(project / L"stock game" / L"Data" / L"Skyrim.esm", "master");
        writeTextFile(
            config,
            "{"
            "\"id\":\"foundation\","
            "\"projectDirectory\":\"../Foundation Edition\","
            "\"gamePath\":\"stock game\""
            "}");

        Logger logger;
        BuildPathSettingsService service(logger);
        const BuildPathSettings requested{
            project / L"stock game",
            std::filesystem::path(windows),
            project / L"Profile Storage",
            project / L"Download Storage",
            project / L"Overwrite Storage"
        };

        EXPECT_THROW(
            (void)service.saveForConfig(config, requested),
            std::invalid_argument);
    }
#endif
}
