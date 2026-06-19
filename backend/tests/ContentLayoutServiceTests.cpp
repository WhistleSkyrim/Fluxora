#include "FluxoraCore/GameSupport/GameSupportRegistry.hpp"
#include "FluxoraCore/Services/ContentLayoutService.hpp"
#include "FluxoraCore/Services/Logger.hpp"
#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fluxora::tests
{
    namespace
    {
        [[nodiscard]] const PlacementPlanEntry* findEntry(
            const PlacementPlan& plan,
            std::wstring_view sourcePath)
        {
            const auto found = std::find_if(
                plan.entries.begin(),
                plan.entries.end(),
                [sourcePath](const PlacementPlanEntry& entry)
                {
                    return entry.sourcePath.path().generic_wstring() == sourcePath;
                });
            return found == plan.entries.end() ? nullptr : &*found;
        }

        [[nodiscard]] std::string readFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::in | std::ios::binary);
            std::ostringstream content;
            content << file.rdbuf();
            return content.str();
        }

        [[nodiscard]] bool hasFindingFor(
            const PlacementPlan& plan,
            ContentLayoutClassification classification)
        {
            return std::any_of(
                plan.validationFindings.begin(),
                plan.validationFindings.end(),
                [classification](const ValidationFinding& finding)
                {
                    return finding.classification == classification;
                });
        }

        class ContentLayoutServiceTests : public testing::Test
        {
        protected:
            ContentLayoutServiceTests()
            {
                registry_.loadEmbeddedDefinitions();
                lookup_ = registry_.lookupById(L"skyrimse");
            }

            [[nodiscard]] ContentLayoutAnalysisRequest skyrimRequest(
                std::vector<ContentLayoutArchiveEntry> entries = {}) const
            {
                EXPECT_TRUE(lookup_.supported);
                EXPECT_NE(lookup_.support, nullptr);
                const GameSupportComponents& components = lookup_.support->components();
                EXPECT_NE(components.contentLayoutRulesProvider, nullptr);

                ContentLayoutAnalysisRequest request;
                request.selectedGameId = lookup_.support->identity().id;
                request.selectedGameDisplayName = lookup_.support->identity().displayName;
                request.selectedGameCapabilities = lookup_.support->capabilities();
                request.rulesProvider = components.contentLayoutRulesProvider;
                request.archiveFileTree = std::move(entries);
                return request;
            }

            GameSupportRegistry registry_;
            GameSupportLookupResult lookup_;
            ContentLayoutService service_;
        };

        class TestContentLayoutRulesProvider final : public IContentLayoutRulesProvider
        {
        public:
            explicit TestContentLayoutRulesProvider(ContentLayoutSupportRules rules)
                : rules_(std::move(rules))
            {
            }

            [[nodiscard]] const ContentLayoutSupportRules& contentLayoutRules() const noexcept override
            {
                return rules_;
            }

        private:
            ContentLayoutSupportRules rules_;
        };

        [[nodiscard]] ContentLayoutAnalysisRequest requestForRules(
            const TestContentLayoutRulesProvider& provider,
            CapabilitySet capabilities,
            std::vector<ContentLayoutArchiveEntry> entries)
        {
            ContentLayoutAnalysisRequest request;
            request.selectedGameId = GameId::parseOrThrow(L"testgame");
            request.selectedGameDisplayName = L"Test Game";
            request.selectedGameCapabilities = capabilities;
            request.rulesProvider = &provider;
            request.archiveFileTree = std::move(entries);
            return request;
        }
    }

    TEST_F(ContentLayoutServiceTests, SkyrimDataFolderClassifiesPluginsArchivesAndSksePlugins)
    {
        const PlacementPlan plan = service_.analyze(skyrimRequest({
            {L"Data/SkyUI_SE.esp"},
            {L"Data/SkyUI_SE.bsa"},
            {L"Data/SKSE/Plugins/skse_plugin.dll"}
        }));

        ASSERT_TRUE(plan.canInstall());
        EXPECT_EQ(plan.summary.pluginEntries, 1U);
        EXPECT_EQ(plan.summary.archiveEntries, 1U);
        EXPECT_EQ(plan.summary.scriptExtenderEntries, 1U);

        const PlacementPlanEntry* plugin = findEntry(plan, L"Data/SkyUI_SE.esp");
        ASSERT_NE(plugin, nullptr);
        EXPECT_EQ(plugin->classification, ContentLayoutClassification::Plugin);
        EXPECT_EQ(plugin->target, PlacementTarget::Data);
        EXPECT_EQ(plugin->targetRelativePath.path().generic_wstring(), L"SkyUI_SE.esp");

        const PlacementPlanEntry* archive = findEntry(plan, L"Data/SkyUI_SE.bsa");
        ASSERT_NE(archive, nullptr);
        EXPECT_EQ(archive->classification, ContentLayoutClassification::Archive);
        EXPECT_EQ(archive->targetRelativePath.path().generic_wstring(), L"SkyUI_SE.bsa");

        const PlacementPlanEntry* skse = findEntry(plan, L"Data/SKSE/Plugins/skse_plugin.dll");
        ASSERT_NE(skse, nullptr);
        EXPECT_EQ(skse->classification, ContentLayoutClassification::ScriptExtender);
        EXPECT_EQ(skse->target, PlacementTarget::Data);
        EXPECT_EQ(skse->targetRelativePath.path().generic_wstring(), L"SKSE/Plugins/skse_plugin.dll");
    }

    TEST_F(ContentLayoutServiceTests, LogsPlacementDecisionsAndBlockers)
    {
        Logger logger;
        Logger::setOperationId(L"content-layout-diagnostics-test");
        logger.initialize();
        ASSERT_TRUE(logger.isInitialized());

        ContentLayoutAnalysisRequest request = skyrimRequest({
            {L"Data/SkyUI_SE.esp"},
            {L"Data/unexpected_tool.exe"}
        });
        request.gameDefinitionVersion = L"test-definition-version";
        request.logger = &logger;

        const PlacementPlan plan = service_.analyze(request);
        EXPECT_FALSE(plan.canInstall());

        logger.shutdown();
        Logger::clearOperationId();

        const std::string content = readFile(logger.operationsLogPath());
        EXPECT_NE(content.find("operationId=content-layout-diagnostics-test"), std::string::npos);
        EXPECT_NE(content.find("contentLayoutDecision=blocked"), std::string::npos);
        EXPECT_NE(content.find("definitionVersion=\"test-definition-version\""), std::string::npos);
        EXPECT_NE(content.find("source=\"Data/SkyUI_SE.esp\""), std::string::npos);
        EXPECT_NE(content.find("Plugin extension matches the selected game's plugin rules."), std::string::npos);
        EXPECT_NE(content.find("source=\"Data/unexpected_tool.exe\""), std::string::npos);
        EXPECT_NE(content.find("unexpected executable or DLL"), std::string::npos);
    }

    TEST_F(ContentLayoutServiceTests, SkyrimArchiveWithoutDataFolderStillClassifiesDataContent)
    {
        const PlacementPlan plan = service_.analyze(skyrimRequest({
            {L"SkyUI_SE.esp"},
            {L"meshes/actors/character/facegen.nif"},
            {L"SKSE/Plugins/skse_plugin.dll"}
        }));

        ASSERT_TRUE(plan.canInstall());
        const PlacementPlanEntry* plugin = findEntry(plan, L"SkyUI_SE.esp");
        ASSERT_NE(plugin, nullptr);
        EXPECT_EQ(plugin->classification, ContentLayoutClassification::Plugin);
        EXPECT_EQ(plugin->target, PlacementTarget::Data);
        EXPECT_EQ(plugin->targetRelativePath.path().generic_wstring(), L"SkyUI_SE.esp");

        const PlacementPlanEntry* mesh = findEntry(plan, L"meshes/actors/character/facegen.nif");
        ASSERT_NE(mesh, nullptr);
        EXPECT_EQ(mesh->classification, ContentLayoutClassification::GameData);
        EXPECT_EQ(mesh->target, PlacementTarget::Data);

        const PlacementPlanEntry* skse = findEntry(plan, L"SKSE/Plugins/skse_plugin.dll");
        ASSERT_NE(skse, nullptr);
        EXPECT_EQ(skse->classification, ContentLayoutClassification::ScriptExtender);
    }

    TEST_F(ContentLayoutServiceTests, UnknownRootFilesAreReportedWithoutBlockingRecognizedContent)
    {
        const PlacementPlan plan = service_.analyze(skyrimRequest({
            {L"SkyUI_SE.esp"},
            {L"mystery.payload"}
        }));

        ASSERT_TRUE(plan.canInstall());
        EXPECT_EQ(plan.summary.unknownEntries, 1U);
        EXPECT_TRUE(plan.summary.hasWarnings);
        EXPECT_TRUE(hasFindingFor(plan, ContentLayoutClassification::Unknown));

        const PlacementPlanEntry* unknown = findEntry(plan, L"mystery.payload");
        ASSERT_NE(unknown, nullptr);
        EXPECT_EQ(unknown->classification, ContentLayoutClassification::Unknown);
        EXPECT_EQ(unknown->target, PlacementTarget::Data);
    }

    TEST_F(ContentLayoutServiceTests, UnsafePathsAreBlocked)
    {
        const PlacementPlan plan = service_.analyze(skyrimRequest({
            {L"../escaped.txt"},
            {L"Data/SkyUI_SE.esp"}
        }));

        EXPECT_FALSE(plan.canInstall());
        EXPECT_TRUE(plan.summary.hasBlockers);
        EXPECT_EQ(plan.summary.unsafeEntries, 1U);
        EXPECT_TRUE(hasFindingFor(plan, ContentLayoutClassification::Unsafe));
    }

    TEST_F(ContentLayoutServiceTests, RootScriptExtenderLoaderUsesGameRootAndUnexpectedExecutablesBlock)
    {
        const PlacementPlan plan = service_.analyze(skyrimRequest({
            {L"skse64_loader.exe"},
            {L"helper.exe"}
        }));

        EXPECT_FALSE(plan.canInstall());
        const PlacementPlanEntry* loader = findEntry(plan, L"skse64_loader.exe");
        ASSERT_NE(loader, nullptr);
        EXPECT_EQ(loader->classification, ContentLayoutClassification::ScriptExtender);
        EXPECT_EQ(loader->target, PlacementTarget::GameRoot);
        EXPECT_EQ(loader->contentArea, ContentArea::GameRoot);
        EXPECT_EQ(loader->targetRelativePath.path().generic_wstring(), L"skse64_loader.exe");

        const PlacementPlanEntry* helper = findEntry(plan, L"helper.exe");
        ASSERT_NE(helper, nullptr);
        EXPECT_EQ(helper->classification, ContentLayoutClassification::ToolExecutable);
        EXPECT_EQ(helper->target, PlacementTarget::Blocked);
        EXPECT_TRUE(hasFindingFor(plan, ContentLayoutClassification::ToolExecutable));
    }

    TEST_F(ContentLayoutServiceTests, ApplyPlanNormalizesDataAndRootTargetsInStagingDirectory)
    {
        TempDirectory temp;
        const std::filesystem::path staging = temp.path() / L"staging";
        writeTextFile(staging / L"Data" / L"SkyUI_SE.esp", "plugin");
        writeTextFile(staging / L"Data" / L"SKSE" / L"Plugins" / L"skse_plugin.dll", "dll");
        writeTextFile(staging / L"skse64_loader.exe", "loader");

        const PlacementPlan plan = service_.analyzeDirectory(staging, skyrimRequest());
        ASSERT_TRUE(plan.canInstall());

        service_.applyPlanToDirectory(staging, plan);

        EXPECT_TRUE(std::filesystem::is_regular_file(staging / L"SkyUI_SE.esp"));
        EXPECT_TRUE(std::filesystem::is_regular_file(staging / L"SKSE" / L"Plugins" / L"skse_plugin.dll"));
        EXPECT_TRUE(std::filesystem::is_regular_file(staging / L"root" / L"skse64_loader.exe"));
        EXPECT_FALSE(std::filesystem::exists(staging / L"Data" / L"SkyUI_SE.esp"));
        EXPECT_FALSE(std::filesystem::exists(staging / L"skse64_loader.exe"));
    }

    TEST_F(ContentLayoutServiceTests, SelectedGameRulesDriveDataFolderAndPluginRecognition)
    {
        ContentLayoutSupportRules rules;
        rules.dataFolder = L"Payload";
        rules.pluginExtensions = {NormalizedExtension::parseOrThrow(L".plug")};
        rules.archiveExtensions = {NormalizedExtension::parseOrThrow(L".pack")};
        rules.gameDataDirectories = {L"assets"};
        const TestContentLayoutRulesProvider provider(std::move(rules));

        CapabilitySet capabilities;
        capabilities.enable(GameCapability::ContentLayoutRules);

        const PlacementPlan plan = service_.analyze(requestForRules(
            provider,
            capabilities,
            {
                {L"Payload/TestPlugin.plug"},
                {L"Payload/Textures.pack"},
                {L"Payload/assets/model.bin"},
                {L"Data/SkyrimStyle.esp"}
            }));

        ASSERT_TRUE(plan.canInstall());
        EXPECT_EQ(plan.summary.pluginEntries, 1U);
        EXPECT_EQ(plan.summary.archiveEntries, 1U);
        EXPECT_EQ(plan.summary.gameDataEntries, 1U);
        EXPECT_EQ(plan.summary.unknownEntries, 1U);

        const PlacementPlanEntry* plugin = findEntry(plan, L"Payload/TestPlugin.plug");
        ASSERT_NE(plugin, nullptr);
        EXPECT_EQ(plugin->classification, ContentLayoutClassification::Plugin);
        EXPECT_EQ(plugin->targetRelativePath.path().generic_wstring(), L"TestPlugin.plug");

        const PlacementPlanEntry* skyrimStylePlugin = findEntry(plan, L"Data/SkyrimStyle.esp");
        ASSERT_NE(skyrimStylePlugin, nullptr);
        EXPECT_EQ(skyrimStylePlugin->classification, ContentLayoutClassification::Unknown);
        EXPECT_TRUE(plan.summary.hasWarnings);
    }

    TEST_F(ContentLayoutServiceTests, SelectedSubfolderRestrictsAnalysisToChosenPayloadRoot)
    {
        ContentLayoutAnalysisRequest request = skyrimRequest({
            {L"Some Mod/Data/SkyUI_SE.esp"},
            {L"Some Mod/Data/textures/interface/widget.dds"},
            {L"Other Mod/Data/Other.esp"}
        });
        request.userSelectedSubfolder = std::filesystem::path(L"Some Mod");

        const PlacementPlan plan = service_.analyze(request);

        ASSERT_TRUE(plan.canInstall());
        EXPECT_EQ(plan.summary.totalEntries, 2U);
        EXPECT_EQ(plan.summary.pluginEntries, 1U);
        EXPECT_EQ(plan.summary.gameDataEntries, 1U);
        EXPECT_EQ(findEntry(plan, L"Other Mod/Data/Other.esp"), nullptr);

        const PlacementPlanEntry* plugin = findEntry(plan, L"Some Mod/Data/SkyUI_SE.esp");
        ASSERT_NE(plugin, nullptr);
        EXPECT_EQ(plugin->targetRelativePath.path().generic_wstring(), L"SkyUI_SE.esp");
    }

    TEST_F(ContentLayoutServiceTests, ExplicitRootWrapperPlacesRootContentThroughRootTarget)
    {
        const PlacementPlan plan = service_.analyze(skyrimRequest({
            {L"root/skse64_loader.exe"},
            {L"Data/SkyUI_SE.esp"}
        }));

        ASSERT_TRUE(plan.canInstall());
        const PlacementPlanEntry* loader = findEntry(plan, L"root/skse64_loader.exe");
        ASSERT_NE(loader, nullptr);
        EXPECT_EQ(loader->classification, ContentLayoutClassification::ScriptExtender);
        EXPECT_EQ(loader->target, PlacementTarget::GameRoot);
        EXPECT_EQ(loader->targetRelativePath.path().generic_wstring(), L"skse64_loader.exe");
    }

    TEST_F(ContentLayoutServiceTests, SkyrimRulesCoverMasterLightArchivesAndCommonDataDirectories)
    {
        const PlacementPlan plan = service_.analyze(skyrimRequest({
            {L"Update.esm"},
            {L"CreationClub.esl"},
            {L"Skyrim - Textures.bsa"},
            {L"textures/armor/iron.dds"},
            {L"scripts/Foo.pex"},
            {L"interface/bar.swf"}
        }));

        ASSERT_TRUE(plan.canInstall());
        EXPECT_EQ(plan.summary.pluginEntries, 2U);
        EXPECT_EQ(plan.summary.archiveEntries, 1U);
        EXPECT_EQ(plan.summary.gameDataEntries, 3U);

        const PlacementPlanEntry* archive = findEntry(plan, L"Skyrim - Textures.bsa");
        ASSERT_NE(archive, nullptr);
        EXPECT_EQ(archive->classification, ContentLayoutClassification::Archive);
        EXPECT_EQ(archive->target, PlacementTarget::Data);
    }

    TEST_F(ContentLayoutServiceTests, DuplicateTargetsAfterDataWrapperAreBlocked)
    {
        const PlacementPlan plan = service_.analyze(skyrimRequest({
            {L"Data/Same.esp"},
            {L"same.ESP"}
        }));

        EXPECT_FALSE(plan.canInstall());
        EXPECT_TRUE(plan.summary.hasBlockers);
        EXPECT_TRUE(hasFindingFor(plan, ContentLayoutClassification::Plugin));
    }

    TEST_F(ContentLayoutServiceTests, SelectedGameRulesBlockRootFilesWhenCapabilityIsDisabled)
    {
        ContentLayoutSupportRules rules;
        rules.dataFolder = L"Payload";
        rules.supportsRootFiles = false;
        rules.rootFileWrapperDirectory = L"root";
        rules.pluginExtensions = {NormalizedExtension::parseOrThrow(L".plug")};
        const TestContentLayoutRulesProvider provider(std::move(rules));

        CapabilitySet capabilities;
        capabilities.enable(GameCapability::ContentLayoutRules);

        const PlacementPlan plan = service_.analyze(requestForRules(
            provider,
            capabilities,
            {
                {L"Payload/TestPlugin.plug"},
                {L"root/Launcher.exe"}
            }));

        EXPECT_FALSE(plan.canInstall());
        EXPECT_EQ(plan.summary.pluginEntries, 1U);
        EXPECT_TRUE(hasFindingFor(plan, ContentLayoutClassification::GameRoot));
    }

    TEST_F(ContentLayoutServiceTests, LayoutCacheInvalidatesWhenDefinitionVersionChanges)
    {
        ContentLayoutSupportRules firstRules;
        firstRules.dataFolder = L"Payload";
        firstRules.pluginExtensions = {NormalizedExtension::parseOrThrow(L".one")};
        const TestContentLayoutRulesProvider firstProvider(std::move(firstRules));

        CapabilitySet capabilities;
        capabilities.enable(GameCapability::ContentLayoutRules);

        ContentLayoutAnalysisRequest firstRequest = requestForRules(
            firstProvider,
            capabilities,
            {{L"Payload/Test.one"}});
        firstRequest.archiveContentHash = L"same-archive";
        firstRequest.gameDefinitionVersion = L"1.0.0";

        const PlacementPlan first = service_.analyze(firstRequest);
        ASSERT_TRUE(first.canInstall());
        ASSERT_EQ(first.summary.pluginEntries, 1U);

        ContentLayoutSupportRules secondRules;
        secondRules.dataFolder = L"Payload";
        secondRules.pluginExtensions = {NormalizedExtension::parseOrThrow(L".two")};
        const TestContentLayoutRulesProvider secondProvider(std::move(secondRules));

        ContentLayoutAnalysisRequest secondRequest = requestForRules(
            secondProvider,
            capabilities,
            {{L"Payload/Test.one"}});
        secondRequest.archiveContentHash = L"same-archive";
        secondRequest.gameDefinitionVersion = L"2.0.0";

        const PlacementPlan second = service_.analyze(secondRequest);
        EXPECT_FALSE(second.canInstall());
        EXPECT_EQ(second.summary.pluginEntries, 0U);
        EXPECT_TRUE(hasFindingFor(second, ContentLayoutClassification::Unknown));
    }

    TEST_F(ContentLayoutServiceTests, LongLayoutAnalysisCanBeCanceled)
    {
        std::atomic_bool canceled{true};
        ContentLayoutAnalysisRequest request = skyrimRequest({{L"Data/SkyUI_SE.esp"}});
        request.cancellationRequested = &canceled;

        EXPECT_THROW((void)service_.analyze(request), std::runtime_error);
    }
}
