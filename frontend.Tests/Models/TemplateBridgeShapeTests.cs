using System.Text.Json;
using Fluxora.App.Models;

namespace Fluxora.App.Tests.Models;

public sealed class TemplateBridgeShapeTests
{
    private static readonly JsonSerializerOptions BridgeJsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    [Fact]
    public void GameTemplateOptionDeserializesCurrentBridgeShape()
    {
        const string json = """
            {
              "id": "skyrimse",
              "displayName": "Skyrim Special Edition",
              "gameName": "Skyrim Special Edition",
              "summary": "Registry-projected template"
            }
            """;

        GameTemplateOption? option = JsonSerializer.Deserialize<GameTemplateOption>(json, BridgeJsonOptions);

        Assert.NotNull(option);
        Assert.Equal("skyrimse", option.Id);
        Assert.Equal("Skyrim Special Edition", option.DisplayName);
        Assert.Equal("Skyrim Special Edition", option.GameName);
        Assert.Equal("Registry-projected template", option.Summary);
    }

    [Fact]
    public void GameTemplateOptionDeserializesExpandedBridgeShape()
    {
        const string json = """
            {
              "id": "skyrimse",
              "displayName": "Skyrim Special Edition",
              "gameName": "Skyrim Special Edition",
              "summary": "Registry-projected template",
              "uiTemplateId": "skyrimse",
              "gameCapabilities": {
                "bits": 511,
                "supportsPlugins": true,
                "supportsLoadOrder": true,
                "supportsRootFiles": true,
                "supportsArchives": true,
                "supportsScriptExtender": true,
                "supportsIniProfiles": true,
                "supportsSaveProfiles": true,
                "supportsGameSpecificVfs": true,
                "supportsContentLayoutRules": true,
                "enabled": ["plugins", "loadOrder", "contentLayoutRules"]
              },
              "archiveExtensions": [".bsa"],
              "requiredFiles": ["SkyrimSE.exe", "Data/Skyrim.esm"]
            }
            """;

        GameTemplateOption? option = JsonSerializer.Deserialize<GameTemplateOption>(json, BridgeJsonOptions);

        Assert.NotNull(option);
        Assert.Equal("skyrimse", option.UiTemplateId);
        Assert.True(option.GameCapabilities.SupportsPlugins);
        Assert.True(option.GameCapabilities.SupportsContentLayoutRules);
        Assert.Contains(".bsa", option.ArchiveExtensions);
        Assert.Contains("SkyrimSE.exe", option.RequiredFiles);
    }

    [Fact]
    public void ResolvedTemplateDeserializesDeprecatedCompatibilityFields()
    {
        const string json = """
            {
              "id": "skyrimse",
              "displayName": "Skyrim Special Edition",
              "gameName": "Skyrim Special Edition",
              "summary": "Registry-projected template",
              "baseTemplateId": "base",
              "defaultProfile": "Default",
              "dataDirectory": "Data",
              "nexusDomain": "skyrimspecialedition",
              "folders": ["mods", "downloads", "profiles"],
              "profileFiles": ["modlist.txt", "plugins.txt", "loadorder.txt"],
              "basePlugins": ["Skyrim.esm", "Update.esm"],
              "pluginExtensions": [".esm", ".esp", ".esl"],
              "executables": ["SkyrimSE.exe", "skse64_loader.exe"],
              "capabilities": [
                {
                  "id": "plugins",
                  "displayName": "Plugins",
                  "description": "Plugin management"
                }
              ],
              "scriptExtender": {
                "name": "Skyrim Script Extender (SKSE64)",
                "loaderExecutable": "skse64_loader.exe",
                "website": "https://skse.silverlock.org/"
              }
            }
            """;

        ResolvedTemplate? template = JsonSerializer.Deserialize<ResolvedTemplate>(json, BridgeJsonOptions);

        Assert.NotNull(template);
        Assert.Equal("skyrimse", template.Id);
        Assert.Equal("base", template.BaseTemplateId);
        Assert.Equal("Default", template.DefaultProfile);
        Assert.Equal("Data", template.DataDirectory);
        Assert.Equal("skyrimspecialedition", template.NexusDomain);
        Assert.Contains("plugins.txt", template.ProfileFiles);
        Assert.Contains(".esp", template.PluginExtensions);
        Assert.Contains("SkyrimSE.exe", template.Executables);
        Assert.True(template.HasBasePlugins);
        Assert.True(template.HasScriptExtender);
        Assert.Single(template.Capabilities);
        Assert.Equal("plugins", template.Capabilities[0].Id);
        Assert.Equal("skse64_loader.exe", template.ScriptExtender?.LoaderExecutable);
    }

    [Fact]
    public void ResolvedTemplateDeserializesExpandedGameDefinitionFields()
    {
        const string json = """
            {
              "id": "skyrimse",
              "displayName": "Skyrim Special Edition",
              "gameName": "Skyrim Special Edition",
              "uiTemplateId": "skyrimse",
              "pluginExtensions": [".esm", ".esp", ".esl"],
              "archiveExtensions": [".bsa"],
              "requiredFiles": ["SkyrimSE.exe", "Data/Skyrim.esm"],
              "gameCapabilities": {
                "supportsPlugins": true,
                "supportsArchives": true,
                "supportsContentLayoutRules": true
              },
              "contentLayoutSummary": {
                "supported": true,
                "hasWarnings": true,
                "hasBlockers": true,
                "dataFolder": "Data",
                "supportsRootFiles": true,
                "rootFileWrapperDirectory": "Root",
                "pluginExtensions": [".esm", ".esp", ".esl"],
                "archiveExtensions": [".bsa"],
                "scriptExtenderLoaders": ["skse64_loader.exe"],
                "summary": "Content placement is driven by the selected game definition.",
                "details": ["Game data content is placed under Data."],
                "warnings": ["Unknown root files require review."],
                "blockers": ["Path traversal was blocked."]
              },
              "executableDisplayMetadata": [
                {
                  "id": "skse",
                  "displayName": "SKSE64",
                  "executableName": "skse64_loader.exe",
                  "role": "scriptExtender",
                  "workingDirectoryKind": "gameRoot",
                  "isScriptExtender": true
                }
              ],
              "launchTrackingMetadata": {
                "kind": "expectedChildProcess",
                "expectedChildProcessNames": ["SkyrimSE.exe"],
                "handoffDisplayName": "Skyrim Special Edition",
                "handoffTimeoutMs": 30000
              }
            }
            """;

        ResolvedTemplate? template = JsonSerializer.Deserialize<ResolvedTemplate>(json, BridgeJsonOptions);

        Assert.NotNull(template);
        Assert.Equal("skyrimse", template.UiTemplateId);
        Assert.True(template.GameCapabilities.SupportsArchives);
        Assert.Contains(".bsa", template.ArchiveExtensions);
        Assert.Contains("Data/Skyrim.esm", template.RequiredFiles);
        Assert.True(template.ContentLayoutSummary.Supported);
        Assert.True(template.ContentLayoutSummary.HasWarnings);
        Assert.True(template.ContentLayoutSummary.HasBlockers);
        Assert.Equal("Data", template.ContentLayoutSummary.DataFolder);
        Assert.Contains("Unknown root files require review.", template.ContentLayoutSummary.Warnings);
        Assert.Contains("Path traversal was blocked.", template.ContentLayoutSummary.Blockers);
        Assert.Single(template.ExecutableDisplayMetadata);
        Assert.True(template.ExecutableDisplayMetadata[0].IsScriptExtender);
        Assert.Equal("expectedChildProcess", template.LaunchTrackingMetadata.Kind);
        Assert.Contains("SkyrimSE.exe", template.LaunchTrackingMetadata.ExpectedChildProcessNames);
    }

    [Fact]
    public void ModProjectDeserializesOldBridgeShapeWithoutNewOptionalFields()
    {
        const string json = """
            {
              "id": "C:/Fluxora/Builds/Foundation.json",
              "name": "Foundation",
              "templateId": "skyrimse",
              "gameName": "Skyrim Special Edition",
              "gamePath": "C:/Games/Skyrim",
              "installRootDirectory": "C:/Fluxora",
              "projectDirectory": "C:/Fluxora/Foundation",
              "configPath": "C:/Fluxora/Builds/Foundation.json",
              "paths": {
                "gameDirectory": "C:/Games/Skyrim",
                "modsDirectory": "C:/Fluxora/Foundation/mods",
                "profilesDirectory": "C:/Fluxora/Foundation/profiles",
                "downloadsDirectory": "C:/Fluxora/Foundation/downloads",
                "overwriteDirectory": "C:/Fluxora/Foundation/overwrite"
              },
              "executables": [],
              "template": {
                "id": "skyrimse",
                "displayName": "Skyrim Special Edition"
              }
            }
            """;

        ModProject? project = JsonSerializer.Deserialize<ModProject>(json, BridgeJsonOptions);

        Assert.NotNull(project);
        Assert.Equal("skyrimse", project.TemplateId);
        Assert.NotNull(project.GameCapabilities);
        Assert.False(project.GameCapabilities.SupportsPlugins);
        Assert.NotNull(project.GameHealthSummary);
        Assert.Equal("unknown", project.GameHealthSummary.Status);
        Assert.Null(project.ProjectFingerprint);
        Assert.NotNull(project.ContentLayoutSummary);
    }

    [Fact]
    public void ModProjectDeserializesExpandedBridgeShape()
    {
        const string json = """
            {
              "id": "C:/Fluxora/Builds/Foundation.json",
              "name": "Foundation",
              "templateId": "skyrimse",
              "uiTemplateId": "skyrimse",
              "gameName": "Skyrim Special Edition",
              "gamePath": "C:/Games/Skyrim",
              "installRootDirectory": "C:/Fluxora",
              "projectDirectory": "C:/Fluxora/Foundation",
              "configPath": "C:/Fluxora/Builds/Foundation.json",
              "gameCapabilities": {
                "supportsPlugins": true,
                "supportsLoadOrder": true,
                "supportsContentLayoutRules": true
              },
              "gameHealthSummary": {
                "gameId": "skyrimse",
                "displayName": "Skyrim Special Edition",
                "status": "warning",
                "summary": "Health status recorded when the project was created.",
                "hasBlockers": false,
                "allowsAutomation": true,
                "matchedFiles": ["SkyrimSE.exe"],
                "missingFiles": [],
                "warnings": ["Script extender missing"],
                "findings": [
                  {
                    "severity": "warning",
                    "code": "missing-script-extender",
                    "message": "SKSE is optional.",
                    "path": "skse64_loader.exe",
                    "critical": false
                  }
                ]
              },
              "projectFingerprint": {
                "algorithm": "fluxora-project-v1",
                "gameId": "skyrimse",
                "gameDisplayName": "Skyrim Special Edition",
                "gameDefinitionVersion": "1",
                "definitionBundleVersion": "embedded-1",
                "supportModuleVersion": "1",
                "selectedInstallPath": "C:/Games/Skyrim",
                "canonicalInstallPath": "C:/Games/Skyrim",
                "selectedExecutable": "SkyrimSE.exe",
                "detectedStoreSource": "manual-path",
                "detectionSource": "manual-path",
                "detectionConfidence": "explicit",
                "healthStatusAtCreation": "warning",
                "gameVersion": "",
                "timestamp": "2026-06-17T00:00:00Z"
              },
              "contentLayoutSummary": {
                "supported": true,
                "hasWarnings": true,
                "hasBlockers": true,
                "dataFolder": "Data",
                "supportsRootFiles": true,
                "archiveExtensions": [".bsa"],
                "details": ["Game data content is placed under Data."],
                "warnings": ["Loose root files require review."],
                "blockers": ["Unexpected executable was blocked."]
              },
              "paths": {},
              "executables": [
                {
                  "id": "skse",
                  "displayName": "SKSE",
                  "executablePath": "skse64_loader.exe",
                  "executableDisplayMetadata": {
                    "id": "skse",
                    "displayName": "SKSE64",
                    "executableName": "skse64_loader.exe",
                    "role": "scriptExtender",
                    "isScriptExtender": true
                  }
                }
              ],
              "template": {
                "id": "skyrimse",
                "uiTemplateId": "skyrimse",
                "gameCapabilities": { "supportsPlugins": true }
              }
            }
            """;

        ModProject? project = JsonSerializer.Deserialize<ModProject>(json, BridgeJsonOptions);

        Assert.NotNull(project);
        Assert.Equal("skyrimse", project.UiTemplateId);
        Assert.True(project.GameCapabilities.SupportsPlugins);
        Assert.Equal("warning", project.GameHealthSummary.Status);
        Assert.Single(project.GameHealthSummary.Findings);
        Assert.Equal("skyrimse", project.ProjectFingerprint?.GameId);
        Assert.True(project.ContentLayoutSummary.Supported);
        Assert.True(project.ContentLayoutSummary.HasWarnings);
        Assert.True(project.ContentLayoutSummary.HasBlockers);
        Assert.Contains("Loose root files require review.", project.ContentLayoutSummary.Warnings);
        Assert.Contains("Unexpected executable was blocked.", project.ContentLayoutSummary.Blockers);
        Assert.Single(project.Executables);
        Assert.True(project.Executables[0].ExecutableDisplayMetadata.IsScriptExtender);
        Assert.True(project.Template?.GameCapabilities.SupportsPlugins);
    }

    [Fact]
    public void ContentLayoutPreviewDeserializesPlacementPlanShape()
    {
        const string json = """
            {
              "gameId": "skyrimse",
              "gameDisplayName": "Skyrim Special Edition",
              "rootFileWrapperDirectory": "root",
              "canInstall": true,
              "summary": {
                "supported": true,
                "hasWarnings": true,
                "hasBlockers": false,
                "totalEntries": 2,
                "plannedEntries": 2,
                "pluginEntries": 1,
                "archiveEntries": 0,
                "scriptExtenderEntries": 1,
                "unknownEntries": 1,
                "unsafeEntries": 0
              },
              "entries": [
                {
                  "sourcePath": "Data/SkyUI_SE.esp",
                  "target": "data",
                  "contentArea": "data",
                  "targetRelativePath": "SkyUI_SE.esp",
                  "classification": "plugin",
                  "explanation": "Plugin extension matches the selected game's plugin rules.",
                  "manualOverrideAllowed": true,
                  "safeManualTargets": ["data"]
                }
              ],
              "validationFindings": [
                {
                  "severity": "warning",
                  "path": "mystery.payload",
                  "classification": "unknown",
                  "message": "Archive contains content Fluxora cannot confidently classify.",
                  "blocksInstall": false
                }
              ],
              "explanationSummary": "Fluxora built a content placement plan for Skyrim Special Edition.",
              "explanationDetails": ["Data/SkyUI_SE.esp -> game data: plugin."]
            }
            """;

        ContentLayoutPreview? preview = JsonSerializer.Deserialize<ContentLayoutPreview>(json, BridgeJsonOptions);

        Assert.NotNull(preview);
        Assert.True(preview.CanInstall);
        Assert.Equal("skyrimse", preview.GameId);
        Assert.Equal(2, preview.Summary.TotalEntries);
        Assert.Single(preview.Entries);
        Assert.Equal("Data/SkyUI_SE.esp", preview.Entries[0].SourcePath);
        Assert.Equal("data", preview.Entries[0].Target);
        Assert.Contains("plugin rules", preview.Entries[0].Explanation);
        Assert.Single(preview.ValidationFindings);
        Assert.Contains("content placement plan", preview.ExplanationSummary);
    }

    [Fact]
    public void GameExecutableLaunchResultDeserializesOldAndNewLaunchTrackingShape()
    {
        const string oldJson = """
            {
              "id": "skse",
              "displayName": "SKSE",
              "executablePath": "skse64_loader.exe",
              "resolvedExecutablePath": "C:/Games/Skyrim/skse64_loader.exe",
              "resolvedWorkingDirectory": "C:/Games/Skyrim",
              "launchTrackingKind": "expectedChildProcess",
              "expectedChildProcessNames": ["SkyrimSE.exe"],
              "handoffDisplayName": "Skyrim Special Edition",
              "handoffTimeoutMs": 30000,
              "processId": 42
            }
            """;
        const string newJson = """
            {
              "id": "skse",
              "displayName": "SKSE",
              "executablePath": "skse64_loader.exe",
              "resolvedExecutablePath": "C:/Games/Skyrim/skse64_loader.exe",
              "resolvedWorkingDirectory": "C:/Games/Skyrim",
              "launchTrackingMetadata": {
                "kind": "expectedChildProcess",
                "expectedChildProcessNames": ["SkyrimSE.exe"],
                "handoffDisplayName": "Skyrim Special Edition",
                "handoffTimeoutMs": 30000
              },
              "executableDisplayMetadata": {
                "id": "skse",
                "displayName": "SKSE64",
                "executableName": "skse64_loader.exe",
                "role": "scriptExtender",
                "isScriptExtender": true
              },
              "processId": 42
            }
            """;

        GameExecutableLaunchResult? oldResult =
            JsonSerializer.Deserialize<GameExecutableLaunchResult>(oldJson, BridgeJsonOptions);
        GameExecutableLaunchResult? newResult =
            JsonSerializer.Deserialize<GameExecutableLaunchResult>(newJson, BridgeJsonOptions);

        Assert.NotNull(oldResult);
        Assert.Equal("expectedChildProcess", oldResult.LaunchTrackingKind);
        Assert.Contains("SkyrimSE.exe", oldResult.ExpectedChildProcessNames);
        Assert.NotNull(newResult);
        Assert.Equal("expectedChildProcess", newResult.LaunchTrackingMetadata.Kind);
        Assert.Contains("SkyrimSE.exe", newResult.LaunchTrackingMetadata.ExpectedChildProcessNames);
        Assert.True(newResult.ExecutableDisplayMetadata.IsScriptExtender);
    }
}
