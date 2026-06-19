#include "FluxoraCore/Services/AppSettingsService.hpp"

#include "FluxoraCore/Services/Logger.hpp"
#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

namespace fluxora::tests
{
    TEST(AppSettingsServiceTests, SavesAndLoadsNormalizedLanguageCode)
    {
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", temp.path().wstring());
        Logger logger;
        AppSettingsService service(logger);
        service.initialize();

        service.saveLanguageCode(L"  RU_ru  ");

        EXPECT_EQ(service.loadLanguageCode(), L"ru-ru");
        EXPECT_EQ(normalized(service.appConfigPath()), normalized(temp.path() / L"Fluxora" / L"settings.ini"));
        EXPECT_NE(readTextFile(service.appConfigPath()).find("LANGUAGE=ru-ru"), std::string::npos);
    }

    TEST(AppSettingsServiceTests, MigratesLegacyLanguagesKey)
    {
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", temp.path().wstring());
        writeTextFile(temp.path() / L"Fluxora" / L"settings.ini", "LANGUAGES=DE_de\n");

        Logger logger;
        AppSettingsService service(logger);
        service.initialize();

        EXPECT_EQ(service.loadLanguageCode(), L"de-de");

        const std::string content = readTextFile(service.appConfigPath());
        EXPECT_EQ(content.find("LANGUAGES="), std::string::npos);
        EXPECT_NE(content.find("LANGUAGE=de-de"), std::string::npos);
    }

    TEST(AppSettingsServiceTests, SavesLoadsAndClearsNexusModsAuth)
    {
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", temp.path().wstring());
        Logger logger;
        AppSettingsService service(logger);
        service.initialize();

        NexusModsStoredAuth saved;
        saved.linked = true;
        saved.username = L"modder";
        saved.userId = L"42";
        saved.tokenType = L"Bearer";
        saved.expiresAtUtc = L"2026-06-16T10:00:00Z";
        saved.protectedAccessToken = L"access-token";
        saved.protectedRefreshToken = L"refresh-token";
        saved.protectedApiKey = L"api-key";

        service.saveNexusModsAuth(saved);
        const NexusModsStoredAuth loaded = service.loadNexusModsAuth();

        EXPECT_TRUE(loaded.linked);
        EXPECT_EQ(loaded.username, saved.username);
        EXPECT_EQ(loaded.userId, saved.userId);
        EXPECT_EQ(loaded.tokenType, saved.tokenType);
        EXPECT_EQ(loaded.expiresAtUtc, saved.expiresAtUtc);
        EXPECT_EQ(loaded.protectedAccessToken, saved.protectedAccessToken);
        EXPECT_EQ(loaded.protectedRefreshToken, saved.protectedRefreshToken);
        EXPECT_EQ(loaded.protectedApiKey, saved.protectedApiKey);

        service.clearNexusModsAuth();
        const NexusModsStoredAuth cleared = service.loadNexusModsAuth();
        EXPECT_FALSE(cleared.linked);
        EXPECT_TRUE(cleared.protectedAccessToken.empty());
        EXPECT_TRUE(cleared.protectedApiKey.empty());
    }

    TEST(AppSettingsServiceTests, InvalidAuthJsonReturnsEmptyAuth)
    {
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", temp.path().wstring());
        Logger logger;
        AppSettingsService service(logger);
        service.initialize();
        writeTextFile(service.settingsPath(), "{not-json");

        const NexusModsStoredAuth loaded = service.loadNexusModsAuth();

        EXPECT_FALSE(loaded.linked);
        EXPECT_TRUE(loaded.username.empty());
        EXPECT_TRUE(loaded.protectedAccessToken.empty());
    }
}
