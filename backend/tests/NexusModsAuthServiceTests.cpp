#include "FluxoraCore/Services/AppSettingsService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Services/NexusModsAuthService.hpp"
#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

namespace fluxora::tests
{
    TEST(NexusModsAuthServiceTests, OAuthTokenWithoutLegacyApiKeyIsLinked)
    {
        TempDirectory temp;
        ScopedEnvironmentVariable appData(L"APPDATA", temp.path().wstring());
        ScopedEnvironmentVariable clientId(L"FLUXORA_NEXUS_CLIENT_ID", L"fluxora-test-client");

        Logger logger;
        AppSettingsService settings(logger);
        settings.initialize();

        NexusModsStoredAuth auth;
        auth.linked = true;
        auth.username = L"modder";
        auth.userId = L"42";
        auth.tokenType = L"Bearer";
        auth.expiresAtUtc = L"2026-06-16T10:00:00Z";
        auth.protectedAccessToken = L"protected-access-token";
        settings.saveNexusModsAuth(auth);

        NexusModsAuthService service(logger, settings);
        const NexusModsAuthStatus status = service.status();

        EXPECT_TRUE(status.isConfigured);
        EXPECT_TRUE(status.isLinked);
        EXPECT_EQ(status.clientId, L"fluxora-test-client");
        EXPECT_EQ(status.displayName, L"modder");
        EXPECT_EQ(status.message, L"NexusMods привязан: modder");

        settings.shutdown();
    }
}
