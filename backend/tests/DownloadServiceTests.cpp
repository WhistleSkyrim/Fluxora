#include "FluxoraCore/Services/AppSettingsService.hpp"
#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/DownloadService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fluxora::tests
{
    TEST(DownloadServiceTests, CaptureNxmLinkWithoutDownloadKeyUsesAuthenticatedDownloadPath)
    {
#ifndef _WIN32
        GTEST_SKIP() << "Nexus downloads are implemented for Windows builds.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        Logger logger;
        logger.initialize();
        AppSettingsService settings(logger);
        settings.initialize();
        BuildPathSettingsService pathSettings(logger);
        pathSettings.initialize();
        DownloadService downloads(logger, settings, pathSettings);
        downloads.initialize();

        const std::filesystem::path projectDirectory = temp.path() / L"Project";
        const std::vector<DownloadEntry> entries = downloads.captureNxmLinks(
            projectDirectory,
            {L"nxm://skyrimspecialedition/mods/3863/files/123"});

        ASSERT_EQ(entries.size(), 1U);
        EXPECT_FALSE(entries.front().canInstall);
        EXPECT_TRUE(entries.front().localPath.extension() == L".nxm");
        EXPECT_NE(entries.front().status.find(L"NexusMods account is not linked"), std::wstring::npos);

        downloads.shutdown();
        pathSettings.shutdown();
        settings.shutdown();
        logger.shutdown();
#endif
    }

    TEST(DownloadServiceTests, CaptureNxmLinkWithOAuthAuthWithoutApiKeyUsesOAuthTokenPath)
    {
#ifndef _WIN32
        GTEST_SKIP() << "Nexus downloads are implemented for Windows builds.";
#else
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        Logger logger;
        logger.initialize();
        AppSettingsService settings(logger);
        settings.initialize();
        NexusModsStoredAuth auth;
        auth.linked = true;
        auth.username = L"modder";
        auth.userId = L"42";
        auth.tokenType = L"Bearer";
        auth.expiresAtUtc = L"2026-06-16T10:00:00Z";
        auth.protectedAccessToken = L"legacy-access-token";
        settings.saveNexusModsAuth(auth);

        BuildPathSettingsService pathSettings(logger);
        pathSettings.initialize();
        DownloadService downloads(logger, settings, pathSettings);
        downloads.initialize();

        const std::filesystem::path projectDirectory = temp.path() / L"Project";
        const std::vector<DownloadEntry> entries = downloads.captureNxmLinks(
            projectDirectory,
            {L"nxm://skyrimspecialedition/mods/3863/files/123"});

        ASSERT_EQ(entries.size(), 1U);
        EXPECT_FALSE(entries.front().canInstall);
        EXPECT_TRUE(entries.front().localPath.extension() == L".nxm");
        EXPECT_NE(entries.front().status.find(L"Invalid protected NexusMods OAuth token"), std::wstring::npos);

        downloads.shutdown();
        pathSettings.shutdown();
        settings.shutdown();
        logger.shutdown();
#endif
    }

    TEST(DownloadServiceTests, ListDownloadsSkipsAtomicBackupFiles)
    {
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", (temp.path() / L"AppData").wstring());

        Logger logger;
        logger.initialize();
        AppSettingsService settings(logger);
        settings.initialize();
        BuildPathSettingsService pathSettings(logger);
        pathSettings.initialize();
        DownloadService downloads(logger, settings, pathSettings);
        downloads.initialize();

        const std::filesystem::path projectDirectory = temp.path() / L"Project";
        const std::filesystem::path downloadsDirectory = projectDirectory / L"downloads";
        writeTextFile(downloadsDirectory / L"Ready.zip", "archive");
        writeTextFile(downloadsDirectory / L".fb1234abcd", "nxm://skyrimspecialedition/mods/3863/files/123");

        const std::vector<DownloadEntry> entries = downloads.listDownloads(projectDirectory);

        ASSERT_EQ(entries.size(), 1U);
        EXPECT_EQ(entries.front().fileName, L"Ready.zip");

        downloads.shutdown();
        pathSettings.shutdown();
        settings.shutdown();
        logger.shutdown();
    }
}
