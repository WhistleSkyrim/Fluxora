#include "FluxoraCore/Services/BuildPathSettingsService.hpp"
#include "FluxoraCore/Services/ExecutableIconService.hpp"
#include "FluxoraCore/Services/ExecutableService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "FluxoraCore/Storage/InstanceMetadataStore.hpp"

#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

namespace fluxora::tests
{
    namespace
    {
        void writeExecutableStub(const std::filesystem::path& path)
        {
            writeTextFile(path, "MZ executable stub");
        }
    }

    TEST(ExecutableServiceTests, RootBuilderLaunchCacheMaterializesEarlyDataRuntimeDirectories)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / L"Imported Build";
        const std::filesystem::path config = project / L"build.json";
        const std::filesystem::path game = project / L"Stock Game";
        const std::filesystem::path mods = project / L"mods";
        const std::filesystem::path overwrite = project / L"overwrite";

        writeExecutableStub(game / L"SkyrimSE.exe");
        std::filesystem::create_directories(game / L"Data");

        const std::filesystem::path skseMod = mods / L"Skyrim Script Extender";
        const std::filesystem::path runtimeLow = mods / L"Runtime Low";
        const std::filesystem::path runtimeHigh = mods / L"Runtime High";
        writeExecutableStub(skseMod / L"root" / L"skse64_loader.exe");
        writeTextFile(runtimeLow / L"SKSE" / L"Plugins" / L"shared.dll", "low");
        writeTextFile(runtimeLow / L"Interface" / L"not-early.swf", "not copied");
        writeTextFile(runtimeHigh / L"SKSE" / L"Plugins" / L"shared.dll", "high");
        writeTextFile(runtimeHigh / L"DLLPlugins" / L"meh-loader.dll", "meh");
        writeTextFile(runtimeHigh / L"Data" / L"NetScriptFramework" / L"Plugins" / L"net.dll", "net");
        writeTextFile(overwrite / L"SKSE" / L"Plugins" / L"overwrite-only.dll", "overwrite");

        writeTextFile(
            config,
            "{"
            "\"schemaVersion\":\"1\","
            "\"name\":\"Imported Build\","
            "\"templateId\":\"skyrimse\","
            "\"gameName\":\"Skyrim Special Edition\","
            "\"gamePath\":\"Stock Game\","
            "\"dataDirectory\":\"Data\","
            "\"defaultProfile\":\"Default\","
            "\"scriptExtender\":{\"name\":\"SKSE\",\"loaderExecutable\":\"skse64_loader.exe\",\"website\":\"\"},"
            "\"launchExecutables\":[{"
            "\"id\":\"skse\","
            "\"displayName\":\"SKSE\","
            "\"executablePath\":\"mods\\\\Skyrim Script Extender\\\\root\\\\skse64_loader.exe\","
            "\"arguments\":\"\","
            "\"workingDirectory\":\"\""
            "}]"
            "}");

        InstanceMetadataStore::ensureInstance(project, L"skyrimse");
        InstanceMetadataStore::registerInstalledMods(
            project,
            {
                InstalledModImportRecord{skseMod, L"Skyrim Script Extender", {}, true, {}},
                InstalledModImportRecord{runtimeLow, L"Runtime Low", {}, true, {}},
                InstalledModImportRecord{runtimeHigh, L"Runtime High", {}, true, {}}
            });
        InstanceMetadataStore::replaceProfileOrderItems(
            project,
            L"Default",
            {
                ProfileOrderImportItemRecord{L"mod", L"Skyrim Script Extender", {}},
                ProfileOrderImportItemRecord{L"mod", L"Runtime Low", {}},
                ProfileOrderImportItemRecord{L"mod", L"Runtime High", {}}
            });

        Logger logger;
        BuildPathSettingsService pathSettings(logger);
        ExecutableIconService iconService(logger);
        ExecutableService service(logger, iconService, pathSettings);

        const ResolvedExecutableLaunch resolved = service.resolveExecutable(config, L"skse");

        ASSERT_FALSE(resolved.rootBuilderLaunchCacheDirectory.empty());
        const std::filesystem::path cacheData = resolved.rootBuilderLaunchCacheDirectory / L"Data";
        EXPECT_TRUE(std::filesystem::is_regular_file(cacheData / L"SKSE" / L"Plugins" / L"shared.dll"));
        EXPECT_EQ(readTextFile(cacheData / L"SKSE" / L"Plugins" / L"shared.dll"), "high");
        EXPECT_TRUE(std::filesystem::is_regular_file(cacheData / L"SKSE" / L"Plugins" / L"overwrite-only.dll"));
        EXPECT_TRUE(std::filesystem::is_regular_file(cacheData / L"DLLPlugins" / L"meh-loader.dll"));
        EXPECT_TRUE(std::filesystem::is_regular_file(
            cacheData / L"NetScriptFramework" / L"Plugins" / L"net.dll"));
        EXPECT_FALSE(std::filesystem::exists(cacheData / L"Interface" / L"not-early.swf"));
    }

    TEST(ExecutableServiceTests, SkyrimScriptExtenderLaunchMetadataComesFromDefinitionRules)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / L"Launch Metadata Build";
        const std::filesystem::path config = project / L"build.json";
        const std::filesystem::path game = project / L"Stock Game";

        writeExecutableStub(game / L"SkyrimSE.exe");
        writeExecutableStub(game / L"skse64_loader.exe");
        std::filesystem::create_directories(game / L"Data");

        writeTextFile(
            config,
            "{"
            "\"schemaVersion\":\"1\","
            "\"name\":\"Launch Metadata Build\","
            "\"templateId\":\"skyrimse\","
            "\"gameName\":\"Skyrim Special Edition\","
            "\"gamePath\":\"Stock Game\","
            "\"dataDirectory\":\"Data\","
            "\"defaultProfile\":\"Default\","
            "\"launchExecutables\":[{"
            "\"id\":\"skse\","
            "\"displayName\":\"SKSE\","
            "\"executablePath\":\"skse64_loader.exe\","
            "\"arguments\":\"\","
            "\"workingDirectory\":\"\""
            "}]"
            "}");

        InstanceMetadataStore::ensureInstance(project, L"skyrimse");

        Logger logger;
        BuildPathSettingsService pathSettings(logger);
        ExecutableIconService iconService(logger);
        ExecutableService service(logger, iconService, pathSettings);

        const ResolvedExecutableLaunch resolved = service.resolveExecutable(config, L"skse");

        EXPECT_EQ(normalized(resolved.resolvedExecutablePath), normalized(game / L"skse64_loader.exe"));
        EXPECT_EQ(normalized(resolved.resolvedWorkingDirectory), normalized(game));
        EXPECT_EQ(resolved.launchTrackingKind, LaunchTrackingKind::ExpectedChildProcess);
        ASSERT_EQ(resolved.expectedChildProcessNames.size(), 1U);
        EXPECT_EQ(resolved.expectedChildProcessNames.front(), L"SkyrimSE.exe");
        EXPECT_EQ(resolved.handoffDisplayName, L"Skyrim Special Edition");
        EXPECT_EQ(resolved.handoffTimeoutMs, 30000U);
    }

    TEST(ExecutableServiceTests, UnknownTemplateUsesDirectLaunchWithoutSkyrimRules)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / L"Unknown Game Build";
        const std::filesystem::path config = project / L"build.json";
        const std::filesystem::path game = project / L"Stock Game";

        writeExecutableStub(game / L"tool_loader.exe");

        writeTextFile(
            config,
            "{"
            "\"schemaVersion\":\"1\","
            "\"name\":\"Unknown Game Build\","
            "\"templateId\":\"unknown-game\","
            "\"gameName\":\"Unknown Game\","
            "\"gamePath\":\"Stock Game\","
            "\"defaultProfile\":\"Default\","
            "\"launchExecutables\":[{"
            "\"id\":\"tool\","
            "\"displayName\":\"Tool Loader\","
            "\"executablePath\":\"tool_loader.exe\","
            "\"arguments\":\"\","
            "\"workingDirectory\":\"\""
            "}]"
            "}");

        Logger logger;
        BuildPathSettingsService pathSettings(logger);
        ExecutableIconService iconService(logger);
        ExecutableService service(logger, iconService, pathSettings);

        const ResolvedExecutableLaunch resolved = service.resolveExecutable(config, L"tool");

        EXPECT_TRUE(resolved.gameId.empty());
        EXPECT_EQ(resolved.templateId, L"unknown-game");
        EXPECT_EQ(resolved.launchTrackingKind, LaunchTrackingKind::DirectProcess);
        EXPECT_TRUE(resolved.expectedChildProcessNames.empty());
        EXPECT_TRUE(resolved.handoffDisplayName.empty());
        EXPECT_EQ(resolved.handoffTimeoutMs, 0U);
        EXPECT_FALSE(resolved.vfsRules.has_value());
        EXPECT_FALSE(resolved.contentLayoutRules.has_value());
    }

    TEST(ExecutableServiceTests, LegacyScriptExtenderManifestFieldDoesNotDriveUnknownTemplate)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / L"Legacy Unknown Build";
        const std::filesystem::path config = project / L"build.json";
        const std::filesystem::path game = project / L"Stock Game";
        const std::filesystem::path modLoader = project / L"mods" / L"Tool" / L"root" / L"skse64_loader.exe";

        writeExecutableStub(game / L"skse64_loader.exe");
        writeExecutableStub(modLoader);

        writeTextFile(
            config,
            "{"
            "\"schemaVersion\":\"1\","
            "\"name\":\"Legacy Unknown Build\","
            "\"templateId\":\"unknown-game\","
            "\"gameName\":\"Unknown Game\","
            "\"gamePath\":\"Stock Game\","
            "\"defaultProfile\":\"Default\","
            "\"scriptExtender\":{\"name\":\"Legacy SE\",\"loaderExecutable\":\"skse64_loader.exe\",\"website\":\"\"},"
            "\"launchExecutables\":[{"
            "\"id\":\"tool\","
            "\"displayName\":\"Tool Loader\","
            "\"executablePath\":\"mods\\\\Tool\\\\root\\\\skse64_loader.exe\","
            "\"arguments\":\"\","
            "\"workingDirectory\":\"\""
            "}]"
            "}");

        Logger logger;
        BuildPathSettingsService pathSettings(logger);
        ExecutableIconService iconService(logger);
        ExecutableService service(logger, iconService, pathSettings);

        const ResolvedExecutableLaunch resolved = service.resolveExecutable(config, L"tool");

        EXPECT_EQ(normalized(resolved.resolvedExecutablePath), normalized(modLoader));
        EXPECT_EQ(resolved.launchTrackingKind, LaunchTrackingKind::DirectProcess);
        EXPECT_TRUE(resolved.expectedChildProcessNames.empty());
        EXPECT_TRUE(resolved.handoffDisplayName.empty());
        EXPECT_EQ(resolved.handoffTimeoutMs, 0U);
    }

#ifdef _WIN32
    TEST(ExecutableServiceTests, RootBuilderLaunchCacheRefusesPreexistingJunction)
    {
        TempDirectory temp;
        const std::filesystem::path project = temp.path() / L"Imported Build";
        const std::filesystem::path config = project / L"build.json";
        const std::filesystem::path game = project / L"Stock Game";
        const std::filesystem::path mods = project / L"mods";

        writeExecutableStub(game / L"SkyrimSE.exe");
        std::filesystem::create_directories(game / L"Data");

        const std::filesystem::path skseMod = mods / L"Skyrim Script Extender";
        const std::filesystem::path skseLoader = skseMod / L"root" / L"skse64_loader.exe";
        writeExecutableStub(skseLoader);

        writeTextFile(
            config,
            "{"
            "\"schemaVersion\":\"1\","
            "\"name\":\"Imported Build\","
            "\"templateId\":\"skyrimse\","
            "\"gameName\":\"Skyrim Special Edition\","
            "\"gamePath\":\"Stock Game\","
            "\"dataDirectory\":\"Data\","
            "\"defaultProfile\":\"Default\","
            "\"scriptExtender\":{\"name\":\"SKSE\",\"loaderExecutable\":\"skse64_loader.exe\",\"website\":\"\"},"
            "\"launchExecutables\":[{"
            "\"id\":\"skse\","
            "\"displayName\":\"SKSE\","
            "\"executablePath\":\"mods\\\\Skyrim Script Extender\\\\root\\\\skse64_loader.exe\","
            "\"arguments\":\"\","
            "\"workingDirectory\":\"\""
            "}]"
            "}");

        InstanceMetadataStore::ensureInstance(project, L"skyrimse");
        InstanceMetadataStore::registerInstalledMods(
            project,
            {InstalledModImportRecord{skseMod, L"Skyrim Script Extender", {}, true, {}}});
        InstanceMetadataStore::replaceProfileOrderItems(
            project,
            L"Default",
            {ProfileOrderImportItemRecord{L"mod", L"Skyrim Script Extender", {}}});

        const std::filesystem::path outside = temp.path() / L"outside-cache-target";
        writeTextFile(outside / L"sentinel.txt", "keep");

        const std::filesystem::path cacheParent = project / L".flow" / L"root-launch";
        std::filesystem::create_directories(cacheParent);
        const std::filesystem::path cacheRoot = cacheParent / L"Skyrim_Script_Extender";

        std::error_code junctionError;
        if (!createDirectoryJunction(outside, cacheRoot, junctionError))
        {
            GTEST_SKIP() << "Directory junction creation is not available: " << junctionError.message();
        }

        Logger logger;
        BuildPathSettingsService pathSettings(logger);
        ExecutableIconService iconService(logger);
        ExecutableService service(logger, iconService, pathSettings);

        const ResolvedExecutableLaunch resolved = service.resolveExecutable(config, L"skse");

        EXPECT_TRUE(resolved.rootBuilderLaunchCacheDirectory.empty());
        EXPECT_EQ(normalized(resolved.resolvedExecutablePath), normalized(skseLoader));
        EXPECT_EQ(readTextFile(outside / L"sentinel.txt"), "keep");
        EXPECT_FALSE(std::filesystem::exists(outside / L"skse64_loader.exe"));

        std::filesystem::remove(cacheRoot);
    }
#endif
}
