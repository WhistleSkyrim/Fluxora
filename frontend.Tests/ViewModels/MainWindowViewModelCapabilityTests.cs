using Fluxora.App.Models;
using Fluxora.App.Services;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class MainWindowViewModelCapabilityTests
{
    [Fact]
    public void UnsupportedWorkspaceHidesUnsupportedPanels()
    {
        MainWindowViewModel viewModel = CreateViewModel();

        viewModel.SelectedProject = ProjectWithCapabilities(new GameCapabilities());

        Assert.False(viewModel.CanShowPluginsPanel);
        Assert.False(viewModel.CanShowLoadOrderPanel);
        Assert.False(viewModel.CanShowIniPanel);
        Assert.False(viewModel.CanShowSavePanel);
        Assert.False(viewModel.CanShowScriptExtenderPanel);
        Assert.False(viewModel.CanShowRootFilesPanel);
        Assert.False(viewModel.CanShowExecutablePanel);
        Assert.False(viewModel.CanShowContentLayoutReviewPanel);
        Assert.False(viewModel.CanShowHealthDiagnosticsPanel);
    }

    [Fact]
    public void ExistingSkyrimWorkspaceRendersExpectedPanels()
    {
        MainWindowViewModel viewModel = CreateViewModel();
        ModProject project = ProjectWithCapabilities(
            new GameCapabilities
            {
                SupportsPlugins = true,
                SupportsLoadOrder = true,
                SupportsIniProfiles = true,
                SupportsSaveProfiles = true,
                SupportsScriptExtender = true,
                SupportsRootFiles = true,
                SupportsContentLayoutRules = true
            },
            new ResolvedTemplate
            {
                Id = "skyrimse",
                UiTemplateId = "skyrimse",
                PluginExtensions = [".esm", ".esp", ".esl"],
                ScriptExtender = new ScriptExtenderInfo
                {
                    Name = "Skyrim Script Extender (SKSE64)",
                    LoaderExecutable = "skse64_loader.exe"
                }
            });
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
            Summary = "Script extender loader is optional."
        };

        viewModel.SelectedProject = project;

        Assert.True(viewModel.CanShowPluginsPanel);
        Assert.True(viewModel.CanShowLoadOrderPanel);
        Assert.True(viewModel.CanShowIniPanel);
        Assert.True(viewModel.CanShowSavePanel);
        Assert.True(viewModel.CanShowScriptExtenderPanel);
        Assert.True(viewModel.CanShowRootFilesPanel);
        Assert.True(viewModel.CanShowExecutablePanel);
        Assert.True(viewModel.CanShowContentLayoutReviewPanel);
        Assert.True(viewModel.CanShowHealthDiagnosticsPanel);
    }

    [Fact]
    public void ContentLayoutReviewRendersWarningsAndBlockers()
    {
        MainWindowViewModel viewModel = CreateViewModel();
        ModProject project = ProjectWithCapabilities(new GameCapabilities());
        project.ContentLayoutSummary = new ContentLayoutSummary
        {
            HasWarnings = true,
            HasBlockers = true,
            Summary = "Content layout requires review.",
            Details = ["Plugin content is placed under Data."],
            Warnings = ["Unknown root file requires review."],
            Blockers = ["Path traversal was blocked."]
        };

        viewModel.SelectedProject = project;

        Assert.True(viewModel.CanShowContentLayoutReviewPanel);
        Assert.Equal("Content layout requires review.", viewModel.SelectedProjectContentLayoutSummaryText);
        Assert.Contains("Plugin content is placed under Data.", viewModel.SelectedProjectContentLayoutDetails);
        Assert.Contains("Unknown root file requires review.", viewModel.SelectedProjectContentLayoutWarnings);
        Assert.Contains("Path traversal was blocked.", viewModel.SelectedProjectContentLayoutBlockers);
    }

    [Fact]
    public void UnsupportedWorkspaceCoercesHiddenBuildTab()
    {
        MainWindowViewModel viewModel = CreateViewModel();
        viewModel.SelectedProject = ProjectWithCapabilities(new GameCapabilities());

        viewModel.SelectedWorkspaceTabIndex = 3;

        Assert.Equal(1, viewModel.SelectedWorkspaceTabIndex);
        Assert.False(viewModel.CanShowBuildOverviewPanel);
    }

    [Fact]
    public void ContentLayoutReviewHidesEmptyDataFolderText()
    {
        MainWindowViewModel viewModel = CreateViewModel();
        ModProject project = ProjectWithCapabilities(new GameCapabilities());
        project.ContentLayoutSummary = new ContentLayoutSummary
        {
            HasWarnings = true,
            Warnings = ["Layout needs review."]
        };

        viewModel.SelectedProject = project;

        Assert.True(viewModel.CanShowContentLayoutReviewPanel);
        Assert.True(viewModel.CanShowBuildOverviewPanel);
        Assert.False(viewModel.HasSelectedProjectContentLayoutDataFolder);
    }

    private static MainWindowViewModel CreateViewModel()
    {
        ApplicationLogService logService = new();
        CoreBridgeService coreBridgeService = new(logService);
        SettingsService settingsService = new();
        LanguageCatalogService languageCatalogService = new(coreBridgeService);
        ProjectCatalogService projectCatalogService = new(coreBridgeService, settingsService);
        ProjectOpenService projectOpenService = new(projectCatalogService, coreBridgeService);
        ModCatalogService modCatalogService = new(coreBridgeService);
        PluginCatalogService pluginCatalogService = new(coreBridgeService);
        DownloadCatalogService downloadCatalogService = new(coreBridgeService);
        ProjectWorkspaceLoadService workspaceLoadService = new(
            modCatalogService,
            pluginCatalogService,
            downloadCatalogService);
        NxmProtocolService nxmProtocolService = new(coreBridgeService);
        TemplateCatalogService templateCatalogService = new(coreBridgeService);

        return new MainWindowViewModel(
            projectCatalogService,
            projectOpenService,
            modCatalogService,
            pluginCatalogService,
            downloadCatalogService,
            workspaceLoadService,
            nxmProtocolService,
            templateCatalogService,
            coreBridgeService,
            settingsService,
            languageCatalogService,
            logService,
            new NullFolderPickerService(),
            new NullExecutablePickerService(),
            new NullBuildConfigPickerService(),
            new NullModArchivePickerService(),
            new NullModInstallDialogService(),
            new NullExecutableManagerDialogService(),
            new NullBuildSettingsDialogService(),
            new NullBuildDeletionDialogService());
    }

    private static ModProject ProjectWithCapabilities(
        GameCapabilities capabilities,
        ResolvedTemplate? template = null)
    {
        return new ModProject
        {
            Id = "project",
            Name = "Project",
            GameName = "Example Game",
            GamePath = @"C:\Games\Example",
            InstallRootDirectory = @"C:\Fluxora",
            ProjectDirectory = @"C:\Fluxora\Project",
            GameCapabilities = capabilities,
            Template = template
        };
    }

    private sealed class NullFolderPickerService : IFolderPickerService
    {
        public string? PickFolder(string title, string selectedPath) => null;
    }

    private sealed class NullExecutablePickerService : IExecutablePickerService
    {
        public string? PickExecutable(string title, string selectedPath) => null;
    }

    private sealed class NullBuildConfigPickerService : IBuildConfigPickerService
    {
        public string? PickBuildConfig(string selectedDirectory) => null;
    }

    private sealed class NullModArchivePickerService : IModArchivePickerService
    {
        public string? PickArchive(string selectedDirectory) => null;
    }

    private sealed class NullModInstallDialogService : IModInstallDialogService
    {
        public string? PickModName(string suggestedName, ContentLayoutPreview? layoutPreview = null) => null;
        public ExistingModInstallMode? PickExistingModInstallMode(string modName) => null;
        public IReadOnlyList<string>? PickFomodSelections(FomodInstallerInfo installer) => null;
        public string? PickSeparatorName(string suggestedName) => null;
        public string? PickProjectName(string suggestedName) => null;
    }

    private sealed class NullExecutableManagerDialogService : IExecutableManagerDialogService
    {
        public IReadOnlyList<GameExecutableEntry>? EditExecutables(
            IReadOnlyList<GameExecutableEntry> executables,
            string gamePath,
            string projectDirectory) => null;
    }

    private sealed class NullBuildSettingsDialogService : IBuildSettingsDialogService
    {
        public BuildSettingsResult? EditBuildPaths(ModProject project) => null;
    }

    private sealed class NullBuildDeletionDialogService : IBuildDeletionDialogService
    {
        public bool Confirm(ConfirmDialogOptions options) => false;
    }
}
