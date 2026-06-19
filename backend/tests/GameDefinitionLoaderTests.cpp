#include "FluxoraCore/GameSupport/GameDefinitionLoader.hpp"

#include "TestFilesystem.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace fluxora::tests
{
    namespace
    {
        constexpr std::wstring_view minimalDefinition = LR"json({
            "schemaVersion": "1",
            "definitionVersion": "1.0.0",
            "id": "ExampleGame",
            "displayName": "Example Game",
            "summary": "Generic test game.",
            "aliases": [],
            "domains": ["ExampleDomain"],
            "installFolderAliases": ["Example Game"],
            "defaultProfileName": "Default",
            "dataFolder": "",
            "requiredFiles": ["Example.exe"],
            "executables": [{"id": "game", "displayName": "Example", "name": "Example.exe", "role": "primary"}],
            "executableRoles": {"primary": "Example.exe"},
            "archiveExtensions": ["ZIP", ".7Z"],
            "pluginExtensions": ["ESP", ".ESM"],
            "capabilities": {
                "supportsPlugins": false,
                "supportsLoadOrder": false,
                "supportsRootFiles": false,
                "supportsArchives": false,
                "supportsScriptExtender": false,
                "supportsIniProfiles": false,
                "supportsSaveProfiles": false,
                "supportsGameSpecificVfs": false,
                "supportsContentLayoutRules": false
            },
            "uiTemplateId": "ExampleGame",
            "detectionHints": {
                "executableNames": ["Example.exe"],
                "folderNames": ["Example Game"],
                "domains": ["ExampleDomain"]
            },
            "pluginRules": {
                "profileFiles": [],
                "basePlugins": []
            },
            "contentLayoutRules": {
                "dataFolder": "",
                "supportsRootFiles": false
            },
            "vfsRules": {
                "supportsRootBuilder": false,
                "userSettingsDirectoryName": "",
                "profileIniFileNames": [],
                "saveDirectoryNames": [],
                "excludedLaunchCacheDirectories": []
            },
            "launchRules": {},
            "healthRules": {
                "requiredFiles": ["Example.exe"]
            }
        })json";

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

        [[nodiscard]] bool containsString(
            const std::vector<std::wstring>& values,
            std::wstring_view value)
        {
            return std::find_if(
                values.begin(),
                values.end(),
                [value](const std::wstring& candidate)
                {
                    return candidate == value;
                }) != values.end();
        }

        [[nodiscard]] std::wstring replaceFirst(
            std::wstring source,
            std::wstring_view oldValue,
            std::wstring_view newValue)
        {
            const std::size_t position = source.find(oldValue);
            if (position == std::wstring::npos)
            {
                throw std::logic_error("Test JSON marker was not found.");
            }

            source.replace(position, oldValue.size(), std::wstring(newValue));
            return source;
        }

        [[nodiscard]] std::string narrowAscii(std::wstring_view value)
        {
            std::string result;
            result.reserve(value.size());
            for (wchar_t character : value)
            {
                result.push_back(static_cast<char>(character));
            }

            return result;
        }
    }

    TEST(GameDefinitionLoaderTests, EmbeddedDefinitionsContainSkyrimSpecialEdition)
    {
        const std::vector<GameDefinition> definitions = GameDefinitionLoader::loadEmbeddedDefinitions();

        ASSERT_EQ(definitions.size(), 1U);
        const GameDefinition& skyrim = definitions.front();
        EXPECT_EQ(skyrim.id.value(), L"skyrimse");
        EXPECT_EQ(skyrim.displayName, L"Skyrim Special Edition");
        EXPECT_EQ(skyrim.dataFolder, L"Data");
        EXPECT_TRUE(skyrim.capabilities.has(GameCapability::Plugins));
        EXPECT_TRUE(skyrim.capabilities.has(GameCapability::LoadOrder));
        EXPECT_TRUE(skyrim.capabilities.has(GameCapability::ScriptExtender));
        EXPECT_TRUE(containsExtension(skyrim.pluginExtensions, L".esp"));
        ASSERT_TRUE(skyrim.executableRoles.primary.has_value());
        EXPECT_EQ(skyrim.executableRoles.primary->normalizedName(), L"skyrimse.exe");
        ASSERT_FALSE(skyrim.detectionHints.executableNames.empty());
        EXPECT_EQ(skyrim.contentLayoutRules.dataFolder, L"Data");
        EXPECT_TRUE(skyrim.contentLayoutRules.supportsRootFiles);
        EXPECT_EQ(skyrim.contentLayoutRules.rootFileWrapperDirectory, L"root");
        EXPECT_TRUE(skyrim.vfsRules.supportsRootBuilder);
        EXPECT_EQ(skyrim.vfsRules.rootBuilderDirectoryName, L"root");
        EXPECT_EQ(skyrim.vfsRules.userSettingsDirectoryName, L"Skyrim Special Edition");
        EXPECT_NE(
            std::find(
                skyrim.vfsRules.profileIniFileNames.begin(),
                skyrim.vfsRules.profileIniFileNames.end(),
                L"SkyrimPrefs.ini"),
            skyrim.vfsRules.profileIniFileNames.end());
        EXPECT_NE(
            std::find(
                skyrim.vfsRules.excludedLaunchCacheDirectories.begin(),
                skyrim.vfsRules.excludedLaunchCacheDirectories.end(),
                L"NetScriptFramework"),
            skyrim.vfsRules.excludedLaunchCacheDirectories.end());
        ASSERT_EQ(skyrim.healthRules.requiredFiles.size(), 2U);
        ASSERT_TRUE(skyrim.launchRules.scriptExtender.has_value());
        EXPECT_EQ(skyrim.launchRules.scriptExtender->loaderExecutable.displayName(), L"skse64_loader.exe");
        ASSERT_EQ(skyrim.launchRules.scriptExtender->expectedChildProcessNames.size(), 1U);
        EXPECT_EQ(skyrim.launchRules.scriptExtender->expectedChildProcessNames.front().displayName(), L"SkyrimSE.exe");
        EXPECT_EQ(skyrim.launchRules.scriptExtender->handoffDisplayName, L"Skyrim Special Edition");
        EXPECT_EQ(skyrim.launchRules.scriptExtender->handoffTimeoutMs, 30000U);
        EXPECT_EQ(
            skyrim.launchRules.scriptExtender->launchTrackingKind,
            LaunchTrackingKind::ExpectedChildProcess);
    }

    TEST(GameDefinitionLoaderTests, EmbeddedSkyrimDefinitionCarriesParityRules)
    {
        const std::vector<GameDefinition> definitions = GameDefinitionLoader::loadEmbeddedDefinitions();

        ASSERT_EQ(definitions.size(), 1U);
        const GameDefinition& skyrim = definitions.front();

        EXPECT_TRUE(skyrim.capabilities.has(GameCapability::Plugins));
        EXPECT_TRUE(skyrim.capabilities.has(GameCapability::LoadOrder));
        EXPECT_TRUE(skyrim.capabilities.has(GameCapability::RootFiles));
        EXPECT_TRUE(skyrim.capabilities.has(GameCapability::Archives));
        EXPECT_TRUE(skyrim.capabilities.has(GameCapability::ScriptExtender));
        EXPECT_TRUE(skyrim.capabilities.has(GameCapability::IniProfiles));
        EXPECT_TRUE(skyrim.capabilities.has(GameCapability::SaveProfiles));
        EXPECT_TRUE(skyrim.capabilities.has(GameCapability::GameSpecificVfs));
        EXPECT_TRUE(skyrim.capabilities.has(GameCapability::ContentLayoutRules));

        EXPECT_TRUE(containsExtension(skyrim.archiveExtensions, L".bsa"));
        EXPECT_TRUE(containsExtension(skyrim.pluginExtensions, L".esm"));
        EXPECT_TRUE(containsExtension(skyrim.pluginExtensions, L".esp"));
        EXPECT_TRUE(containsExtension(skyrim.pluginExtensions, L".esl"));

        ASSERT_TRUE(skyrim.executableRoles.primary.has_value());
        ASSERT_TRUE(skyrim.executableRoles.launcher.has_value());
        ASSERT_TRUE(skyrim.executableRoles.scriptExtender.has_value());
        EXPECT_EQ(skyrim.executableRoles.primary->displayName(), L"SkyrimSE.exe");
        EXPECT_EQ(skyrim.executableRoles.launcher->displayName(), L"SkyrimSELauncher.exe");
        EXPECT_EQ(skyrim.executableRoles.scriptExtender->displayName(), L"skse64_loader.exe");
        const auto scriptExtenderExecutable = std::find_if(
            skyrim.executables.begin(),
            skyrim.executables.end(),
            [](const GameExecutableDefinition& executable)
            {
                return executable.role == GameExecutableRole::ScriptExtender;
            });
        ASSERT_NE(scriptExtenderExecutable, skyrim.executables.end());
        ASSERT_TRUE(scriptExtenderExecutable->workingDirectory.has_value());
        EXPECT_EQ(
            scriptExtenderExecutable->workingDirectory.value(),
            GameExecutableWorkingDirectoryKind::GameRoot);

        EXPECT_TRUE(containsString(skyrim.pluginRules.profileFiles, L"plugins.txt"));
        EXPECT_TRUE(containsString(skyrim.pluginRules.profileFiles, L"loadorder.txt"));
        EXPECT_TRUE(containsString(skyrim.pluginRules.basePlugins, L"Skyrim.esm"));
        EXPECT_TRUE(containsString(skyrim.pluginRules.basePlugins, L"Update.esm"));
        EXPECT_TRUE(containsString(skyrim.pluginRules.basePlugins, L"Dawnguard.esm"));
        EXPECT_TRUE(containsString(skyrim.pluginRules.basePlugins, L"HearthFires.esm"));
        EXPECT_TRUE(containsString(skyrim.pluginRules.basePlugins, L"Dragonborn.esm"));

        EXPECT_TRUE(skyrim.vfsRules.supportsRootBuilder);
        EXPECT_EQ(skyrim.vfsRules.rootBuilderDirectoryName, L"root");
        EXPECT_EQ(skyrim.vfsRules.userSettingsDirectoryName, L"Skyrim Special Edition");
        EXPECT_TRUE(containsString(skyrim.vfsRules.profileIniFileNames, L"Skyrim.ini"));
        EXPECT_TRUE(containsString(skyrim.vfsRules.profileIniFileNames, L"SkyrimCustom.ini"));
        EXPECT_TRUE(containsString(skyrim.vfsRules.profileIniFileNames, L"SkyrimPrefs.ini"));
        EXPECT_TRUE(containsString(skyrim.vfsRules.saveDirectoryNames, L"Saves"));
        EXPECT_TRUE(containsString(skyrim.vfsRules.excludedLaunchCacheDirectories, L"root"));
        EXPECT_TRUE(containsString(skyrim.vfsRules.excludedLaunchCacheDirectories, L"SKSE"));
        EXPECT_TRUE(containsString(skyrim.vfsRules.excludedLaunchCacheDirectories, L"DLLPlugins"));
        EXPECT_TRUE(containsString(skyrim.vfsRules.excludedLaunchCacheDirectories, L"Plugins"));
        EXPECT_TRUE(containsString(skyrim.vfsRules.excludedLaunchCacheDirectories, L"NetScriptFramework"));
    }

    TEST(GameDefinitionLoaderTests, NormalizesIdsDomainsAndExtensions)
    {
        const GameDefinition definition = GameDefinitionLoader::loadDefinition(minimalDefinition);

        EXPECT_EQ(definition.id.value(), L"examplegame");
        EXPECT_EQ(definition.uiTemplateId.value(), L"examplegame");
        ASSERT_EQ(definition.domains.size(), 1U);
        EXPECT_EQ(definition.domains.front(), L"exampledomain");
        EXPECT_TRUE(containsExtension(definition.archiveExtensions, L".zip"));
        EXPECT_TRUE(containsExtension(definition.archiveExtensions, L".7z"));
        EXPECT_TRUE(containsExtension(definition.pluginExtensions, L".esp"));
        EXPECT_EQ(definition.capabilities.bits(), 0U);
        ASSERT_EQ(definition.executables.size(), 1U);
        EXPECT_EQ(definition.executables.front().name.displayName(), L"Example.exe");
        EXPECT_EQ(definition.executables.front().name.normalizedName(), L"example.exe");
        ASSERT_TRUE(definition.executableRoles.primary.has_value());
        EXPECT_EQ(definition.executableRoles.primary->normalizedName(), L"example.exe");
    }

    TEST(GameDefinitionLoaderTests, RejectsUnknownFields)
    {
        std::wstring json(minimalDefinition);
        const std::wstring marker = L"\"healthRules\"";
        const std::size_t position = json.find(marker);
        ASSERT_NE(position, std::wstring::npos);
        json.insert(position, L"\"futureField\": true,");

        EXPECT_THROW((void)GameDefinitionLoader::loadDefinition(json), std::runtime_error);
    }

    TEST(GameDefinitionLoaderTests, RejectsMissingRequiredIdentityFields)
    {
        EXPECT_THROW(
            (void)GameDefinitionLoader::loadDefinition(
                replaceFirst(std::wstring(minimalDefinition), L"\"schemaVersion\": \"1\"", L"\"schemaVersion\": null")),
            std::runtime_error);
        EXPECT_THROW(
            (void)GameDefinitionLoader::loadDefinition(
                replaceFirst(
                    std::wstring(minimalDefinition),
                    L"\"definitionVersion\": \"1.0.0\"",
                    L"\"definitionVersion\": null")),
            std::runtime_error);
        EXPECT_THROW(
            (void)GameDefinitionLoader::loadDefinition(
                replaceFirst(std::wstring(minimalDefinition), L"\"id\": \"ExampleGame\"", L"\"id\": null")),
            std::runtime_error);
    }

    TEST(GameDefinitionLoaderTests, RejectsUnsupportedSchemaVersion)
    {
        EXPECT_THROW(
            (void)GameDefinitionLoader::loadDefinition(
                replaceFirst(std::wstring(minimalDefinition), L"\"schemaVersion\": \"1\"", L"\"schemaVersion\": \"2\"")),
            std::runtime_error);
    }

    TEST(GameDefinitionLoaderTests, RejectsMissingExplicitCapabilityFlags)
    {
        const std::wstring json = replaceFirst(
            std::wstring(minimalDefinition),
            L"\"supportsPlugins\": false,",
            L"");

        EXPECT_THROW((void)GameDefinitionLoader::loadDefinition(json), std::runtime_error);
    }

    TEST(GameDefinitionLoaderTests, RejectsEmptyRequiredFiles)
    {
        const std::wstring json = replaceFirst(
            std::wstring(minimalDefinition),
            L"\"requiredFiles\": [\"Example.exe\"]",
            L"\"requiredFiles\": []");

        EXPECT_THROW((void)GameDefinitionLoader::loadDefinition(json), std::runtime_error);
    }

    TEST(GameDefinitionLoaderTests, RejectsEmptyExecutableList)
    {
        const std::wstring json = replaceFirst(
            std::wstring(minimalDefinition),
            L"\"executables\": [{\"id\": \"game\", \"displayName\": \"Example\", \"name\": \"Example.exe\", \"role\": \"primary\"}]",
            L"\"executables\": []");

        EXPECT_THROW((void)GameDefinitionLoader::loadDefinition(json), std::runtime_error);
    }

    TEST(GameDefinitionLoaderTests, RejectsExecutableRolesThatDoNotReferenceDefinitions)
    {
        const std::wstring json = replaceFirst(
            std::wstring(minimalDefinition),
            L"\"executableRoles\": {\"primary\": \"Example.exe\"}",
            L"\"executableRoles\": {\"primary\": \"Other.exe\"}");

        EXPECT_THROW((void)GameDefinitionLoader::loadDefinition(json), std::runtime_error);
    }

    TEST(GameDefinitionLoaderTests, RejectsUnknownExecutableRole)
    {
        const std::wstring json = replaceFirst(
            std::wstring(minimalDefinition),
            L"\"role\": \"primary\"",
            L"\"role\": \"tool\"");

        EXPECT_THROW((void)GameDefinitionLoader::loadDefinition(json), std::runtime_error);
    }

    TEST(GameDefinitionLoaderTests, RejectsInvalidExecutableWorkingDirectory)
    {
        const std::wstring json = replaceFirst(
            std::wstring(minimalDefinition),
            L"\"role\": \"primary\"",
            L"\"role\": \"primary\", \"workingDirectory\": \"somewhereElse\"");

        EXPECT_THROW((void)GameDefinitionLoader::loadDefinition(json), std::runtime_error);
    }

    TEST(GameDefinitionLoaderTests, RejectsUnsafeRelativeDefinitionPaths)
    {
        const std::wstring json = replaceFirst(
            std::wstring(minimalDefinition),
            L"\"requiredFiles\": [\"Example.exe\"]",
            L"\"requiredFiles\": [\"../Example.exe\"]");

        EXPECT_THROW((void)GameDefinitionLoader::loadDefinition(json), std::runtime_error);
    }

    TEST(GameDefinitionLoaderTests, RejectsInvalidLaunchTrackingKind)
    {
        const std::wstring json = replaceFirst(
            std::wstring(minimalDefinition),
            L"\"launchRules\": {}",
            LR"json("launchRules": {
                "scriptExtender": {
                    "name": "Example Extender",
                    "loaderExecutable": "Example.exe",
                    "launchTrackingKind": "teleport"
                }
            })json");

        EXPECT_THROW((void)GameDefinitionLoader::loadDefinition(json), std::runtime_error);
    }

    TEST(GameDefinitionLoaderTests, RejectsDuplicateNormalizedIds)
    {
        std::wstring duplicate(minimalDefinition);
        const std::wstring oldId = L"\"id\": \"ExampleGame\"";
        const std::size_t position = duplicate.find(oldId);
        ASSERT_NE(position, std::wstring::npos);
        duplicate.replace(position, oldId.size(), L"\"id\": \"examplegame\"");

        EXPECT_THROW(
            (void)GameDefinitionLoader::loadDefinitionsFromJsonStrings({
                std::wstring(minimalDefinition),
                duplicate
            }),
            std::runtime_error);
    }

    TEST(GameDefinitionLoaderTests, DevOverrideInvalidDefinitionKeepsEmbeddedOfficialDefinition)
    {
        TempDirectory temp;
        writeTextFile(
            temp.path() / "skyrimse.json",
            R"json({
                "schemaVersion": null,
                "definitionVersion": "99.0.0",
                "id": "skyrimse"
            })json");

        const GameDefinitionOverrideLoadResult result =
            GameDefinitionLoader::loadEmbeddedDefinitionsWithDevOverrides(temp.path());

        EXPECT_FALSE(result.overrideApplied);
        ASSERT_FALSE(result.diagnostics.empty());
        ASSERT_EQ(result.definitions.size(), 1U);
        EXPECT_EQ(result.definitions.front().id.value(), L"skyrimse");
        EXPECT_EQ(result.definitions.front().displayName, L"Skyrim Special Edition");
    }

    TEST(GameDefinitionLoaderTests, DevOverrideAddsDefinitionsWithoutDeletingEmbeddedOfficialDefinition)
    {
        TempDirectory temp;
        writeTextFile(temp.path() / "example.json", narrowAscii(minimalDefinition));

        const GameDefinitionOverrideLoadResult result =
            GameDefinitionLoader::loadEmbeddedDefinitionsWithDevOverrides(temp.path());

        EXPECT_TRUE(result.overrideApplied);
        EXPECT_TRUE(result.diagnostics.empty());
        ASSERT_EQ(result.definitions.size(), 2U);
        EXPECT_NE(
            std::find_if(
                result.definitions.begin(),
                result.definitions.end(),
                [](const GameDefinition& definition)
                {
                    return definition.id.value() == L"skyrimse";
                }),
            result.definitions.end());
        EXPECT_NE(
            std::find_if(
                result.definitions.begin(),
                result.definitions.end(),
                [](const GameDefinition& definition)
                {
                    return definition.id.value() == L"examplegame";
                }),
            result.definitions.end());
    }
}
