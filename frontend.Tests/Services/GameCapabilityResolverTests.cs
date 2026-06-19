using Fluxora.App.Models;
using Fluxora.App.Services;

namespace Fluxora.App.Tests.Services;

public sealed class GameCapabilityResolverTests
{
    [Fact]
    public void ForProject_HidesUnsupportedOptionalPanels()
    {
        ModProject project = ProjectWithCapabilities(new GameCapabilities());

        UiCapabilityState state = GameCapabilityResolver.ForProject(project);

        Assert.False(state.SupportsPluginSection);
        Assert.False(state.SupportsLoadOrder);
        Assert.False(state.SupportsIniProfiles);
        Assert.False(state.SupportsSaveProfiles);
        Assert.False(state.SupportsScriptExtender);
        Assert.False(state.SupportsRootFiles);
        Assert.False(state.SupportsExecutablePanel);
        Assert.False(state.SupportsContentLayoutReview);
        Assert.False(state.SupportsHealthDiagnostics);
    }

    [Fact]
    public void ForProject_UsesTypedGameCapabilitiesForUiPanels()
    {
        ModProject project = ProjectWithCapabilities(new GameCapabilities
        {
            SupportsPlugins = true,
            SupportsLoadOrder = true,
            SupportsIniProfiles = true,
            SupportsSaveProfiles = true,
            SupportsScriptExtender = true,
            SupportsRootFiles = true,
            SupportsContentLayoutRules = true
        });
        project.ContentLayoutSummary = new ContentLayoutSummary
        {
            Supported = true,
            Summary = "Data folder content is placed under the game data directory."
        };
        project.GameHealthSummary = new GameHealthSummary
        {
            Status = "healthy",
            Summary = "Required files were found."
        };

        UiCapabilityState state = GameCapabilityResolver.ForProject(project);

        Assert.True(state.SupportsPluginSection);
        Assert.True(state.SupportsLoadOrder);
        Assert.True(state.SupportsIniProfiles);
        Assert.True(state.SupportsSaveProfiles);
        Assert.True(state.SupportsScriptExtender);
        Assert.True(state.SupportsRootFiles);
        Assert.True(state.SupportsContentLayoutReview);
        Assert.True(state.SupportsHealthDiagnostics);
    }

    [Fact]
    public void ForProject_ExistingSkyrimWorkspaceShowsExpectedPanels()
    {
        ModProject project = new()
        {
            Id = "project",
            Name = "Skyrim Build",
            GameName = "Skyrim Special Edition",
            GamePath = @"C:\Games\Skyrim Special Edition",
            InstallRootDirectory = @"C:\Fluxora",
            ProjectDirectory = @"C:\Fluxora\Skyrim Build",
            TemplateId = "skyrimse",
            UiTemplateId = "skyrimse",
            GameCapabilities = new GameCapabilities
            {
                SupportsPlugins = true,
                SupportsLoadOrder = true,
                SupportsIniProfiles = true,
                SupportsSaveProfiles = true,
                SupportsScriptExtender = true,
                SupportsRootFiles = true,
                SupportsContentLayoutRules = true
            },
            Template = new ResolvedTemplate
            {
                Id = "skyrimse",
                UiTemplateId = "skyrimse",
                PluginExtensions = [".esm", ".esp", ".esl"],
                ScriptExtender = new ScriptExtenderInfo
                {
                    Name = "Skyrim Script Extender (SKSE64)",
                    LoaderExecutable = "skse64_loader.exe"
                }
            }
        };
        project.Executables =
        [
            new GameExecutableEntry
            {
                Id = "skse",
                DisplayName = "SKSE64",
                ExecutablePath = "skse64_loader.exe"
            }
        ];
        project.ContentLayoutSummary = new ContentLayoutSummary
        {
            Supported = true,
            DataFolder = "Data",
            RootFileWrapperDirectory = "root"
        };
        project.GameHealthSummary = new GameHealthSummary
        {
            Status = "warning",
            Summary = "SKSE loader is optional."
        };

        UiCapabilityState state = GameCapabilityResolver.ForProject(project);

        Assert.True(state.SupportsPluginSection);
        Assert.True(state.SupportsLoadOrder);
        Assert.True(state.SupportsIniProfiles);
        Assert.True(state.SupportsSaveProfiles);
        Assert.True(state.SupportsScriptExtender);
        Assert.True(state.SupportsRootFiles);
        Assert.True(state.SupportsExecutablePanel);
        Assert.True(state.SupportsContentLayoutReview);
        Assert.True(state.SupportsHealthDiagnostics);
    }

    [Fact]
    public void ForProject_ContentLayoutDiagnosticsEnableReviewPanel()
    {
        ModProject project = ProjectWithCapabilities(new GameCapabilities());
        project.ContentLayoutSummary = new ContentLayoutSummary
        {
            HasWarnings = true,
            HasBlockers = true,
            Warnings = ["Unknown top-level file will be reviewed."],
            Blockers = ["Path traversal was blocked."]
        };

        UiCapabilityState state = GameCapabilityResolver.ForProject(project);

        Assert.True(state.SupportsContentLayoutReview);
    }

    [Fact]
    public void ForProject_HandlesExplicitNullBridgeLists()
    {
        ModProject project = new()
        {
            Id = "project",
            Name = "Project",
            GameName = "Example Game",
            GamePath = @"C:\Games\Example",
            InstallRootDirectory = @"C:\Fluxora",
            ProjectDirectory = @"C:\Fluxora\Project",
            GameCapabilities = new GameCapabilities
            {
                Enabled = null!
            },
            Template = new ResolvedTemplate
            {
                Capabilities = null!
            }
        };
        project.ContentLayoutSummary = new ContentLayoutSummary
        {
            Warnings = null!,
            Blockers = null!
        };
        project.GameHealthSummary = new GameHealthSummary
        {
            MatchedFiles = null!,
            MissingFiles = null!,
            Warnings = null!,
            Findings = null!
        };

        UiCapabilityState state = GameCapabilityResolver.ForProject(project);

        Assert.False(state.SupportsPluginSection);
        Assert.False(state.SupportsContentLayoutReview);
        Assert.False(state.SupportsHealthDiagnostics);
    }

    [Fact]
    public void ForProject_UsesTemplateCapabilitiesAsCompatibilityProjection()
    {
        ModProject project = new()
        {
            Id = "project",
            Name = "Project",
            GameName = "Example Game",
            GamePath = @"C:\Games\Example",
            InstallRootDirectory = @"C:\Fluxora",
            ProjectDirectory = @"C:\Fluxora\Project",
            GameCapabilities = new GameCapabilities(),
            Template = new ResolvedTemplate
            {
                Capabilities =
                [
                    new TemplateCapability { Id = "plugins" },
                    new TemplateCapability { Id = "load-order" },
                    new TemplateCapability { Id = "ini-tweaks" },
                    new TemplateCapability { Id = "save-games" },
                    new TemplateCapability { Id = "script-extender" }
                ]
            }
        };

        UiCapabilityState state = GameCapabilityResolver.ForProject(project);

        Assert.True(state.SupportsPlugins);
        Assert.True(state.SupportsLoadOrder);
        Assert.True(state.SupportsIniProfiles);
        Assert.True(state.SupportsSaveProfiles);
        Assert.True(state.SupportsScriptExtender);
    }

    [Fact]
    public void ShouldRequestPluginSection_ReturnsFalseWhenPluginsAndLoadOrderUnsupported()
    {
        ModProject project = ProjectWithCapabilities(new GameCapabilities());

        Assert.False(ProjectWorkspaceLoadService.ShouldRequestPluginSection(project));
    }

    [Fact]
    public void ShouldRequestPluginSection_ReturnsTrueWhenLoadOrderSupported()
    {
        ModProject project = ProjectWithCapabilities(new GameCapabilities
        {
            SupportsLoadOrder = true
        });

        Assert.True(ProjectWorkspaceLoadService.ShouldRequestPluginSection(project));
    }

    private static ModProject ProjectWithCapabilities(GameCapabilities capabilities)
    {
        return new ModProject
        {
            Id = "project",
            Name = "Project",
            GameName = "Example Game",
            GamePath = @"C:\Games\Example",
            InstallRootDirectory = @"C:\Fluxora",
            ProjectDirectory = @"C:\Fluxora\Project",
            GameCapabilities = capabilities
        };
    }
}
