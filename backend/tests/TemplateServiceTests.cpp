#include "FluxoraCore/Services/TemplateService.hpp"

#include "FluxoraCore/Services/Logger.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>

namespace fluxora::tests
{
    namespace
    {
        bool contains(const std::vector<std::wstring>& values, const std::wstring& expected)
        {
            return std::find(values.begin(), values.end(), expected) != values.end();
        }

        const TemplateCapability* findCapability(
            const std::vector<TemplateCapability>& capabilities,
            const std::wstring& id)
        {
            const auto match = std::find_if(
                capabilities.begin(),
                capabilities.end(),
                [&id](const TemplateCapability& capability)
                {
                    return capability.id == id;
                });

            return match == capabilities.end() ? nullptr : &(*match);
        }
    }

    TEST(TemplateServiceTests, InitializeProjectsGameTemplatesFromRegistryDefinitions)
    {
        Logger logger;
        TemplateService service(logger);
        GameSupportRegistry registry;
        registry.loadEmbeddedDefinitions();

        EXPECT_FALSE(service.isInitialized());

        service.initialize();

        EXPECT_TRUE(service.isInitialized());
        EXPECT_EQ(service.baseTemplate().id, L"base");
        EXPECT_TRUE(service.baseTemplate().isBase);
        EXPECT_FALSE(contains(service.baseTemplate().profileFiles, L"plugins.txt"));
        EXPECT_FALSE(contains(service.baseTemplate().profileFiles, L"loadorder.txt"));
        ASSERT_EQ(service.gameTemplates().size(), registry.definitions().size());
        ASSERT_FALSE(registry.definitions().empty());
        ASSERT_FALSE(service.gameTemplates().empty());
        EXPECT_EQ(service.gameTemplates().front().id, registry.definitions().front().id.value());
        EXPECT_EQ(service.gameTemplates().front().displayName, registry.definitions().front().displayName);
        EXPECT_NE(service.findGameTemplate(L"skyrimse"), nullptr);
    }

    TEST(TemplateServiceTests, ResolveProjectsRegistryDefinitionOnBase)
    {
        Logger logger;
        TemplateService service(logger);
        service.initialize();

        const BuildTemplate resolved = service.resolve(L" SkyrimSE ");

        EXPECT_EQ(resolved.id, L"skyrimse");
        EXPECT_EQ(resolved.displayName, L"Skyrim Special Edition");
        EXPECT_EQ(resolved.gameName, L"Skyrim Special Edition");
        EXPECT_EQ(resolved.baseTemplateId, L"base");
        EXPECT_FALSE(resolved.isBase);
        EXPECT_EQ(resolved.defaultProfileName, L"Default");
        EXPECT_EQ(resolved.dataDirectory, L"Data");
        EXPECT_EQ(resolved.nexusDomain, L"skyrimspecialedition");
        EXPECT_TRUE(contains(resolved.folders, L"mods"));
        EXPECT_TRUE(contains(resolved.profileFiles, L"plugins.txt"));
        EXPECT_TRUE(contains(resolved.profileFiles, L"loadorder.txt"));
        EXPECT_TRUE(contains(resolved.basePlugins, L"Skyrim.esm"));
        EXPECT_TRUE(contains(resolved.basePlugins, L"Dragonborn.esm"));
        EXPECT_TRUE(contains(resolved.pluginExtensions, L".esm"));
        EXPECT_TRUE(contains(resolved.pluginExtensions, L".esp"));
        EXPECT_TRUE(contains(resolved.pluginExtensions, L".esl"));
        EXPECT_TRUE(contains(resolved.executables, L"SkyrimSE.exe"));
        EXPECT_TRUE(contains(resolved.executables, L"SkyrimSELauncher.exe"));
        EXPECT_TRUE(contains(resolved.executables, L"skse64_loader.exe"));
        EXPECT_NE(findCapability(resolved.capabilities, L"mod-list"), nullptr);
        EXPECT_NE(findCapability(resolved.capabilities, L"plugins"), nullptr);
        EXPECT_NE(findCapability(resolved.capabilities, L"load-order"), nullptr);
        EXPECT_NE(findCapability(resolved.capabilities, L"ini-tweaks"), nullptr);
        EXPECT_NE(findCapability(resolved.capabilities, L"save-games"), nullptr);
        EXPECT_NE(findCapability(resolved.capabilities, L"root-files"), nullptr);
        EXPECT_NE(findCapability(resolved.capabilities, L"content-layout"), nullptr);
        ASSERT_NE(findCapability(resolved.capabilities, L"script-extender"), nullptr);
        EXPECT_EQ(findCapability(resolved.capabilities, L"script-extender")->displayName, L"SKSE64");
        ASSERT_TRUE(resolved.scriptExtender.has_value());
        EXPECT_EQ(resolved.scriptExtender->loaderExecutable, L"skse64_loader.exe");
    }

    TEST(TemplateServiceTests, CompatibilityProjectionKeepsDeprecatedBridgeFieldsForFrontend)
    {
        Logger logger;
        TemplateService service(logger);
        service.initialize();

        const BuildTemplate listed = service.gameTemplates().front();
        const BuildTemplate resolved = service.resolve(listed.id);

        // Deprecated bridge compatibility fields stay populated until the C#
        // frontend moves to typed GameDefinition/capability models.
        EXPECT_EQ(listed.id, L"skyrimse");
        EXPECT_EQ(listed.displayName, L"Skyrim Special Edition");
        EXPECT_EQ(listed.gameName, L"Skyrim Special Edition");
        EXPECT_FALSE(listed.summary.empty());

        EXPECT_EQ(resolved.baseTemplateId, L"base");
        EXPECT_EQ(resolved.defaultProfileName, L"Default");
        EXPECT_EQ(resolved.dataDirectory, L"Data");
        EXPECT_EQ(resolved.nexusDomain, L"skyrimspecialedition");
        EXPECT_TRUE(contains(resolved.folders, L"mods"));
        EXPECT_TRUE(contains(resolved.profileFiles, L"plugins.txt"));
        EXPECT_TRUE(contains(resolved.basePlugins, L"Skyrim.esm"));
        EXPECT_TRUE(contains(resolved.pluginExtensions, L".esp"));
        EXPECT_TRUE(contains(resolved.executables, L"SkyrimSE.exe"));
        EXPECT_NE(findCapability(resolved.capabilities, L"plugins"), nullptr);
        ASSERT_TRUE(resolved.scriptExtender.has_value());
        EXPECT_EQ(resolved.scriptExtender->name, L"Skyrim Script Extender (SKSE64)");
        EXPECT_EQ(resolved.scriptExtender->website, L"https://skse.silverlock.org/");
    }

    TEST(TemplateServiceTests, ResolveRejectsUnknownTemplate)
    {
        Logger logger;
        TemplateService service(logger);
        service.initialize();

        EXPECT_THROW((void)service.resolve(L"unknown"), std::invalid_argument);
    }
}
