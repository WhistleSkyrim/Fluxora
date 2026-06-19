#include "FluxoraCore/GameSupport/GameSupportRegistry.hpp"
#include "FluxoraCore/GameSupport/SkyrimSpecialEditionSupport.hpp"

#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

namespace fluxora::tests
{
    namespace
    {
        [[nodiscard]] bool containsExtension(
            const std::vector<NormalizedExtension>& extensions,
            std::wstring_view value)
        {
            return std::find_if(
                extensions.begin(),
                extensions.end(),
                [value](const NormalizedExtension& extension)
                {
                    return extension.value() == value;
                }) != extensions.end();
        }

        [[nodiscard]] bool containsPath(
            const std::vector<std::filesystem::path>& paths,
            std::wstring_view value)
        {
            return std::find_if(
                paths.begin(),
                paths.end(),
                [value](const std::filesystem::path& path)
                {
                    return path.generic_wstring() == value;
                }) != paths.end();
        }
    }

    TEST(GameSupportRegistryTests, EmbeddedRegistryFindsSkyrimByIdExecutableAndDomainHint)
    {
        GameSupportRegistry registry;
        registry.loadEmbeddedDefinitions();

        const GameSupportLookupResult byId = registry.lookupById(GameId::parseOrThrow(L"SkyrimSE"));
        ASSERT_TRUE(byId.supported);
        ASSERT_NE(byId.definition, nullptr);
        ASSERT_NE(byId.support, nullptr);
        EXPECT_EQ(byId.mode, GameSupportLookupMode::Id);
        EXPECT_EQ(byId.confidence, DetectionConfidence::Explicit);
        EXPECT_EQ(byId.definition->displayName, L"Skyrim Special Edition");
        EXPECT_EQ(byId.support->identity().id.value(), L"skyrimse");

        const GameSupportLookupResult byDomain =
            registry.lookupByDomainHint(L"https://www.nexusmods.com/skyrimspecialedition/mods/123");
        ASSERT_TRUE(byDomain.supported);
        EXPECT_EQ(byDomain.mode, GameSupportLookupMode::DomainHint);
        EXPECT_EQ(byDomain.definition->id.value(), L"skyrimse");

        const GameSupportLookupResult byExecutable = registry.lookupByExecutableName(L"skse64_loader.exe");
        ASSERT_TRUE(byExecutable.supported);
        EXPECT_EQ(byExecutable.mode, GameSupportLookupMode::ExecutableName);
        EXPECT_EQ(byExecutable.confidence, DetectionConfidence::High);
        EXPECT_EQ(byExecutable.definition->id.value(), L"skyrimse");
    }

    TEST(GameSupportRegistryTests, InstallPathLookupReturnsConfidenceMetadata)
    {
        TempDirectory temp;
        const std::filesystem::path installPath = temp.path() / L"Skyrim Special Edition";
        writeTextFile(installPath / L"SkyrimSE.exe", "exe");
        writeTextFile(installPath / L"Data" / L"Skyrim.esm", "master");

        GameSupportRegistry registry;
        registry.loadEmbeddedDefinitions();

        const GameSupportLookupResult result = registry.lookupByInstallPath(installPath);

        ASSERT_TRUE(result.supported);
        ASSERT_NE(result.definition, nullptr);
        EXPECT_EQ(result.definition->id.value(), L"skyrimse");
        EXPECT_EQ(result.mode, GameSupportLookupMode::InstallPath);
        EXPECT_EQ(result.confidence, DetectionConfidence::High);
        EXPECT_FALSE(result.matchedHints.empty());
    }

    TEST(GameSupportRegistryTests, ManualSelectionUsesExplicitTypedGameId)
    {
        GameSupportRegistry registry;
        registry.loadEmbeddedDefinitions();

        const GameSupportLookupResult result =
            registry.lookupByManualSelection(GameId::parseOrThrow(L"skyrimse"));

        ASSERT_TRUE(result.supported);
        EXPECT_EQ(result.mode, GameSupportLookupMode::ManualSelection);
        EXPECT_EQ(result.confidence, DetectionConfidence::Explicit);
        EXPECT_EQ(result.definition->displayName, L"Skyrim Special Edition");
    }

    TEST(GameSupportRegistryTests, EmbeddedDefinitionsRegisterBundledSupportModulesWithNarrowProviders)
    {
        GameSupportRegistry registry;
        registry.loadEmbeddedDefinitions();

        const std::vector<const IGameSupport*> modules = registry.supportModules();

        ASSERT_EQ(modules.size(), registry.definitions().size());
        ASSERT_EQ(modules.size(), 1U);
        const IGameSupport* skyrim = modules.front();
        ASSERT_NE(skyrim, nullptr);
        EXPECT_NE(dynamic_cast<const SkyrimSpecialEditionSupport*>(skyrim), nullptr);
        EXPECT_EQ(skyrim->identity().id.value(), L"skyrimse");
        EXPECT_TRUE(skyrim->capabilities().has(GameCapability::Plugins));

        const GameSupportComponents& components = skyrim->components();
        ASSERT_NE(components.pluginRulesProvider, nullptr);
        ASSERT_NE(components.executableRulesProvider, nullptr);
        ASSERT_NE(components.contentLayoutRulesProvider, nullptr);
        ASSERT_NE(components.vfsRulesProvider, nullptr);
        ASSERT_NE(components.uiRulesProvider, nullptr);

        const PluginSupportRules& pluginRules = components.pluginRulesProvider->pluginRules();
        EXPECT_TRUE(containsExtension(pluginRules.pluginExtensions, L".esp"));
        EXPECT_TRUE(containsExtension(pluginRules.pluginExtensions, L".esm"));
        EXPECT_TRUE(containsExtension(pluginRules.pluginExtensions, L".esl"));
        EXPECT_TRUE(containsExtension(pluginRules.masterPluginExtensions, L".esm"));
        EXPECT_TRUE(containsExtension(pluginRules.lightPluginExtensions, L".esl"));
        EXPECT_TRUE(pluginRules.masterPluginExtensionKeys.contains(L".esm"));
        EXPECT_TRUE(pluginRules.lightPluginExtensionKeys.contains(L".esl"));
        EXPECT_FALSE(pluginRules.basePlugins.empty());

        const ContentLayoutSupportRules& layoutRules =
            components.contentLayoutRulesProvider->contentLayoutRules();
        EXPECT_EQ(layoutRules.dataFolder, L"Data");
        EXPECT_TRUE(layoutRules.supportsRootFiles);
        EXPECT_TRUE(containsPath(layoutRules.scriptExtenderDataPaths, L"SKSE/Plugins"));
        EXPECT_TRUE(containsPath(layoutRules.scriptExtenderDataPaths, L"DLLPlugins"));
        EXPECT_TRUE(containsPath(layoutRules.scriptExtenderDataPaths, L"NetScriptFramework"));
        EXPECT_TRUE(containsPath(layoutRules.scriptExtenderDataPaths, L"Plugins"));
        EXPECT_TRUE(layoutRules.scriptExtenderDataPathKeys.contains(L"skse/plugins"));
        EXPECT_TRUE(layoutRules.scriptExtenderDataPathKeys.contains(L"dllplugins"));
        EXPECT_TRUE(layoutRules.scriptExtenderDataPathKeys.contains(L"netscriptframework"));
        EXPECT_TRUE(layoutRules.scriptExtenderDataPathKeys.contains(L"plugins"));

        const VfsSupportRules& vfsRules = components.vfsRulesProvider->vfsRules();
        EXPECT_TRUE(vfsRules.rules.supportsRootBuilder);
        EXPECT_EQ(vfsRules.rules.userSettingsDirectoryName, L"Skyrim Special Edition");
        EXPECT_NE(
            std::find(
                vfsRules.rules.profileIniFileNames.begin(),
                vfsRules.rules.profileIniFileNames.end(),
                L"Skyrim.ini"),
            vfsRules.rules.profileIniFileNames.end());
        EXPECT_NE(
            std::find(
                vfsRules.rules.excludedLaunchCacheDirectories.begin(),
                vfsRules.rules.excludedLaunchCacheDirectories.end(),
                L"DLLPlugins"),
            vfsRules.rules.excludedLaunchCacheDirectories.end());
    }

    TEST(GameSupportRegistryTests, UnknownLookupsReturnExplicitUnsupportedWithoutSkyrimFallback)
    {
        GameSupportRegistry registry;
        registry.loadEmbeddedDefinitions();

        const GameSupportLookupResult byId = registry.lookupById(L"unknown");
        EXPECT_FALSE(byId.supported);
        EXPECT_EQ(byId.definition, nullptr);
        EXPECT_EQ(byId.support, nullptr);
        EXPECT_EQ(byId.status, GameSupportLookupStatus::Unsupported);
        EXPECT_EQ(byId.confidence, DetectionConfidence::None);
        EXPECT_FALSE(byId.message.empty());

        EXPECT_FALSE(registry.lookupByDomain(L"stardewvalley").supported);
        EXPECT_FALSE(registry.lookupByExecutableName(L"Game.exe").supported);
        EXPECT_FALSE(registry.lookupByInstallPath(L"C:\\Games\\Unknown").supported);
        EXPECT_FALSE(registry.lookupByManualSelection(L"fallout4").supported);
    }
}
