using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Diagnostics;
using System.IO;
using System.Management;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Input;
using Fluxora.App.Models;
using Fluxora.App.Services;

namespace Fluxora.App.ViewModels;

public sealed class MainWindowViewModel : INotifyPropertyChanged
{
    private const int PluginsWorkspaceTabIndex = 0;
    private const int DataWorkspaceTabIndex = 1;
    private const int DownloadsWorkspaceTabIndex = 2;
    private const int BuildWorkspaceTabIndex = 3;
    private static readonly TimeSpan BuildDeletionSplashCompletionHold = TimeSpan.FromMilliseconds(620);
    private static readonly TimeSpan BuildCreationSplashCompletionHold = TimeSpan.FromMilliseconds(620);
    private static readonly TimeSpan FluxPackPackageSplashCompletionHold = TimeSpan.FromMilliseconds(620);
    private static readonly TimeSpan FluxPackInstallSplashCompletionHold = TimeSpan.FromMilliseconds(820);
    private static readonly TimeSpan ModOperationSplashCompletionHold = TimeSpan.FromMilliseconds(520);
    private static readonly TimeSpan BuildLoadingSplashPhraseInterval = TimeSpan.FromMilliseconds(4350);
    private static readonly TimeSpan BuildLoadingSplashMinimumDuration = TimeSpan.FromMilliseconds(650);
    private static readonly TimeSpan ExecutableLaunchSplashCompletionHold = TimeSpan.FromMilliseconds(650);
    private static readonly TimeSpan LaunchSessionStartTimeTolerance = TimeSpan.FromSeconds(2);
    private static readonly string[] BuildLoadingSplashPhrases =
    {
        "Загружаем сборку",
        "Проверяем конфиг",
        "Собираем моды",
        "Подключаем плагины",
        "Почти готово",
        "Ещё чуть-чуть"
    };

    private enum SelectionScope
    {
        Mods,
        Plugins,
        Downloads
    }

    private readonly ProjectCatalogService projectCatalogService;
    private readonly ProjectOpenService projectOpenService;
    private readonly ModCatalogService modCatalogService;
    private readonly PluginCatalogService pluginCatalogService;
    private readonly DownloadCatalogService downloadCatalogService;
    private readonly ProjectWorkspaceLoadService projectWorkspaceLoadService;
    private readonly NxmProtocolService nxmProtocolService;
    private readonly TemplateCatalogService templateCatalogService;
    private readonly CoreBridgeService coreBridgeService;
    private readonly SettingsService settingsService;
    private readonly LanguageCatalogService languageCatalogService;
    private readonly ApplicationLogService logService;
    private readonly IFolderPickerService folderPickerService;
    private readonly IExecutablePickerService executablePickerService;
    private readonly IBuildConfigPickerService buildConfigPickerService;
    private readonly IModArchivePickerService modArchivePickerService;
    private readonly IModInstallDialogService modInstallDialogService;
    private readonly IExecutableManagerDialogService executableManagerDialogService;
    private readonly IBuildSettingsDialogService buildSettingsDialogService;
    private readonly IBuildDeletionDialogService buildDeletionDialogService;
    private readonly IFluxPackPickerService fluxPackPickerService;
    private readonly IConfirmDialogService confirmDialogService;
    private readonly ExecutableLaunchSessionStore launchSessionStore;
    private readonly SeparatorCollapseService modCollapseService = new();
    private readonly SeparatorCollapseService pluginCollapseService = new();

    private string coreStatus = "C++ core: bridge pending";
    private string projectsDirectory = string.Empty;
    private ModProject? selectedProject;
    private ModEntry? selectedMod;
    private PluginEntry? selectedPlugin;
    private DownloadEntry? selectedDownload;
    private string? modSelectionAnchorId;
    private string? pluginSelectionAnchorId;
    private string? downloadSelectionAnchorId;
    private ObservableCollection<ModEntry> visibleMods = new();
    private string modSearchText = string.Empty;
    private ObservableCollection<GameTemplateOption> visibleTemplates = new();
    private string gameSearchText = string.Empty;
    private bool isProjectWorkspaceOpen;
    private bool isCreateProjectPanelOpen;
    private bool isCreatingProject;
    private bool isOpeningProject;
    private bool isProcessingDownload;
    private bool isProcessingPlugins;
    private bool isCheckingModUpdates;
    private bool isProcessingFluxPack;
    private bool isLoadingModFiles;
    private bool isBuildLoadingSplashVisible;
    private Process? launchedExecutableProcess;
    private ExecutableLaunchSession? activeLaunchSession;
    private ManagementEventWatcher? launchedChildProcessWatcher;
    private CancellationTokenSource? launchHandoffCancellation;
    private bool isLaunchHandoffPending;
    private readonly BuildDeletionProcessViewModel buildDeletionProcess = new();
    private readonly BuildCreationProcessViewModel buildCreationProcess = new();
    private readonly FluxPackPackageProcessViewModel fluxPackPackageProcess = new();
    private readonly FluxPackInstallProcessViewModel fluxPackInstallProcess = new();
    private readonly ModOperationProcessViewModel modOperationProcess = new();
    private readonly ExecutableLaunchProcessViewModel executableLaunchProcess = new();
    private SettingsWindowViewModel? transferViewModel;
    private ModProject? transferReturnProject;
    private bool shouldReopenWorkspaceAfterTransfer;
    private bool isTransferImportHandled;
    private int selectedWorkspaceTabIndex;
    private int createProjectStepIndex;
    private string projectName = string.Empty;
    private GameTemplateOption? selectedTemplate;
    private ResolvedTemplate? selectedResolvedTemplate;
    private string gamePath = string.Empty;
    private string installRootDirectory = string.Empty;
    private string selectedProfile = string.Empty;
    private ExecutableMenuItem? selectedExecutable;
    private string lastSelectedExecutableId = string.Empty;
    private string validationMessage = string.Empty;
    private string activityMessage = string.Empty;
    private string buildLoadingSplashPhrase = BuildLoadingSplashPhrases[0];
    private string buildLoadingSplashDetail = "Готовим страницу сборки";
    private int buildLoadingSplashPhraseIndex;
    private Stopwatch? buildLoadingSplashStopwatch;
    private CancellationTokenSource? buildLoadingSplashPhraseCancellation;
    private Task? buildLoadingSplashPhraseTask;
    private CancellationTokenSource? workspaceLoadCancellation;
    private CancellationTokenSource? createProjectCancellation;
    private Task? workspaceLoadTask;
    private long workspaceLoadVersion;
    private readonly Dictionary<string, string> projectSizeTextCache = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, CancellationTokenSource> projectSizeCalculations = new(StringComparer.OrdinalIgnoreCase);
    private string selectedProjectSizeCacheKey = string.Empty;
    private CancellationTokenSource? projectModCountCalculationCancellation;
    private string selectedProjectSizeText = "Считаем...";
    private int? selectedProjectActiveModCount;
    private int? selectedProjectDisabledModCount;
    private bool selectedProjectModCountUnavailable;

    public MainWindowViewModel(
        ProjectCatalogService projectCatalogService,
        ProjectOpenService projectOpenService,
        ModCatalogService modCatalogService,
        PluginCatalogService pluginCatalogService,
        DownloadCatalogService downloadCatalogService,
        ProjectWorkspaceLoadService projectWorkspaceLoadService,
        NxmProtocolService nxmProtocolService,
        TemplateCatalogService templateCatalogService,
        CoreBridgeService coreBridgeService,
        SettingsService settingsService,
        LanguageCatalogService languageCatalogService,
        ApplicationLogService logService,
        IFolderPickerService folderPickerService,
        IExecutablePickerService executablePickerService,
        IBuildConfigPickerService buildConfigPickerService,
        IModArchivePickerService modArchivePickerService,
        IModInstallDialogService modInstallDialogService,
        IExecutableManagerDialogService executableManagerDialogService,
        IBuildSettingsDialogService buildSettingsDialogService,
        IBuildDeletionDialogService buildDeletionDialogService,
        ExecutableLaunchSessionStore? launchSessionStore = null,
        IFluxPackPickerService? fluxPackPickerService = null,
        IConfirmDialogService? confirmDialogService = null)
    {
        this.projectCatalogService = projectCatalogService;
        this.projectOpenService = projectOpenService;
        this.modCatalogService = modCatalogService;
        this.pluginCatalogService = pluginCatalogService;
        this.downloadCatalogService = downloadCatalogService;
        this.projectWorkspaceLoadService = projectWorkspaceLoadService;
        this.nxmProtocolService = nxmProtocolService;
        this.templateCatalogService = templateCatalogService;
        this.coreBridgeService = coreBridgeService;
        this.settingsService = settingsService;
        this.languageCatalogService = languageCatalogService;
        this.logService = logService;
        this.folderPickerService = folderPickerService;
        this.executablePickerService = executablePickerService;
        this.buildConfigPickerService = buildConfigPickerService;
        this.modArchivePickerService = modArchivePickerService;
        this.modInstallDialogService = modInstallDialogService;
        this.executableManagerDialogService = executableManagerDialogService;
        this.buildSettingsDialogService = buildSettingsDialogService;
        this.buildDeletionDialogService = buildDeletionDialogService;
        this.fluxPackPickerService = fluxPackPickerService ?? new NullFluxPackPickerService();
        this.confirmDialogService = confirmDialogService ?? new NullConfirmDialogService();
        this.launchSessionStore = launchSessionStore ?? new ExecutableLaunchSessionStore(logService);

        OpenCreateProjectCommand = new RelayCommand(OpenCreateProject, () => !IsCreatingProject && !IsOpeningProject && !IsTransferPanelOpen && !IsWorkspaceOperationBlocked);
        InstallFluxPackCommand = new RelayCommand(InstallFluxPack, () => !IsCreatingProject && !IsOpeningProject && !IsTransferPanelOpen && !IsWorkspaceOperationBlocked);
        OpenProjectCommand = new RelayCommand(OpenProjectFromConfig, () => !IsCreatingProject && !IsOpeningProject && !IsTransferPanelOpen && !IsWorkspaceOperationBlocked);
        OpenProjectBuildCommand = new RelayCommand<ModProject>(
            OpenProjectBuild,
            project => project is not null && !IsCreatingProject && !IsOpeningProject && !IsTransferPanelOpen && !IsWorkspaceOperationBlocked);
        RenameProjectCommand = new RelayCommand<ModProject>(
            RenameProject,
            project => project is not null && !IsCreatingProject && !IsOpeningProject && !IsTransferPanelOpen && !IsWorkspaceOperationBlocked);
        DeleteProjectCommand = new RelayCommand<ModProject>(
            DeleteProject,
            project => project is not null && !IsCreatingProject && !IsOpeningProject && !IsTransferPanelOpen && !IsWorkspaceOperationBlocked);
        PackageProjectCommand = new RelayCommand<ModProject>(
            PackageProject,
            project => project is not null && !IsCreatingProject && !IsOpeningProject && !IsTransferPanelOpen && !IsWorkspaceOperationBlocked);
        BackToProjectsCommand = new RelayCommand(BackToProjects, () => IsProjectWorkspaceOpen && !IsOpeningProject && !IsWorkspaceOperationBlocked);
        RefreshWorkspaceCommand = new RelayCommand(RefreshProjectWorkspace, () => IsProjectWorkspaceOpen && HasSelectedProject && !IsWorkspaceOperationBlocked);
        OpenBuildSettingsCommand = new RelayCommand(OpenBuildSettings, () => IsProjectWorkspaceOpen && HasSelectedProject && !IsOpeningProject && !IsWorkspaceOperationBlocked);
        OpenProjectDirectoryCommand = new RelayCommand(OpenProjectDirectory, () => IsProjectWorkspaceOpen && HasSelectedProject && !IsWorkspaceOperationBlocked);
        OpenGameDirectoryCommand = new RelayCommand(OpenGameDirectory, () => IsProjectWorkspaceOpen && HasSelectedProject && !IsWorkspaceOperationBlocked);
        OpenHomeProjectDirectoryCommand = new RelayCommand(OpenProjectDirectory, () => HasSelectedProject && !IsWorkspaceOperationBlocked);
        OpenHomeGameDirectoryCommand = new RelayCommand(OpenGameDirectory, () => HasSelectedProject && !IsWorkspaceOperationBlocked);
        OpenModsDirectoryCommand = new RelayCommand(OpenModsDirectory, () => IsProjectWorkspaceOpen && HasSelectedProject && !IsWorkspaceOperationBlocked);
        OpenProfilesDirectoryCommand = new RelayCommand(OpenProfilesDirectory, () => IsProjectWorkspaceOpen && HasSelectedProject && !IsWorkspaceOperationBlocked);
        OpenDownloadsDirectoryCommand = new RelayCommand(OpenDownloadsDirectory, () => IsProjectWorkspaceOpen && HasSelectedProject && !IsWorkspaceOperationBlocked);
        AddDownloadFileCommand = new RelayCommand(AddDownloadFile, () => IsProjectWorkspaceOpen && HasSelectedProject && !IsProcessingDownload && !IsWorkspaceOperationBlocked);
        CheckModUpdatesCommand = new RelayCommand(CheckModUpdates, () => IsProjectWorkspaceOpen && HasSelectedProject && !IsCheckingModUpdates && !IsWorkspaceOperationBlocked);
        CreateModSeparatorCommand = new RelayCommand(CreateModSeparator, () => IsProjectWorkspaceOpen && HasSelectedProject && !IsProcessingDownload && !IsWorkspaceOperationBlocked);
        OpenModInExplorerCommand = new RelayCommand<ModEntry>(
            OpenModInExplorer,
            CanOpenSelectedModInExplorer);
        ToggleModSeparatorCommand = new RelayCommand<ModEntry>(
            ToggleModSeparator,
            mod => mod is { IsSeparator: true });
        MoveSelectedModUpCommand = new RelayCommand(
            MoveSelectedModUp,
            () => CanMoveSelectedMod(-1));
        MoveSelectedModDownCommand = new RelayCommand(
            MoveSelectedModDown,
            () => CanMoveSelectedMod(1));
        DeleteSelectedModCommand = new RelayCommand<ModEntry>(
            DeleteSelectedMod,
            CanDeleteSelectedMod);
        EnableSelectedModCommand = new RelayCommand<ModEntry>(
            mod => SetSelectedModEnabled(mod, true),
            mod => CanSetSelectedModEnabled(mod, true));
        EnableAllModsCommand = new RelayCommand(
            () => SetAllModsEnabled(true),
            () => CanSetAllModsEnabled(true));
        DisableSelectedModCommand = new RelayCommand<ModEntry>(
            mod => SetSelectedModEnabled(mod, false),
            mod => CanSetSelectedModEnabled(mod, false));
        DisableAllModsCommand = new RelayCommand(
            () => SetAllModsEnabled(false),
            () => CanSetAllModsEnabled(false));
        MoveSelectedPluginUpCommand = new RelayCommand(
            MoveSelectedPluginUp,
            () => CanMoveSelectedPlugin(-1));
        MoveSelectedPluginDownCommand = new RelayCommand(
            MoveSelectedPluginDown,
            () => CanMoveSelectedPlugin(1));
        CreatePluginSeparatorCommand = new RelayCommand(
            CreatePluginSeparator,
            () => CanShowLoadOrderPanel && IsProjectWorkspaceOpen && HasSelectedProject && !IsProcessingPlugins && !IsWorkspaceOperationBlocked);
        TogglePluginSeparatorCommand = new RelayCommand<PluginEntry>(
            TogglePluginSeparator,
            plugin => plugin is { IsSeparator: true });
        DeleteSelectedPluginCommand = new RelayCommand<PluginEntry>(
            DeleteSelectedPlugin,
            CanDeleteSelectedPlugin);
        EnableSelectedPluginCommand = new RelayCommand<PluginEntry>(
            plugin => SetSelectedPluginsEnabled(plugin, true),
            plugin => CanSetSelectedPluginsEnabled(plugin, true));
        DisableSelectedPluginCommand = new RelayCommand<PluginEntry>(
            plugin => SetSelectedPluginsEnabled(plugin, false),
            plugin => CanSetSelectedPluginsEnabled(plugin, false));
        TogglePluginEnabledCommand = new RelayCommand<PluginEntry>(
            TogglePluginEnabled,
            plugin => CanShowPluginsPanel && IsProjectWorkspaceOpen && HasSelectedProject && plugin is { CanToggle: true } && !IsProcessingPlugins && !IsWorkspaceOperationBlocked);
        InstallSelectedDownloadCommand = new RelayCommand<DownloadEntry>(
            InstallSelectedDownload,
            CanInstallSelectedDownload);
        DeleteSelectedDownloadCommand = new RelayCommand<DownloadEntry>(
            DeleteSelectedDownload,
            CanDeleteSelectedDownload);
        CancelDownloadCommand = new RelayCommand<DownloadEntry>(
            CancelDownload,
            download => IsProjectWorkspaceOpen && HasSelectedProject && (download ?? SelectedDownload)?.IsDownloading == true && !IsWorkspaceOperationBlocked);
        ResumeDownloadCommand = new RelayCommand<DownloadEntry>(
            ResumeDownload,
            download => IsProjectWorkspaceOpen && HasSelectedProject && (download ?? SelectedDownload)?.CanResume == true && !IsProcessingDownload && !IsWorkspaceOperationBlocked);
        OpenDownloadInExplorerCommand = new RelayCommand<DownloadEntry>(
            OpenDownloadInExplorer,
            CanOpenSelectedDownloadInExplorer);
        RegisterNxmProtocolCommand = new RelayCommand(
            RegisterNxmProtocol,
            () => !IsProcessingDownload && !IsWorkspaceOperationBlocked);
        LaunchSelectedExecutableCommand = new RelayCommand(
            LaunchSelectedExecutable,
            () => IsProjectWorkspaceOpen && HasSelectedProject && SelectedExecutable?.Executable is not null && !IsWorkspaceOperationBlocked);
        BrowseGamePathCommand = new RelayCommand(BrowseGamePath, () => !IsCreatingProject && !IsWorkspaceOperationBlocked);
        BrowseInstallRootCommand = new RelayCommand(BrowseInstallRoot, () => !IsCreatingProject && !IsWorkspaceOperationBlocked);
        CancelCreateProjectCommand = new RelayCommand(CancelCreateProject, () => !IsCreatingProject && !IsWorkspaceOperationBlocked);
        PreviousCreateStepCommand = new RelayCommand(MoveToPreviousStep, () => IsCreateProjectPanelOpen && !IsCreatingProject && CreateProjectStepIndex > 0 && !IsWorkspaceOperationBlocked);
        NextCreateStepCommand = new RelayCommand(MoveToNextStep, () => IsCreateProjectPanelOpen && !IsCreatingProject && CreateProjectStepIndex < 3 && !IsWorkspaceOperationBlocked);
        CreateProjectCommand = new RelayCommand(CreateProject, () => IsCreateProjectPanelOpen && !IsCreatingProject && CreateProjectStepIndex == 3 && !IsWorkspaceOperationBlocked);
        CancelBuildCreationCommand = new RelayCommand(RequestCancelBuildCreation, () => BuildCreationProcess.CanCancel);
        CloseBuildCreationProcessCommand = new RelayCommand(CloseBuildCreationProcess, () => BuildCreationProcess.CanClose);
        CloseTransferPanelCommand = new RelayCommand(CloseTransferPanel, () => IsTransferPanelClosable);
        CloseBuildDeletionProcessCommand = new RelayCommand(CloseBuildDeletionProcess, () => BuildDeletionProcess.CanClose);
        CloseFluxPackPackageProcessCommand = new RelayCommand(CloseFluxPackPackageProcess, () => FluxPackPackageProcess.CanClose);
        CloseFluxPackInstallProcessCommand = new RelayCommand(CloseFluxPackInstallProcess, () => FluxPackInstallProcess.CanClose);
        CloseExecutableLaunchProcessCommand = new RelayCommand(CloseExecutableLaunchProcess, () => ExecutableLaunchProcess.CanClose);
        BuildCreationProcess.PropertyChanged += OnBuildCreationProcessPropertyChanged;
        BuildDeletionProcess.PropertyChanged += OnBuildDeletionProcessPropertyChanged;
        FluxPackPackageProcess.PropertyChanged += OnFluxPackPackageProcessPropertyChanged;
        FluxPackInstallProcess.PropertyChanged += OnFluxPackInstallProcessPropertyChanged;
        ModOperationProcess.PropertyChanged += OnModOperationProcessPropertyChanged;
        ExecutableLaunchProcess.PropertyChanged += OnExecutableLaunchProcessPropertyChanged;
        LocalizationManager.Current.LanguageChanged += OnLocalizationChanged;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public ObservableCollection<ModProject> Projects { get; } = new();

    public ObservableCollection<ModEntry> Mods { get; } = new();

    public ObservableCollection<ModEntry> VisibleMods
    {
        get => visibleMods;
        private set
        {
            if (SetField(ref visibleMods, value))
            {
                NotifyVisibleModsChanged();
            }
        }
    }

    public string ModSearchText
    {
        get => modSearchText;
        set
        {
            string nextValue = value ?? string.Empty;
            if (!SetField(ref modSearchText, nextValue))
            {
                return;
            }

            HashSet<string> selectedModIds = SelectedVisibleMods()
                .Select(ModListItemKey)
                .ToHashSet(StringComparer.OrdinalIgnoreCase);
            string? selectedModId = SelectedMod is null ? null : ModListItemKey(SelectedMod);

            OnPropertyChanged(nameof(IsModSearchActive));
            RebuildVisibleMods();
            RestoreModSelection(selectedModIds, selectedModId);
            RaiseModCommandStateChanged();
        }
    }

    public ObservableCollection<PluginEntry> Plugins { get; } = new();

    public ObservableCollection<DownloadEntry> Downloads { get; } = new();

    public ObservableCollection<ModFileTreeNode> SelectedModFileTree { get; } = new();

    public ObservableCollection<string> AvailableProfiles { get; } = new();

    public ObservableCollection<ExecutableMenuItem> AvailableExecutables { get; } = new();

    /// <summary>Game templates offered by the C++ core (base template excluded).</summary>
    public ObservableCollection<GameTemplateOption> AvailableTemplates { get; } = new();

    public ObservableCollection<GameTemplateOption> VisibleTemplates
    {
        get => visibleTemplates;
        private set
        {
            if (SetField(ref visibleTemplates, value))
            {
                OnPropertyChanged(nameof(HasVisibleTemplates));
                OnPropertyChanged(nameof(GameSearchResultText));
            }
        }
    }

    public string GameSearchText
    {
        get => gameSearchText;
        set
        {
            string nextValue = value ?? string.Empty;
            if (!SetField(ref gameSearchText, nextValue))
            {
                return;
            }

            ValidationMessage = string.Empty;
            OnPropertyChanged(nameof(IsGameSearchActive));
            RebuildVisibleTemplates();
        }
    }

    public ICommand OpenCreateProjectCommand { get; }
    public ICommand InstallFluxPackCommand { get; }
    public ICommand OpenProjectCommand { get; }
    public ICommand OpenProjectBuildCommand { get; }
    public ICommand RenameProjectCommand { get; }
    public ICommand DeleteProjectCommand { get; }
    public ICommand PackageProjectCommand { get; }
    public ICommand BackToProjectsCommand { get; }
    public ICommand RefreshWorkspaceCommand { get; }
    public ICommand OpenBuildSettingsCommand { get; }
    public ICommand OpenProjectDirectoryCommand { get; }
    public ICommand OpenGameDirectoryCommand { get; }
    public ICommand OpenHomeProjectDirectoryCommand { get; }
    public ICommand OpenHomeGameDirectoryCommand { get; }
    public ICommand OpenModsDirectoryCommand { get; }
    public ICommand OpenProfilesDirectoryCommand { get; }
    public ICommand OpenDownloadsDirectoryCommand { get; }
    public ICommand AddDownloadFileCommand { get; }
    public ICommand CheckModUpdatesCommand { get; }
    public ICommand CreateModSeparatorCommand { get; }
    public ICommand OpenModInExplorerCommand { get; }
    public ICommand ToggleModSeparatorCommand { get; }
    public ICommand MoveSelectedModUpCommand { get; }
    public ICommand MoveSelectedModDownCommand { get; }
    public ICommand DeleteSelectedModCommand { get; }
    public ICommand EnableSelectedModCommand { get; }
    public ICommand EnableAllModsCommand { get; }
    public ICommand DisableSelectedModCommand { get; }
    public ICommand DisableAllModsCommand { get; }
    public ICommand MoveSelectedPluginUpCommand { get; }
    public ICommand MoveSelectedPluginDownCommand { get; }
    public ICommand CreatePluginSeparatorCommand { get; }
    public ICommand TogglePluginSeparatorCommand { get; }
    public ICommand DeleteSelectedPluginCommand { get; }
    public ICommand EnableSelectedPluginCommand { get; }
    public ICommand DisableSelectedPluginCommand { get; }
    public ICommand TogglePluginEnabledCommand { get; }
    public ICommand InstallSelectedDownloadCommand { get; }
    public ICommand DeleteSelectedDownloadCommand { get; }
    public ICommand CancelDownloadCommand { get; }
    public ICommand ResumeDownloadCommand { get; }
    public ICommand OpenDownloadInExplorerCommand { get; }
    public ICommand RegisterNxmProtocolCommand { get; }
    public ICommand LaunchSelectedExecutableCommand { get; }
    public ICommand BrowseGamePathCommand { get; }
    public ICommand BrowseInstallRootCommand { get; }
    public ICommand CancelCreateProjectCommand { get; }
    public ICommand PreviousCreateStepCommand { get; }
    public ICommand NextCreateStepCommand { get; }
    public ICommand CreateProjectCommand { get; }
    public ICommand CancelBuildCreationCommand { get; }
    public ICommand CloseBuildCreationProcessCommand { get; }
    public ICommand CloseTransferPanelCommand { get; }
    public ICommand CloseBuildDeletionProcessCommand { get; }
    public ICommand CloseFluxPackPackageProcessCommand { get; }
    public ICommand CloseFluxPackInstallProcessCommand { get; }
    public ICommand CloseExecutableLaunchProcessCommand { get; }

    public string CoreStatus
    {
        get => coreStatus;
        private set => SetField(ref coreStatus, value);
    }

    public string ProjectsDirectory
    {
        get => projectsDirectory;
        private set => SetField(ref projectsDirectory, value);
    }

    public ModProject? SelectedProject
    {
        get => selectedProject;
        set
        {
            if (SetField(ref selectedProject, value))
            {
                OnPropertyChanged(nameof(HasSelectedProject));
                OnPropertyChanged(nameof(SelectedProjectCreatedText));
                OnPropertyChanged(nameof(SelectedProjectLastLaunchText));
                OnPropertyChanged(nameof(SelectedProjectConfigText));
                OnPropertyChanged(nameof(SelectedProjectExecutablesText));
                OnPropertyChanged(nameof(SelectedProjectPluginExtensionsText));
                OnPropertyChanged(nameof(WorkspaceTitle));
                OnPropertyChanged(nameof(WorkspaceSubtitle));
                OnPropertyChanged(nameof(SelectedProjectProfileFilesText));
                NotifyCapabilityPropertiesChanged();
                OnPropertyChanged(nameof(ActiveModCountText));
                OnPropertyChanged(nameof(DisabledModCountText));
                OnPropertyChanged(nameof(ProjectModBreakdownText));
                RefreshSelectedProjectSize();
                RefreshSelectedProjectModCounts();
                CoerceSelectedWorkspaceTabForCapabilities();
                RaiseWorkspaceCommandStateChanged();
            }
        }
    }

    private UiCapabilityState SelectedProjectCapabilities => GameCapabilityResolver.ForProject(SelectedProject);

    public ModEntry? SelectedMod
    {
        get => selectedMod;
        set
        {
            if (value is not null && !value.IsSelected)
            {
                RangeSelectionService.Clear(VisibleMods, static (item, selected) => item.IsSelected = selected);
                value.IsSelected = true;
                modSelectionAnchorId = ModListItemKey(value);
            }

            if (SetField(ref selectedMod, value))
            {
                OnPropertyChanged(nameof(SelectedModTitle));
                OnPropertyChanged(nameof(SelectedModVersionText));
                OnPropertyChanged(nameof(SelectedModUpdateText));
                OnPropertyChanged(nameof(SelectedModConflictText));
                OnPropertyChanged(nameof(SelectedModFileCountText));
                OnPropertyChanged(nameof(HasSelectedModFileTree));
                RaiseModCommandStateChanged();
                if (IsProcessingDownload || IsWorkspaceOperationBlocked)
                {
                    SelectedModFileTree.Clear();
                    OnPropertyChanged(nameof(HasSelectedModFileTree));
                }
                else
                {
                    _ = LoadSelectedModRootFileTreeAsync();
                }
            }
        }
    }

    public PluginEntry? SelectedPlugin
    {
        get => selectedPlugin;
        set
        {
            if (value is not null && !value.IsSelected)
            {
                RangeSelectionService.Clear(VisiblePlugins(), static (item, selected) => item.IsSelected = selected);
                value.IsSelected = true;
                pluginSelectionAnchorId = PluginListItemKey(value);
            }

            if (SetField(ref selectedPlugin, value))
            {
                OnPropertyChanged(nameof(SelectedPluginTitle));
                OnPropertyChanged(nameof(SelectedPluginStatusText));
                RaisePluginCommandStateChanged();
            }
        }
    }

    public DownloadEntry? SelectedDownload
    {
        get => selectedDownload;
        set
        {
            if (value is not null && !value.IsSelected)
            {
                RangeSelectionService.Clear(Downloads, static (item, selected) => item.IsSelected = selected);
                value.IsSelected = true;
                downloadSelectionAnchorId = value.Id;
            }

            if (SetField(ref selectedDownload, value))
            {
                OnPropertyChanged(nameof(SelectedDownloadTitle));
                OnPropertyChanged(nameof(SelectedDownloadStatusText));
                OnPropertyChanged(nameof(SelectedDownloadSourceText));
                RaiseDownloadCommandStateChanged();
            }
        }
    }

    public bool IsProjectWorkspaceOpen
    {
        get => isProjectWorkspaceOpen;
        private set
        {
            if (SetField(ref isProjectWorkspaceOpen, value))
            {
                RaiseWorkspaceStateChanged();
                RaiseBusyCommandStateChanged();
            }
        }
    }

    public bool IsHomeViewOpen => !IsProjectWorkspaceOpen && !IsTransferPanelOpen;

    public bool IsModlistViewOpen => IsProjectWorkspaceOpen && !IsTransferPanelOpen;

    public int SelectedWorkspaceTabIndex
    {
        get => selectedWorkspaceTabIndex;
        set
        {
            if (SetField(ref selectedWorkspaceTabIndex, value))
            {
                CoerceSelectedWorkspaceTabForCapabilities();
            }
        }
    }

    public bool IsCreateProjectPanelOpen
    {
        get => isCreateProjectPanelOpen;
        private set
        {
            if (SetField(ref isCreateProjectPanelOpen, value))
            {
                RaiseWizardStateChanged();
            }
        }
    }

    public SettingsWindowViewModel? TransferViewModel
    {
        get => transferViewModel;
        private set
        {
            if (ReferenceEquals(transferViewModel, value))
            {
                return;
            }

            if (transferViewModel is not null)
            {
                transferViewModel.PropertyChanged -= OnTransferViewModelPropertyChanged;
            }

            transferViewModel = value;
            if (transferViewModel is not null)
            {
                transferViewModel.PropertyChanged += OnTransferViewModelPropertyChanged;
            }

            OnPropertyChanged();
            OnPropertyChanged(nameof(IsTransferPanelOpen));
            OnPropertyChanged(nameof(IsMainShellVisible));
            OnPropertyChanged(nameof(IsHomeViewOpen));
            OnPropertyChanged(nameof(IsModlistViewOpen));
            OnPropertyChanged(nameof(IsTransferPanelClosable));
            (CloseTransferPanelCommand as RelayCommand)?.RaiseCanExecuteChanged();
            RaiseBusyCommandStateChanged();
        }
    }

    public bool IsTransferPanelOpen => TransferViewModel is not null;

    public bool IsMainShellVisible => !IsTransferPanelOpen;

    public bool IsTransferPanelClosable => TransferViewModel?.IsTransferRunning != true;

    public bool IsCreatingProject
    {
        get => isCreatingProject;
        private set
        {
            if (SetField(ref isCreatingProject, value))
            {
                RaiseWizardStateChanged();
                RaiseBusyCommandStateChanged();
            }
        }
    }

    public bool IsOpeningProject
    {
        get => isOpeningProject;
        private set
        {
            if (SetField(ref isOpeningProject, value))
            {
                RaiseBusyCommandStateChanged();
            }
        }
    }

    public bool IsProcessingDownload
    {
        get => isProcessingDownload;
        private set
        {
            if (SetField(ref isProcessingDownload, value))
            {
                OnPropertyChanged(nameof(CanImportDownloadFiles));
                (AddDownloadFileCommand as RelayCommand)?.RaiseCanExecuteChanged();
                (CreateModSeparatorCommand as RelayCommand)?.RaiseCanExecuteChanged();
                RaiseModCommandStateChanged();
                (InstallSelectedDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
                (DeleteSelectedDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
                (CancelDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
                (ResumeDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
                (RegisterNxmProtocolCommand as RelayCommand)?.RaiseCanExecuteChanged();
            }
        }
    }

    public bool IsProcessingPlugins
    {
        get => isProcessingPlugins;
        private set
        {
            if (SetField(ref isProcessingPlugins, value))
            {
                RaisePluginCommandStateChanged();
            }
        }
    }

    public bool IsCheckingModUpdates
    {
        get => isCheckingModUpdates;
        private set
        {
            if (SetField(ref isCheckingModUpdates, value))
            {
                (CheckModUpdatesCommand as RelayCommand)?.RaiseCanExecuteChanged();
            }
        }
    }

    public bool IsProcessingFluxPack
    {
        get => isProcessingFluxPack;
        private set
        {
            if (SetField(ref isProcessingFluxPack, value))
            {
                RaiseBusyCommandStateChanged();
            }
        }
    }

    public bool IsLoadingModFiles
    {
        get => isLoadingModFiles;
        private set => SetField(ref isLoadingModFiles, value);
    }

    public bool IsBuildLoadingSplashVisible
    {
        get => isBuildLoadingSplashVisible;
        private set
        {
            if (SetField(ref isBuildLoadingSplashVisible, value))
            {
                RaiseBusyCommandStateChanged();
            }
        }
    }

    public string BuildLoadingSplashPhrase
    {
        get => T(buildLoadingSplashPhrase);
        private set => SetField(ref buildLoadingSplashPhrase, value);
    }

    public string BuildLoadingSplashDetail
    {
        get => T(buildLoadingSplashDetail);
        private set => SetField(ref buildLoadingSplashDetail, value);
    }

    public BuildCreationProcessViewModel BuildCreationProcess => buildCreationProcess;

    public BuildDeletionProcessViewModel BuildDeletionProcess => buildDeletionProcess;

    public FluxPackPackageProcessViewModel FluxPackPackageProcess => fluxPackPackageProcess;

    public FluxPackInstallProcessViewModel FluxPackInstallProcess => fluxPackInstallProcess;

    public ModOperationProcessViewModel ModOperationProcess => modOperationProcess;

    public ExecutableLaunchProcessViewModel ExecutableLaunchProcess => executableLaunchProcess;

    private bool IsWorkspaceOperationBlocked =>
        BuildCreationProcess.IsVisible ||
        ModOperationProcess.IsVisible ||
        ExecutableLaunchProcess.IsVisible ||
        BuildDeletionProcess.IsVisible ||
        FluxPackPackageProcess.IsVisible ||
        FluxPackInstallProcess.IsVisible ||
        IsProcessingFluxPack ||
        IsBuildLoadingSplashVisible;

    public int CreateProjectStepIndex
    {
        get => createProjectStepIndex;
        private set
        {
            if (SetField(ref createProjectStepIndex, value))
            {
                RaiseWizardStateChanged();
            }
        }
    }

    public string ProjectName
    {
        get => projectName;
        set
        {
            if (SetField(ref projectName, value))
            {
                ValidationMessage = string.Empty;
                OnPropertyChanged(nameof(TargetProjectDirectory));
            }
        }
    }

    public GameTemplateOption? SelectedTemplate
    {
        get => selectedTemplate;
        set
        {
            if (SetField(ref selectedTemplate, value))
            {
                ValidationMessage = string.Empty;
                SelectedResolvedTemplate = templateCatalogService.Resolve(value?.Id);
            }
        }
    }

    /// <summary>
    /// The resolved (base + game) template for the selected game. Drives the
    /// template preview in the wizard so the user sees exactly what the build
    /// will expose before creating it.
    /// </summary>
    public ResolvedTemplate? SelectedResolvedTemplate
    {
        get => selectedResolvedTemplate;
        private set
        {
            if (SetField(ref selectedResolvedTemplate, value))
            {
                OnPropertyChanged(nameof(HasResolvedTemplate));
            }
        }
    }

    public string GamePath
    {
        get => gamePath;
        set
        {
            if (SetField(ref gamePath, value))
            {
                ValidationMessage = string.Empty;
            }
        }
    }

    public string InstallRootDirectory
    {
        get => installRootDirectory;
        set
        {
            if (SetField(ref installRootDirectory, value))
            {
                ValidationMessage = string.Empty;
                OnPropertyChanged(nameof(TargetProjectDirectory));
            }
        }
    }

    public string SelectedProfile
    {
        get => selectedProfile;
        set
        {
            if (SetField(ref selectedProfile, value))
            {
                OnPropertyChanged(nameof(WorkspaceSubtitle));
                RefreshProfileScopedLists();
            }
        }
    }

    public ExecutableMenuItem? SelectedExecutable
    {
        get => selectedExecutable;
        set
        {
            if (value?.OpensManager == true)
            {
                RestorePreviousExecutableSelection();
                OpenExecutableManager();
                return;
            }

            if (SetField(ref selectedExecutable, value))
            {
                lastSelectedExecutableId = value?.Executable?.Id ?? string.Empty;
                OnPropertyChanged(nameof(SelectedExecutableToolTip));
                (LaunchSelectedExecutableCommand as RelayCommand)?.RaiseCanExecuteChanged();
            }
        }
    }

    public string SelectedExecutableToolTip => SelectedExecutable?.ToolTip ?? T("Исполняемые файлы");

    public string ValidationMessage
    {
        get => T(validationMessage);
        private set => SetField(ref validationMessage, value);
    }

    public string ActivityMessage
    {
        get => T(activityMessage);
        private set => SetField(ref activityMessage, value);
    }

    private int InstalledModCount => Mods.Count(mod => mod.IsMod);
    private int InstalledPluginCount => Plugins.Count(plugin => plugin.IsPlugin);

    public bool HasProjects => Projects.Count > 0;
    public bool HasSelectedProject => SelectedProject is not null;
    public bool HasMods => InstalledModCount > 0;
    public bool HasModOrderItems => Mods.Count > 0;
    public bool HasVisibleModOrderItems => VisibleMods.Count > 0;
    public bool IsModSearchActive => !string.IsNullOrWhiteSpace(ModSearchText);
    public bool HasPlugins => Plugins.Count > 0;
    public bool HasDownloads => Downloads.Count > 0;
    public bool HasSelectedModFileTree => SelectedModFileTree.Count > 0;
    public bool HasTemplates => AvailableTemplates.Count > 0;
    public bool HasVisibleTemplates => VisibleTemplates.Count > 0;
    public bool IsGameSearchActive => !string.IsNullOrWhiteSpace(GameSearchText);
    public bool HasResolvedTemplate => SelectedResolvedTemplate is not null;
    public bool CanShowPluginsPanel => SelectedProjectCapabilities.SupportsPluginSection;
    public bool CanShowLoadOrderPanel => SelectedProjectCapabilities.SupportsLoadOrder;
    public bool CanShowIniPanel => SelectedProjectCapabilities.SupportsIniProfiles;
    public bool CanShowSavePanel => SelectedProjectCapabilities.SupportsSaveProfiles;
    public bool CanShowScriptExtenderPanel => SelectedProjectCapabilities.SupportsScriptExtender;
    public bool CanShowRootFilesPanel => SelectedProjectCapabilities.SupportsRootFiles;
    public bool CanShowExecutablePanel => SelectedProjectCapabilities.SupportsExecutablePanel;
    public bool CanShowContentLayoutReviewPanel => SelectedProjectCapabilities.SupportsContentLayoutReview;
    public bool CanShowHealthDiagnosticsPanel => SelectedProjectCapabilities.SupportsHealthDiagnostics;
    public bool CanShowBuildOverviewPanel =>
        CanShowExecutablePanel ||
        CanShowIniPanel ||
        CanShowSavePanel ||
        CanShowScriptExtenderPanel ||
        CanShowRootFilesPanel ||
        CanShowContentLayoutReviewPanel ||
        CanShowHealthDiagnosticsPanel;
    public bool IsNameStep => CreateProjectStepIndex == 0;
    public bool IsGameStep => CreateProjectStepIndex == 1;
    public bool IsGameExeStep => CreateProjectStepIndex == 2;
    public bool IsInstallStep => CreateProjectStepIndex == 3;

    public string ProjectCountText => FormatBuildCount(Projects.Count);

    public string ModCountText => InstalledModCount == 0
        ? T("Моды не установлены")
        : F("{0} {1}", InstalledModCount, FormatModWord(InstalledModCount));

    public string ModSearchResultText => IsModSearchActive
        ? VisibleMods.Count == 0
            ? T("Ничего не найдено")
            : F("Показано: {0}", VisibleMods.Count)
        : T("Поиск по модам и разделителям");

    public string GameSearchResultText => IsGameSearchActive
        ? VisibleTemplates.Count == 0
            ? T("Ничего не найдено")
            : F("Показано: {0}", VisibleTemplates.Count)
        : T("Поиск по играм");

    public string ModListEmptyTitle => IsModSearchActive && HasModOrderItems
        ? T("Ничего не найдено")
        : T("Папка mods пуста");

    public string ModListEmptySubtitle => IsModSearchActive && HasModOrderItems
        ? F("По запросу «{0}» нет модов или разделителей.", ModSearchText.Trim())
        : T("Добавьте моды через NXM или вручную, затем обновите список.");

    public string ActiveModCountText => selectedProjectModCountUnavailable
        ? "-"
        : selectedProjectActiveModCount?.ToString() ?? "...";

    public string DisabledModCountText => selectedProjectModCountUnavailable
        ? "-"
        : selectedProjectDisabledModCount?.ToString() ?? "...";

    public string ProjectModBreakdownText => selectedProjectModCountUnavailable
        ? T("Недоступно")
        : selectedProjectActiveModCount is null || selectedProjectDisabledModCount is null
        ? T("Считаем моды...")
        : selectedProjectActiveModCount + selectedProjectDisabledModCount == 0
        ? T("Моды не установлены")
        : F("{0} активных · {1} выключенных", selectedProjectActiveModCount, selectedProjectDisabledModCount);

    public string PluginCountText => InstalledPluginCount == 0
        ? T("Плагины не найдены")
        : F("{0} {1}", InstalledPluginCount, FormatPluginWord(InstalledPluginCount));

    public string DownloadCountText => Downloads.Count == 0
        ? T("Загрузок пока нет")
        : F("{0} {1}", Downloads.Count, FormatDownloadWord(Downloads.Count));

    public bool CanImportDownloadFiles =>
        IsProjectWorkspaceOpen &&
        HasSelectedProject &&
        !IsProcessingDownload &&
        !IsWorkspaceOperationBlocked;

    public bool CanInstallDownloadAtModInsertionIndex(DownloadEntry? download)
    {
        return IsProjectWorkspaceOpen &&
            HasSelectedProject &&
            download?.CanInstall == true &&
            !IsModSearchActive &&
            !IsProcessingDownload &&
            !IsWorkspaceOperationBlocked;
    }

    public void SelectModWithGesture(ModEntry? mod, RangeSelectionGesture gesture)
    {
        ClearSelectionsExcept(SelectionScope.Mods);
        modSelectionAnchorId = RangeSelectionService.Apply(
            VisibleMods,
            mod,
            ModListItemKey,
            item => item.IsSelected,
            static (item, value) => item.IsSelected = value,
            modSelectionAnchorId,
            gesture);
        FocusModSelection(ResolveFocusedModAfterSelection(mod));
    }

    public void SelectAllMods()
    {
        ClearSelectionsExcept(SelectionScope.Mods);
        modSelectionAnchorId = RangeSelectionService.SelectAll(
            VisibleMods,
            ModListItemKey,
            static (item, value) => item.IsSelected = value,
            SelectedMod is null ? modSelectionAnchorId : ModListItemKey(SelectedMod));
        FocusModSelection(SelectedMod is { IsSelected: true } ? SelectedMod : VisibleMods.FirstOrDefault());
    }

    public void FocusModSelection(ModEntry? mod)
    {
        ClearSelectionsExcept(SelectionScope.Mods);
        if (mod is not null)
        {
            modSelectionAnchorId = ModListItemKey(mod);
        }

        SelectedMod = mod ?? SelectedVisibleMods().FirstOrDefault();
        RaiseModCommandStateChanged();
    }

    public void SelectPluginWithGesture(PluginEntry? plugin, RangeSelectionGesture gesture)
    {
        ClearSelectionsExcept(SelectionScope.Plugins);
        IReadOnlyList<PluginEntry> visiblePlugins = VisiblePlugins();
        pluginSelectionAnchorId = RangeSelectionService.Apply(
            visiblePlugins,
            plugin,
            PluginListItemKey,
            item => item.IsSelected,
            static (item, value) => item.IsSelected = value,
            pluginSelectionAnchorId,
            gesture);
        FocusPluginSelection(ResolveFocusedPluginAfterSelection(plugin));
    }

    public void SelectAllPlugins()
    {
        ClearSelectionsExcept(SelectionScope.Plugins);
        IReadOnlyList<PluginEntry> visiblePlugins = VisiblePlugins();
        pluginSelectionAnchorId = RangeSelectionService.SelectAll(
            visiblePlugins,
            PluginListItemKey,
            static (item, value) => item.IsSelected = value,
            SelectedPlugin is null ? pluginSelectionAnchorId : PluginListItemKey(SelectedPlugin));
        FocusPluginSelection(SelectedPlugin is { IsSelected: true } ? SelectedPlugin : visiblePlugins.FirstOrDefault());
    }

    public void FocusPluginSelection(PluginEntry? plugin)
    {
        ClearSelectionsExcept(SelectionScope.Plugins);
        if (plugin is not null)
        {
            pluginSelectionAnchorId = PluginListItemKey(plugin);
        }

        SelectedPlugin = plugin ?? SelectedVisiblePlugins().FirstOrDefault();
        RaisePluginCommandStateChanged();
    }

    public void SelectDownloadWithGesture(DownloadEntry? download, RangeSelectionGesture gesture)
    {
        ClearSelectionsExcept(SelectionScope.Downloads);
        downloadSelectionAnchorId = RangeSelectionService.Apply(
            Downloads,
            download,
            download => download.Id,
            download => download.IsSelected,
            static (download, value) => download.IsSelected = value,
            downloadSelectionAnchorId,
            gesture);
        FocusDownloadSelection(ResolveFocusedDownloadAfterSelection(download));
    }

    public void SelectAllDownloads()
    {
        ClearSelectionsExcept(SelectionScope.Downloads);
        downloadSelectionAnchorId = RangeSelectionService.SelectAll(
            Downloads,
            download => download.Id,
            static (download, value) => download.IsSelected = value,
            SelectedDownload?.Id ?? downloadSelectionAnchorId);
        FocusDownloadSelection(SelectedDownload is { IsSelected: true } ? SelectedDownload : Downloads.FirstOrDefault());
    }

    public void FocusDownloadSelection(DownloadEntry? download)
    {
        ClearSelectionsExcept(SelectionScope.Downloads);
        if (download is not null)
        {
            downloadSelectionAnchorId = download.Id;
        }

        SelectedDownload = download ?? SelectedDownloads().FirstOrDefault();
        RaiseDownloadCommandStateChanged();
    }

    public bool CanMoveModOrderItem(ModEntry? mod)
    {
        return IsProjectWorkspaceOpen &&
            HasSelectedProject &&
            mod is not null &&
            Mods.Count > 1 &&
            !IsModSearchActive &&
            !IsProcessingDownload &&
            !IsCheckingModUpdates &&
            !IsWorkspaceOperationBlocked;
    }

    public bool CanMovePluginOrderItem(PluginEntry? plugin)
    {
        if (!CanShowLoadOrderPanel ||
            !IsProjectWorkspaceOpen ||
            !HasSelectedProject ||
            plugin is not { CanMove: true } ||
            Plugins.Count <= 1 ||
            IsProcessingPlugins ||
            IsWorkspaceOperationBlocked)
        {
            return false;
        }

        if (plugin.IsSeparator)
        {
            int sourceIndex = IndexOfPlugin(plugin.Id);
            int spanLength = PluginOrderMoveSpanLength(sourceIndex);
            return !PluginMoveSpanContainsLockedPlugin(sourceIndex, spanLength);
        }

        return !plugin.IsLocked;
    }

    private bool CanMoveSelectedMod(int direction)
    {
        if (SelectedMod is null || !CanMoveModOrderItem(SelectedMod))
        {
            return false;
        }

        if (SelectedVisibleMods().Count > 1)
        {
            return false;
        }

        int sourceIndex = IndexOfMod(ModListItemKey(SelectedMod));
        if (sourceIndex < 0)
        {
            return false;
        }

        if (SelectedMod.IsSeparator)
        {
            int spanLength = ModOrderMoveSpanLength(sourceIndex);
            return direction < 0
                ? sourceIndex > 0
                : sourceIndex + spanLength < Mods.Count;
        }

        return direction < 0
            ? sourceIndex > 0
            : sourceIndex < Mods.Count - 1;
    }

    public string WorkspaceTitle => SelectedProject is null
        ? "Mod Manager"
        : SelectedProject.Name;

    public string WorkspaceSubtitle
    {
        get
        {
            return SelectedProject?.GameName ?? string.Empty;
        }
    }

    public string SelectedProjectProfileFilesText => SelectedProject?.Template?.ProfileFiles is { Count: > 0 } profileFiles
        ? string.Join("  ·  ", profileFiles)
        : T("Профиль пока пуст");

    public string SelectedModTitle => SelectedMod?.DisplayName ?? T("Мод не выбран");

    public string SelectedModVersionText => SelectedMod is null
        ? T("Выберите мод в списке слева.")
        : SelectedMod.IsSeparator
            ? T("Разделитель визуального порядка.")
            : F("Версия: {0}", SelectedMod.VersionText);

    public string SelectedModUpdateText => SelectedMod is null
        ? string.Empty
        : SelectedMod.IsSeparator
            ? string.Empty
            : string.IsNullOrWhiteSpace(SelectedMod.UpdateStatus)
                ? T("Обновления не проверялись")
                : SelectedMod.UpdateStatus;

    public string SelectedModConflictText => SelectedMod is null
        ? string.Empty
        : SelectedMod.IsSeparator
            ? string.Empty
            : string.IsNullOrWhiteSpace(SelectedMod.ConflictStatus)
                ? T("Конфликты не индексировались")
                : SelectedMod.ConflictStatus;

    public string SelectedModFileCountText => SelectedMod?.FileCountText ?? string.Empty;

    public string SelectedPluginTitle => SelectedPlugin?.DisplayName ?? T("Плагин не выбран");

    public string SelectedPluginStatusText
    {
        get
        {
            if (SelectedPlugin is null)
            {
                return T("Выберите плагин в списке загрузки.");
            }

            if (SelectedPlugin.IsSeparator)
            {
                return T("Разделитель визуального порядка.");
            }

            string state = SelectedPlugin.IsEnabled ? T("Включён") : T("Отключён");
            return F("{0}. {1}", state, SelectedPlugin.SourceText);
        }
    }

    public string SelectedDownloadTitle => SelectedDownload?.Name ?? T("Загрузка не выбрана");

    public string SelectedDownloadStatusText => SelectedDownload is null
        ? T("Выберите файл или NXM-загрузку.")
        : FormatSelectedDownloadStatus(SelectedDownload);

    public string SelectedDownloadSourceText => SelectedDownload?.Source ?? string.Empty;

    public string CreateProjectStepTitle => CreateProjectStepIndex switch
    {
        0 => T("Как назовём вашу сборку?"),
        1 => T("Выберите игру"),
        2 => T("EXE игры"),
        _ => T("Папка установки")
    };

    public string CreateProjectStepSubtitle => CreateProjectStepIndex switch
    {
        0 => T("Это имя станет названием папки и будет видно в библиотеке."),
        1 => T("Найдите игру и выберите шаблон, который станет основой сборки."),
        2 => T("Укажите исполняемый файл игры, чтобы Fluxora знала, что запускать и где лежит игра."),
        _ => T("Выберите место, где будет создана папка новой сборки.")
    };

    public string CreateProjectStepCounter => F("{0} из 4", CreateProjectStepIndex + 1);

    public string TargetProjectDirectory
    {
        get
        {
            try
            {
                if (string.IsNullOrWhiteSpace(ProjectName) || string.IsNullOrWhiteSpace(InstallRootDirectory))
                {
                    return string.Empty;
                }

                string preview = projectCatalogService.BuildProjectDirectoryPreview(ProjectName, InstallRootDirectory);
                return string.IsNullOrWhiteSpace(preview)
                    ? T("C++ core рассчитает путь после подключения.")
                    : preview;
            }
            catch (Exception)
            {
                return string.Empty;
            }
        }
    }

    public string SelectedProjectCreatedText
    {
        get
        {
            if (SelectedProject is null)
            {
                return string.Empty;
            }

            return FormatProjectTimestamp(SelectedProject.CreatedAt, string.Empty);
        }
    }

    public string SelectedProjectLastLaunchText
    {
        get
        {
            return FormatProjectTimestamp(SelectedProject?.LastLaunchedAt, T("Ещё не запускалась"));
        }
    }

    public string SelectedProjectSizeText
    {
        get => selectedProjectSizeText;
        private set => SetField(ref selectedProjectSizeText, value);
    }

    public string SelectedProjectConfigText => SelectedProject?.ConfigPath ?? string.Empty;

    public string SelectedProjectExecutablesText => SelectedProject?.Executables is { Count: > 0 } executables
        ? string.Join("  ·  ", executables.Select(executable => executable.DisplayTitle))
        : string.Empty;

    public string SelectedProjectPluginExtensionsText => SelectedProject?.Template?.PluginExtensionsText ?? string.Empty;

    public string SelectedProjectContentLayoutSummaryText
    {
        get
        {
            ContentLayoutSummary? summary = SelectedProject?.ContentLayoutSummary;
            if (summary is null || string.IsNullOrWhiteSpace(summary.Summary))
            {
                summary = SelectedProject?.Template?.ContentLayoutSummary;
            }

            return string.IsNullOrWhiteSpace(summary?.Summary)
                ? T("Решения о размещении будут показаны после анализа архива.")
                : summary.Summary;
        }
    }

    public IReadOnlyList<string> SelectedProjectContentLayoutDetails =>
        SelectedProject?.ContentLayoutSummary?.Details is { Count: > 0 } projectDetails
            ? projectDetails
            : (IReadOnlyList<string>?)SelectedProject?.Template?.ContentLayoutSummary?.Details ?? Array.Empty<string>();

    public IReadOnlyList<string> SelectedProjectContentLayoutWarnings =>
        SelectedProject?.ContentLayoutSummary?.Warnings is { Count: > 0 } projectWarnings
            ? projectWarnings
            : (IReadOnlyList<string>?)SelectedProject?.Template?.ContentLayoutSummary?.Warnings ?? Array.Empty<string>();

    public IReadOnlyList<string> SelectedProjectContentLayoutBlockers =>
        SelectedProject?.ContentLayoutSummary?.Blockers is { Count: > 0 } projectBlockers
            ? projectBlockers
            : (IReadOnlyList<string>?)SelectedProject?.Template?.ContentLayoutSummary?.Blockers ?? Array.Empty<string>();

    public string SelectedProjectContentLayoutDataFolder =>
        SelectedProject?.ContentLayoutSummary?.DataFolder is { Length: > 0 } projectDataFolder
            ? projectDataFolder
            : SelectedProject?.Template?.ContentLayoutSummary?.DataFolder ?? string.Empty;

    public bool HasSelectedProjectContentLayoutDataFolder =>
        !string.IsNullOrWhiteSpace(SelectedProjectContentLayoutDataFolder);

    public string SelectedProjectRootFilesText
    {
        get
        {
            ContentLayoutSummary? summary = SelectedProject?.ContentLayoutSummary;
            if (summary is null || string.IsNullOrWhiteSpace(summary.RootFileWrapperDirectory))
            {
                summary = SelectedProject?.Template?.ContentLayoutSummary;
            }

            return string.IsNullOrWhiteSpace(summary?.RootFileWrapperDirectory)
                ? T("Root-файлы поддерживаются правилами выбранной игры.")
                : F("Root-файлы изолируются через: {0}", summary.RootFileWrapperDirectory);
        }
    }

    public string SelectedProjectScriptExtenderText
    {
        get
        {
            ScriptExtenderInfo? scriptExtender = SelectedProject?.Template?.ScriptExtender;
            if (scriptExtender is not null && !string.IsNullOrWhiteSpace(scriptExtender.Name))
            {
                return string.IsNullOrWhiteSpace(scriptExtender.LoaderExecutable)
                    ? scriptExtender.Name
                    : $"{scriptExtender.Name} · {scriptExtender.LoaderExecutable}";
            }

            IReadOnlyList<string> loaders = SelectedProject?.ContentLayoutSummary?.ScriptExtenderLoaders is { Count: > 0 } projectLoaders
                ? projectLoaders
                : (IReadOnlyList<string>?)SelectedProject?.Template?.ContentLayoutSummary?.ScriptExtenderLoaders ?? Array.Empty<string>();
            return loaders.Count == 0
                ? T("Script extender поддерживается выбранной игрой.")
                : string.Join("  ·  ", loaders);
        }
    }

    public string SelectedProjectIniProfilesText => CanShowIniPanel
        ? T("INI-профили подключаются только для игр с соответствующей capability.")
        : string.Empty;

    public string SelectedProjectSaveProfilesText => CanShowSavePanel
        ? T("Профили сохранений подключаются только для игр с соответствующей capability.")
        : string.Empty;

    public string SelectedProjectHealthStatusText
    {
        get
        {
            GameHealthSummary? health = SelectedProject?.GameHealthSummary;
            if (health is null)
            {
                return string.Empty;
            }

            return string.IsNullOrWhiteSpace(health.Summary)
                ? health.Status
                : $"{health.Status}: {health.Summary}";
        }
    }

    public IReadOnlyList<GameHealthFinding> SelectedProjectHealthFindings =>
        (IReadOnlyList<GameHealthFinding>?)SelectedProject?.GameHealthSummary?.Findings ?? Array.Empty<GameHealthFinding>();

    public async Task InitializeAsync(
        IEnumerable<string>? startupNxmLinks = null,
        CancellationToken cancellationToken = default)
    {
        await settingsService.InitializeAsync(cancellationToken);
        await coreBridgeService.InitializeAsync(cancellationToken);
        await nxmProtocolService.InitializeAsync(cancellationToken);
        await templateCatalogService.InitializeAsync(cancellationToken);
        await projectCatalogService.InitializeAsync(cancellationToken);
        await projectOpenService.InitializeAsync(cancellationToken);
        await modCatalogService.InitializeAsync(cancellationToken);
        await pluginCatalogService.InitializeAsync(cancellationToken);
        await downloadCatalogService.InitializeAsync(cancellationToken);
        await projectWorkspaceLoadService.InitializeAsync(cancellationToken);

        AvailableTemplates.Clear();
        foreach (GameTemplateOption template in templateCatalogService.GameTemplates)
        {
            AvailableTemplates.Add(template);
        }
        RebuildVisibleTemplates();

        Projects.Clear();
        foreach (ModProject project in await projectCatalogService.GetProjectsAsync(cancellationToken))
        {
            Projects.Add(project);
        }

        SelectedProject = Projects.FirstOrDefault();
        SelectedTemplate = AvailableTemplates.FirstOrDefault();
        ProjectsDirectory = settingsService.ProjectsDirectory;
        InstallRootDirectory = settingsService.ProjectsDirectory;
        CoreStatus = coreBridgeService.IsCoreAvailable
            ? "C++ core: ready"
            : "C++ core: unavailable";

        OnPropertyChanged(nameof(HasTemplates));
        OnPropertyChanged(nameof(HasVisibleTemplates));
        RaiseProjectCollectionStateChanged();

        RestoreActiveLaunchSession();

        if (startupNxmLinks is not null)
        {
            await CaptureNxmLinksAsync(startupNxmLinks, cancellationToken);
        }
    }

    public async Task CaptureNxmLinksAsync(
        IEnumerable<string> nxmLinks,
        CancellationToken cancellationToken = default)
    {
        IReadOnlyList<string> links = nxmLinks
            .Where(link => !string.IsNullOrWhiteSpace(link))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
        if (links.Count == 0)
        {
            return;
        }

        using var operation = logService.BeginOperation(
            "CaptureNxmLinks",
            $"linkCount={links.Count}, selectedProject=\"{SelectedProject?.Name ?? string.Empty}\"");
        try
        {
            IsProcessingDownload = true;
            if (SelectedProject is not null)
            {
                ModProject project = SelectedProject;
                SelectedWorkspaceTabIndex = DownloadsWorkspaceTabIndex;
                if (!IsProjectWorkspaceOpen)
                {
                    await OpenProjectWorkspaceAsync(project, importPendingDownloads: false);
                    SelectedWorkspaceTabIndex = DownloadsWorkspaceTabIndex;
                }

                Task captureTask = downloadCatalogService.CaptureNxmLinksAsync(project, links, cancellationToken);
                await RunDownloadOperationWithLiveRefreshAsync(project, captureTask, cancellationToken);
                ActivityMessage = $"NXM-ссылка принята в загрузки: {project.Name}";
                operation.Complete($"project=\"{project.Name}\"");
            }
            else
            {
                await downloadCatalogService.CaptureNxmLinksAsync(null, links, cancellationToken);
                ActivityMessage = "NXM-ссылка сохранена. Откройте или создайте сборку, чтобы перенести её в загрузки.";
                operation.Complete("queuedWithoutProject=true");
            }
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ActivityMessage = $"Не удалось принять NXM-ссылку: {exception.Message}";
        }
        finally
        {
            IsProcessingDownload = false;
        }
    }

    public async Task OpenTransferFlowAsync()
    {
        if (TransferViewModel?.IsTransferRunning == true)
        {
            ActivityMessage = "Перенос уже выполняется.";
            return;
        }

        ModProject? currentProject = IsProjectWorkspaceOpen ? SelectedProject : null;
        shouldReopenWorkspaceAfterTransfer = IsProjectWorkspaceOpen && currentProject is not null;
        transferReturnProject = currentProject;
        isTransferImportHandled = false;

        TransferViewModel = new SettingsWindowViewModel(
            coreBridgeService,
            settingsService,
            languageCatalogService,
            logService,
            folderPickerService,
            currentProject,
            currentProject is not null);

        IsCreateProjectPanelOpen = false;
        IsProjectWorkspaceOpen = false;
        ValidationMessage = string.Empty;
        ActivityMessage = "Перенос сборки открыт в основном окне.";

        await TransferViewModel.InitializeAsync();
        if (TransferViewModel.OpenModOrganizerTransferCommand.CanExecute(null))
        {
            TransferViewModel.OpenModOrganizerTransferCommand.Execute(null);
        }
    }

    private void OpenCreateProject()
    {
        IsProjectWorkspaceOpen = false;
        ProjectName = string.Empty;
        GameSearchText = string.Empty;
        SelectedTemplate = AvailableTemplates.FirstOrDefault();
        GamePath = string.Empty;
        InstallRootDirectory = string.IsNullOrWhiteSpace(ProjectsDirectory)
            ? settingsService.ProjectsDirectory
            : ProjectsDirectory;
        ValidationMessage = string.Empty;
        ActivityMessage = string.Empty;
        CreateProjectStepIndex = 0;
        IsCreateProjectPanelOpen = true;
    }

    private async void InstallFluxPack()
    {
        string initialDirectory = Directory.Exists(ProjectsDirectory)
            ? ProjectsDirectory
            : settingsService.BuildConfigsDirectory;
        string? selectedPath = fluxPackPickerService.PickFluxPack(initialDirectory);
        if (string.IsNullOrWhiteSpace(selectedPath))
        {
            return;
        }

        using var operation = logService.BeginOperation(
            "InstallFluxPack",
            $"fluxPackPath=\"{selectedPath}\", installRoot=\"{settingsService.ProjectsDirectory}\"");
        try
        {
            IsProcessingFluxPack = true;
            ValidationMessage = string.Empty;
            ActivityMessage = "Устанавливаю FluxPack...";
            FluxPackInstallProcess.Start(selectedPath);

            FluxPackInstallResult result = await coreBridgeService.InstallFluxPackAsync(
                selectedPath,
                settingsService.ProjectsDirectory,
                DispatchFluxPackInstallProgress);
            FluxPackInstallProcess.Complete(result);
            ActivityMessage = FormatFluxPackInstallMessage(result);
            operation.Complete(
                $"build=\"{result.BuildName}\", installedSources={result.InstalledSourceCount}, pendingSources={result.PendingSourceCount}, failedSources={result.FailedSourceCount}, configPath=\"{result.ConfigPath}\"");

            if (!result.HasWarnings)
            {
                await Task.Delay(FluxPackInstallSplashCompletionHold);
                FluxPackInstallProcess.Close();
            }

            IsProcessingFluxPack = false;
            if (!string.IsNullOrWhiteSpace(result.ConfigPath))
            {
                await OpenProjectAsync(
                    () => projectOpenService.OpenFromConfigAsync(result.ConfigPath),
                    $"Открываю установленную сборку: {result.BuildName}...");
            }
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            FluxPackInstallProcess.Fail(exception.Message);
            ValidationMessage = $"Не удалось установить FluxPack: {exception.Message}";
            ActivityMessage = string.Empty;
        }
        finally
        {
            IsProcessingFluxPack = false;
        }
    }

    private async void OpenProjectFromConfig()
    {
        string? selectedConfig = buildConfigPickerService.PickBuildConfig(settingsService.BuildConfigsDirectory);
        if (string.IsNullOrWhiteSpace(selectedConfig))
        {
            return;
        }

        await OpenProjectAsync(
            () => projectOpenService.OpenFromConfigAsync(selectedConfig),
            "Открываю сборку из JSON-конфига...");
    }

    private async void OpenProjectBuild(ModProject? project)
    {
        if (project is null)
        {
            return;
        }

        await OpenProjectAsync(
            () => projectOpenService.OpenProjectAsync(project),
            $"Открываю сборку: {project.Name}...");
    }

    private async void PackageProject(ModProject? project)
    {
        if (project is null)
        {
            return;
        }

        string initialDirectory = Directory.Exists(project.ProjectDirectory)
            ? project.ProjectDirectory
            : settingsService.BuildConfigsDirectory;
        string? outputPath = fluxPackPickerService.PickFluxPackSavePath(
            initialDirectory,
            project.Name + ".fluxpack");
        if (string.IsNullOrWhiteSpace(outputPath))
        {
            return;
        }

        bool includeGeneratedAssets =
            confirmDialogService.Confirm(ConfirmDialogOptions.IncludeGeneratedFluxPackAssets(project));

        using var operation = logService.BeginOperation(
            "ExportFluxPack",
            $"project=\"{project.Name}\", configPath=\"{project.ConfigPath}\", outputPath=\"{outputPath}\", includeGeneratedAssets={includeGeneratedAssets}");
        try
        {
            IsProcessingFluxPack = true;
            ValidationMessage = string.Empty;
            ActivityMessage = $"Упаковываю сборку в FluxPack: {project.Name}...";
            FluxPackPackageProcess.Start(project.Name, outputPath, includeGeneratedAssets);
            FluxPackPackageProcess.SetStage(
                FluxPackPackageProcessStage.Prepare,
                14,
                "Проверяем сборку",
                "Проверяем конфиг и путь сохранения");
            FluxPackPackageProcess.SetStage(
                FluxPackPackageProcessStage.Composition,
                34,
                "Собираем состав",
                includeGeneratedAssets
                    ? "Включаем рецепт и generated assets"
                    : "Готовим рецепт без generated assets");
            FluxPackPackageProcess.SetStage(
                FluxPackPackageProcessStage.Export,
                72,
                "Записываем FluxPack",
                "C++ core формирует манифест сборки");

            FluxPackSummary summary = await coreBridgeService.ExportFluxPackAsync(
                project.ConfigPath,
                outputPath,
                includeGeneratedAssets);
            FluxPackPackageProcess.SetStage(
                FluxPackPackageProcessStage.Summary,
                94,
                "Проверяем результат",
                "Собираем итоговую сводку");
            ActivityMessage = FormatFluxPackExportMessage(summary);
            FluxPackPackageProcess.Complete(summary);
            operation.Complete(
                $"sourceArchives={summary.SourceArchiveCount}, generatedAssets={summary.GeneratedAssetCount}, customPatches={summary.CustomPatchCount}, customConfigs={summary.CustomConfigCount}");
            await Task.Delay(FluxPackPackageSplashCompletionHold);
            FluxPackPackageProcess.Close();
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            FluxPackPackageProcess.Fail(exception.Message);
            ValidationMessage = $"Не удалось упаковать сборку: {exception.Message}";
            ActivityMessage = string.Empty;
        }
        finally
        {
            IsProcessingFluxPack = false;
        }
    }

    private async void RenameProject(ModProject? project)
    {
        if (project is null)
        {
            return;
        }

        string? newName = modInstallDialogService.PickProjectName(project.Name);
        if (string.IsNullOrWhiteSpace(newName) ||
            string.Equals(newName.Trim(), project.Name, StringComparison.Ordinal))
        {
            return;
        }

        using var operation = logService.BeginOperation(
            "RenameProject",
            $"project=\"{project.Name}\", configPath=\"{project.ConfigPath}\", newName=\"{newName.Trim()}\"");
        try
        {
            IsOpeningProject = true;
            ValidationMessage = string.Empty;
            ActivityMessage = $"Переименовываю сборку: {project.Name}...";

            bool wasSelected = IsSameProject(SelectedProject, project);
            ModProject renamedProject = await projectCatalogService.RenameProjectAsync(project, newName);
            MoveProjectSizeCache(project, renamedProject);
            ReplaceProject(project, renamedProject);

            if (wasSelected)
            {
                SelectedProject = renamedProject;
                if (IsProjectWorkspaceOpen)
                {
                    await OpenProjectWorkspaceAsync(renamedProject, importPendingDownloads: false);
                }
                RefreshSelectedProjectSizeAfterMutation();
            }

            ActivityMessage = $"Сборка переименована: {renamedProject.Name}";
            operation.Complete($"configPath=\"{renamedProject.ConfigPath}\"");
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ValidationMessage = $"Не удалось переименовать сборку: {exception.Message}";
            ActivityMessage = string.Empty;
        }
        finally
        {
            IsOpeningProject = false;
        }
    }

    private async void DeleteProject(ModProject? project)
    {
        if (project is null)
        {
            return;
        }

        if (!buildDeletionDialogService.Confirm(ConfirmDialogOptions.DeleteBuild(project)))
        {
            return;
        }

        using var operation = logService.BeginOperation(
            "DeleteProject",
            $"project=\"{project.Name}\", configPath=\"{project.ConfigPath}\", projectDirectory=\"{project.ProjectDirectory}\"");
        try
        {
            IsOpeningProject = true;
            ValidationMessage = string.Empty;
            ActivityMessage = $"Удаление сборки: {project.Name}";

            bool wasSelected = IsSameProject(SelectedProject, project);
            BuildDeletionProcess.Start();

            await projectCatalogService.DeleteProjectAsync(project, DispatchDeletionProgress);
            BuildDeletionProcess.Complete();
            await Task.Delay(BuildDeletionSplashCompletionHold);
            BuildDeletionProcess.Close();

            ForgetProjectSize(project);
            RemoveProject(project);

            if (wasSelected)
            {
                IsProjectWorkspaceOpen = false;
                ClearWorkspaceData();
                SelectedProject = Projects.FirstOrDefault();
            }

            ActivityMessage = $"Сборка удалена: {project.Name}";
            operation.Complete($"project=\"{project.Name}\"");
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            BuildDeletionProcess.Fail(exception.Message);
            ValidationMessage = "Не удалось удалить сборку.";
            ActivityMessage = string.Empty;
        }
        finally
        {
            IsOpeningProject = false;
        }
    }

    private void DispatchDeletionProgress(BuildDeletionProgress progress)
    {
        System.Windows.Application.Current.Dispatcher.BeginInvoke(
            () => BuildDeletionProcess.ApplyProgress(progress));
    }

    private void DispatchFluxPackInstallProgress(FluxPackInstallProgress progress)
    {
        RunOnUiThread(() => FluxPackInstallProcess.ApplyProgress(progress));
    }

    private async Task CompleteModOperationSplashAsync(string currentStep, string statusText)
    {
        ModOperationProcess.Complete(currentStep, statusText);
        await Task.Delay(ModOperationSplashCompletionHold);
        ModOperationProcess.Close();
    }

    private async Task OpenProjectAsync(
        Func<Task<ModProject>> openProject,
        string openingMessage)
    {
        try
        {
            if (!projectOpenService.CanOpenProjects)
            {
                ActivityMessage = string.Empty;
                ValidationMessage = "C++ core недоступен. Сначала соберите backend и положите FluxoraCore.dll рядом с приложением.";
                return;
            }

            IsOpeningProject = true;
            ValidationMessage = string.Empty;
            ActivityMessage = openingMessage;
            StartBuildLoadingSplash(openingMessage);

            UpdateBuildLoadingSplashDetail("Читаем конфиг и проверяем папки");
            ModProject project = await openProject();
            AddOrReplaceProject(project);
            SelectedProject = project;
            UpdateBuildLoadingSplashDetail($"Готовим страницу: {project.Name}");
            await OpenProjectWorkspaceAsync(project);
            IsCreateProjectPanelOpen = false;
            ActivityMessage = $"Сборка открыта, загружаем данные: {project.Name}";
        }
        catch (Exception exception)
        {
            ValidationMessage = $"Не удалось открыть сборку: {exception.Message}";
            ActivityMessage = string.Empty;
        }
        finally
        {
            await StopBuildLoadingSplashAsync();
            IsOpeningProject = false;
        }
    }

    private void CancelCreateProject()
    {
        ValidationMessage = string.Empty;
        ActivityMessage = string.Empty;
        IsCreateProjectPanelOpen = false;
    }

    private void BrowseGamePath()
    {
        string? selectedPath = executablePickerService.PickExecutable("Выберите исполняемый файл игры", GamePath);
        if (!string.IsNullOrWhiteSpace(selectedPath))
        {
            GamePath = selectedPath;
        }
    }

    private void BrowseInstallRoot()
    {
        string? selectedPath = folderPickerService.PickFolder("Выберите папку установки", InstallRootDirectory);
        if (!string.IsNullOrWhiteSpace(selectedPath))
        {
            InstallRootDirectory = selectedPath;
        }
    }

    private void MoveToPreviousStep()
    {
        ValidationMessage = string.Empty;
        CreateProjectStepIndex -= 1;
    }

    private void MoveToNextStep()
    {
        if (!ValidateCurrentStep())
        {
            return;
        }

        CreateProjectStepIndex += 1;
    }

    private async void CreateProject()
    {
        if (!ValidateCurrentStep())
        {
            return;
        }

        CancellationTokenSource? cancellation = null;
        ApplicationLogService.OperationLogScope? operation = null;
        try
        {
            if (!coreBridgeService.CanCreateProjectsNatively)
            {
                ValidationMessage = "C++ core недоступен. Сначала соберите backend и положите FluxoraCore.dll рядом с приложением.";
                return;
            }

            ResolvedTemplate? template = SelectedResolvedTemplate
                ?? templateCatalogService.Resolve(SelectedTemplate?.Id);
            if (template is null)
            {
                ValidationMessage = "Шаблон игры не удалось разрешить. Проверьте, что C++ core подключён.";
                return;
            }

            string targetDirectory = TargetProjectDirectory;
            cancellation = new CancellationTokenSource();
            createProjectCancellation = cancellation;
            IsCreatingProject = true;
            ActivityMessage = "Создаю структуру сборки из шаблона...";
            BuildCreationProcess.Start(ProjectName, template.DisplayName, targetDirectory);
            BuildCreationProcess.SetStage(
                BuildCreationProcessStage.Data,
                12,
                "Проверяем данные",
                "Проверяем имя, путь и выбранную игру");

            operation = logService.BeginOperation(
                "CreateProject",
                $"name=\"{ProjectName}\", template=\"{template.Id}\", gamePath=\"{GamePath}\", installRoot=\"{InstallRootDirectory}\"");
            cancellation.Token.ThrowIfCancellationRequested();
            BuildCreationProcess.SetStage(
                BuildCreationProcessStage.Template,
                34,
                "Готовим шаблон игры",
                string.IsNullOrWhiteSpace(template.DisplayName)
                    ? "Накладываем игровой шаблон на базовую структуру"
                    : $"Накладываем шаблон: {template.DisplayName}");
            cancellation.Token.ThrowIfCancellationRequested();
            BuildCreationProcess.SetStage(
                BuildCreationProcessStage.Files,
                68,
                "Создаём структуру",
                "C++ core записывает папки, профиль и конфиг");
            ModProject project = await projectCatalogService.CreateProjectAsync(
                ProjectName,
                template,
                GamePath,
                InstallRootDirectory,
                cancellation.Token);

            BuildCreationProcess.SetStage(
                BuildCreationProcessStage.Catalog,
                92,
                "Обновляем каталог",
                "Добавляем сборку в локальную библиотеку");
            AddOrReplaceProject(project);
            SelectedProject = project;
            RefreshSelectedProjectSizeAfterMutation();
            IsProjectWorkspaceOpen = false;
            IsCreateProjectPanelOpen = false;
            ActivityMessage = $"Сборка создана: {project.ProjectDirectory}. Данные: {project.ConfigPath}";
            BuildCreationProcess.Complete(project.Name);
            operation.Complete($"projectDirectory=\"{project.ProjectDirectory}\", configPath=\"{project.ConfigPath}\"");
            await Task.Delay(BuildCreationSplashCompletionHold);
            BuildCreationProcess.Close();
        }
        catch (OperationCanceledException) when (cancellation?.IsCancellationRequested == true)
        {
            operation?.Complete("canceled=true");
            BuildCreationProcess.Cancel();
            ValidationMessage = "Создание сборки отменено.";
            ActivityMessage = "Создание сборки отменено.";
        }
        catch (Exception exception)
        {
            operation?.Fail(exception);
            BuildCreationProcess.Fail(exception.Message);
            ValidationMessage = $"Не удалось создать сборку: {exception.Message}";
            ActivityMessage = string.Empty;
        }
        finally
        {
            if (ReferenceEquals(createProjectCancellation, cancellation))
            {
                createProjectCancellation = null;
            }

            cancellation?.Dispose();
            operation?.Dispose();
            IsCreatingProject = false;
        }
    }

    private bool ValidateCurrentStep()
    {
        if (CreateProjectStepIndex == 0 && string.IsNullOrWhiteSpace(ProjectName))
        {
            ValidationMessage = "Введите название сборки.";
            return false;
        }

        if (CreateProjectStepIndex == 1)
        {
            if (SelectedTemplate is null)
            {
                ValidationMessage = HasTemplates
                    ? "Выберите игру."
                    : "Список игр пуст: C++ core не подключён, шаблоны недоступны.";
                return false;
            }
        }

        if (CreateProjectStepIndex == 2)
        {
            if (string.IsNullOrWhiteSpace(GamePath))
            {
                ValidationMessage = "Выберите .exe игры.";
                return false;
            }
        }

        if (CreateProjectStepIndex == 3)
        {
            if (string.IsNullOrWhiteSpace(InstallRootDirectory))
            {
                ValidationMessage = "Выберите папку установки.";
                return false;
            }

            if (string.IsNullOrWhiteSpace(TargetProjectDirectory))
            {
                ValidationMessage = "Путь сборки пока не рассчитан.";
                return false;
            }
        }

        ValidationMessage = string.Empty;
        return true;
    }

    public Task HandleImportedProjectAsync(ModProject project)
    {
        return HandleImportedProjectAsync(project, IsProjectWorkspaceOpen || IsSameProject(SelectedProject, project));
    }

    private async Task HandleImportedProjectAsync(ModProject project, bool shouldReopenWorkspace)
    {
        string refreshWarning = string.Empty;
        ModProject projectToShow = project;

        try
        {
            projectToShow = await projectOpenService.OpenProjectAsync(project);
        }
        catch (Exception exception)
        {
            refreshWarning = $"Конфиг не удалось перечитать автоматически: {exception.Message}";
        }

        AddOrReplaceProject(projectToShow);
        SelectedProject = projectToShow;
        RefreshSelectedProjectSizeAfterMutation();
        IsCreateProjectPanelOpen = false;
        ValidationMessage = string.Empty;

        if (shouldReopenWorkspace)
        {
            await OpenProjectWorkspaceAsync(projectToShow, importPendingDownloads: false);
            ActivityMessage = FormatImportActivityMessage(
                $"Сборка перенесена и открыта: {projectToShow.Name}",
                refreshWarning);
            return;
        }

        IsProjectWorkspaceOpen = false;
        ActivityMessage = FormatImportActivityMessage(
            $"Сборка добавлена: {projectToShow.Name}",
            refreshWarning);
    }

    private async void OnTransferViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (sender is not SettingsWindowViewModel transferFlow ||
            !ReferenceEquals(transferFlow, TransferViewModel))
        {
            return;
        }

        if (e.PropertyName == nameof(SettingsWindowViewModel.ImportedProject) &&
            transferFlow.ImportedProject is not null &&
            !isTransferImportHandled)
        {
            isTransferImportHandled = true;
            await HandleImportedProjectAsync(
                transferFlow.ImportedProject,
                shouldReopenWorkspaceAfterTransfer);
        }

        if (e.PropertyName == nameof(SettingsWindowViewModel.IsTransferRunning))
        {
            OnPropertyChanged(nameof(IsTransferPanelClosable));
            (CloseTransferPanelCommand as RelayCommand)?.RaiseCanExecuteChanged();
        }

        if (e.PropertyName == nameof(SettingsWindowViewModel.IsTransferStepperOpen) &&
            !transferFlow.IsTransferStepperOpen &&
            !transferFlow.IsTransferProcessVisible)
        {
            CloseTransferPanel();
        }
    }

    private void CloseTransferPanel()
    {
        if (TransferViewModel?.IsTransferRunning == true)
        {
            ActivityMessage = "Дождитесь завершения переноса.";
            return;
        }

        bool hasImportedProject = TransferViewModel?.ImportedProject is not null;
        bool shouldRestoreWorkspace = shouldReopenWorkspaceAfterTransfer &&
            !hasImportedProject &&
            transferReturnProject is not null;

        TransferViewModel = null;
        if (shouldRestoreWorkspace)
        {
            IsProjectWorkspaceOpen = true;
        }

        transferReturnProject = null;
        shouldReopenWorkspaceAfterTransfer = false;
        isTransferImportHandled = false;

        if (!hasImportedProject)
        {
            ActivityMessage = "Перенос отменен.";
        }
    }

    private void RequestCancelBuildCreation()
    {
        if (!BuildCreationProcess.CanCancel)
        {
            return;
        }

        BuildCreationProcess.RequestCancel();
        ActivityMessage = "Отмена создания сборки...";

        try
        {
            createProjectCancellation?.Cancel();
        }
        catch (ObjectDisposedException)
        {
        }
    }

    private void CloseBuildCreationProcess()
    {
        BuildCreationProcess.Close();
    }

    private void CloseBuildDeletionProcess()
    {
        BuildDeletionProcess.Close();
    }

    private void CloseFluxPackPackageProcess()
    {
        FluxPackPackageProcess.Close();
    }

    private void CloseFluxPackInstallProcess()
    {
        FluxPackInstallProcess.Close();
    }

    private void OnBuildCreationProcessPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(BuildCreationProcessViewModel.CanCancel) or
            nameof(BuildCreationProcessViewModel.CanClose) or
            nameof(BuildCreationProcessViewModel.IsVisible) or
            nameof(BuildCreationProcessViewModel.IsRunning) or
            nameof(BuildCreationProcessViewModel.IsCancellationRequested))
        {
            (CancelBuildCreationCommand as RelayCommand)?.RaiseCanExecuteChanged();
            (CloseBuildCreationProcessCommand as RelayCommand)?.RaiseCanExecuteChanged();
            RaiseBusyCommandStateChanged();
        }
    }

    private void OnBuildDeletionProcessPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(BuildDeletionProcessViewModel.CanClose) or
            nameof(BuildDeletionProcessViewModel.IsVisible) or
            nameof(BuildDeletionProcessViewModel.IsRunning))
        {
            (CloseBuildDeletionProcessCommand as RelayCommand)?.RaiseCanExecuteChanged();
            RaiseBusyCommandStateChanged();
        }
    }

    private void OnFluxPackPackageProcessPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(FluxPackPackageProcessViewModel.CanClose) or
            nameof(FluxPackPackageProcessViewModel.IsVisible) or
            nameof(FluxPackPackageProcessViewModel.IsRunning))
        {
            (CloseFluxPackPackageProcessCommand as RelayCommand)?.RaiseCanExecuteChanged();
            RaiseBusyCommandStateChanged();
        }
    }

    private void OnFluxPackInstallProcessPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(FluxPackInstallProcessViewModel.CanClose) or
            nameof(FluxPackInstallProcessViewModel.IsVisible) or
            nameof(FluxPackInstallProcessViewModel.IsRunning))
        {
            (CloseFluxPackInstallProcessCommand as RelayCommand)?.RaiseCanExecuteChanged();
            RaiseBusyCommandStateChanged();
        }
    }

    private void OnModOperationProcessPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(ModOperationProcessViewModel.IsVisible) or
            nameof(ModOperationProcessViewModel.IsRunning) or
            nameof(ModOperationProcessViewModel.CanClose))
        {
            OnPropertyChanged(nameof(CanImportDownloadFiles));
            RaiseBusyCommandStateChanged();
        }
    }

    private void OnExecutableLaunchProcessPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(ExecutableLaunchProcessViewModel.IsVisible) or
            nameof(ExecutableLaunchProcessViewModel.IsStarting) or
            nameof(ExecutableLaunchProcessViewModel.IsProcessRunning) or
            nameof(ExecutableLaunchProcessViewModel.CanClose))
        {
            (CloseExecutableLaunchProcessCommand as RelayCommand)?.RaiseCanExecuteChanged();
            RaiseBusyCommandStateChanged();
        }
    }

    private void BackToProjects()
    {
        CancelWorkspaceLoad();
        IsProjectWorkspaceOpen = false;
        ClearWorkspaceData();
        RefreshSelectedProjectSize();
        RefreshSelectedProjectModCounts();
        ActivityMessage = string.Empty;
    }

    private async Task OpenProjectWorkspaceAsync(
        ModProject project,
        bool importPendingDownloads = true)
    {
        UpdateBuildLoadingSplashDetail("Подготавливаем профили и запуск");
        ClearWorkspaceData();
        PrepareWorkspaceOptions(project);
        UpdateBuildLoadingSplashDetail("Загружаем моды, плагины и загрузки");
        await StartWorkspaceLoad(project, importPendingDownloads);
        UpdateBuildLoadingSplashDetail("Открываем рабочую область");
        IsProjectWorkspaceOpen = true;
    }

    private Task StartWorkspaceLoad(ModProject project, bool importPendingDownloads)
    {
        CancelWorkspaceLoad();

        CancellationTokenSource cancellation = new();
        workspaceLoadCancellation = cancellation;
        long version = ++workspaceLoadVersion;
        string profileName = SelectedProfile;

        Task task = LoadWorkspaceDataAsync(project, profileName, importPendingDownloads, version, cancellation.Token);
        workspaceLoadTask = task;
        _ = ClearCompletedWorkspaceLoadAsync(task, cancellation, version);
        return task;
    }

    private void CancelWorkspaceLoad()
    {
        CancellationTokenSource? cancellation = workspaceLoadCancellation;
        workspaceLoadCancellation = null;
        workspaceLoadTask = null;
        ++workspaceLoadVersion;

        if (cancellation is null)
        {
            return;
        }

        try
        {
            cancellation.Cancel();
        }
        catch (ObjectDisposedException)
        {
        }
    }

    private async Task ClearCompletedWorkspaceLoadAsync(
        Task task,
        CancellationTokenSource cancellation,
        long version)
    {
        try
        {
            await task;
        }
        catch
        {
        }
        finally
        {
            if (ReferenceEquals(workspaceLoadCancellation, cancellation) &&
                version == workspaceLoadVersion)
            {
                workspaceLoadCancellation = null;
                workspaceLoadTask = null;
            }

            cancellation.Dispose();
        }
    }

    private async Task LoadWorkspaceDataAsync(
        ModProject project,
        string profileName,
        bool importPendingDownloads,
        long version,
        CancellationToken cancellationToken)
    {
        try
        {
            ActivityMessage = $"Загружаем данные сборки: {project.Name}";
            ProjectWorkspaceLoadResult result = await projectWorkspaceLoadService.LoadAsync(
                project,
                profileName,
                includeDownloads: true,
                cancellationToken);

            if (!IsCurrentWorkspaceLoad(project, profileName, version, cancellationToken))
            {
                return;
            }

            ApplyWorkspaceLoadResult(result);

            if (importPendingDownloads)
            {
                UpdateBuildLoadingSplashDetail("Проверяем входящие NXM-ссылки");
                await ImportPendingDownloadsAsync(project, cancellationToken);
            }

            if (!IsCurrentWorkspaceLoad(project, profileName, version, cancellationToken))
            {
                return;
            }

            ActivityMessage = result.HasErrors
                ? BuildWorkspaceLoadErrorMessage(result)
                : $"Mod Manager открыт: {project.Name}";
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            if (IsCurrentWorkspaceLoad(project, profileName, version, cancellationToken))
            {
                ActivityMessage = $"Не удалось загрузить данные сборки: {exception.Message}";
            }
        }
    }

    private bool IsCurrentWorkspaceLoad(
        ModProject project,
        string profileName,
        long version,
        CancellationToken cancellationToken)
    {
        return !cancellationToken.IsCancellationRequested &&
            version == workspaceLoadVersion &&
            IsSameProject(SelectedProject, project) &&
            string.Equals(SelectedProfile, profileName, StringComparison.OrdinalIgnoreCase);
    }

    private void ApplyWorkspaceLoadResult(ProjectWorkspaceLoadResult result)
    {
        ApplyModsSection(result.Mods, null);
        ApplyPluginsSection(result.Plugins, null);
        if (result.Downloads is not null)
        {
            ApplyDownloadsSection(result.Downloads, null);
        }
    }

    private void ApplyProfileLoadResult(
        ProjectWorkspaceProfileLoadResult result,
        string? selectedModId,
        string? selectedPluginId)
    {
        ApplyModsSection(result.Mods, selectedModId);
        ApplyPluginsSection(result.Plugins, selectedPluginId);
    }

    private void ApplyModsSection(ProjectWorkspaceLoadSection<ModEntry> section, string? selectedModId)
    {
        SyncMods(section.Items, selectedModId);
        NotifyModCountPropertiesChanged();
        RaiseModCommandStateChanged();
    }

    private void NotifyModCountPropertiesChanged(bool updateSelectedProjectCounts = true)
    {
        if (updateSelectedProjectCounts)
        {
            UpdateSelectedProjectModCounts(Mods);
        }

        OnPropertyChanged(nameof(HasMods));
        OnPropertyChanged(nameof(HasModOrderItems));
        OnPropertyChanged(nameof(HasVisibleModOrderItems));
        OnPropertyChanged(nameof(ModCountText));
        OnPropertyChanged(nameof(ModListEmptyTitle));
        OnPropertyChanged(nameof(ModListEmptySubtitle));
        OnPropertyChanged(nameof(ModSearchResultText));
        OnPropertyChanged(nameof(ActiveModCountText));
        OnPropertyChanged(nameof(DisabledModCountText));
        OnPropertyChanged(nameof(ProjectModBreakdownText));
    }

    private void NotifyVisibleModsChanged()
    {
        OnPropertyChanged(nameof(HasVisibleModOrderItems));
        OnPropertyChanged(nameof(ModSearchResultText));
        OnPropertyChanged(nameof(GameSearchResultText));
        OnPropertyChanged(nameof(ModListEmptyTitle));
        OnPropertyChanged(nameof(ModListEmptySubtitle));
    }

    private void ApplyPluginsSection(ProjectWorkspaceLoadSection<PluginEntry> section, string? selectedPluginId)
    {
        SyncPlugins(section.Items, selectedPluginId);
        OnPropertyChanged(nameof(HasPlugins));
        OnPropertyChanged(nameof(PluginCountText));
    }

    private void ApplyDownloadsSection(ProjectWorkspaceLoadSection<DownloadEntry> section, string? selectedDownloadId)
    {
        SyncDownloads(section.Items, selectedDownloadId);
        OnPropertyChanged(nameof(HasDownloads));
        OnPropertyChanged(nameof(DownloadCountText));
    }

    private static string BuildWorkspaceLoadErrorMessage(ProjectWorkspaceLoadResult result)
    {
        List<string> parts = new();
        if (result.Mods.Error is not null)
        {
            parts.Add($"моды: {result.Mods.Error.Message}");
        }
        if (result.Plugins.Error is not null)
        {
            parts.Add($"плагины: {result.Plugins.Error.Message}");
        }
        if (result.Downloads?.Error is not null)
        {
            parts.Add($"загрузки: {result.Downloads.Error.Message}");
        }

        return parts.Count == 0
            ? "Данные сборки загружены."
            : $"Сборка открыта частично: {string.Join("; ", parts)}";
    }

    private static string BuildProfileLoadErrorMessage(ProjectWorkspaceProfileLoadResult result)
    {
        List<string> parts = new();
        if (result.Mods.Error is not null)
        {
            parts.Add($"моды: {result.Mods.Error.Message}");
        }
        if (result.Plugins.Error is not null)
        {
            parts.Add($"плагины: {result.Plugins.Error.Message}");
        }

        return parts.Count == 0
            ? "Профиль обновлён."
            : $"Профиль обновлён частично: {string.Join("; ", parts)}";
    }

    private void StartBuildLoadingSplash(string detail)
    {
        buildLoadingSplashPhraseCancellation?.Cancel();
        buildLoadingSplashPhraseCancellation?.Dispose();

        buildLoadingSplashPhraseIndex = 0;
        BuildLoadingSplashPhrase = BuildLoadingSplashPhrases[buildLoadingSplashPhraseIndex];
        BuildLoadingSplashDetail = string.IsNullOrWhiteSpace(detail)
            ? "Готовим страницу сборки"
            : detail;
        IsBuildLoadingSplashVisible = true;
        buildLoadingSplashStopwatch = Stopwatch.StartNew();

        CancellationTokenSource phraseCancellation = new();
        buildLoadingSplashPhraseCancellation = phraseCancellation;
        buildLoadingSplashPhraseTask = RotateBuildLoadingSplashPhrasesAsync(phraseCancellation.Token);
    }

    private void UpdateBuildLoadingSplashDetail(string detail)
    {
        if (IsBuildLoadingSplashVisible && !string.IsNullOrWhiteSpace(detail))
        {
            BuildLoadingSplashDetail = detail;
        }
    }

    private async Task StopBuildLoadingSplashAsync()
    {
        CancellationTokenSource? phraseCancellation = buildLoadingSplashPhraseCancellation;
        Task? phraseTask = buildLoadingSplashPhraseTask;
        buildLoadingSplashPhraseCancellation = null;
        buildLoadingSplashPhraseTask = null;

        if (phraseCancellation is not null)
        {
            phraseCancellation.Cancel();
            try
            {
                if (phraseTask is not null)
                {
                    await phraseTask;
                }
            }
            catch (OperationCanceledException)
            {
            }
            finally
            {
                phraseCancellation.Dispose();
            }
        }

        TimeSpan elapsed = buildLoadingSplashStopwatch?.Elapsed ?? BuildLoadingSplashMinimumDuration;
        if (elapsed < BuildLoadingSplashMinimumDuration)
        {
            await Task.Delay(BuildLoadingSplashMinimumDuration - elapsed);
        }

        buildLoadingSplashStopwatch = null;
        IsBuildLoadingSplashVisible = false;
        BuildLoadingSplashDetail = "Готовим страницу сборки";
    }

    private async Task RotateBuildLoadingSplashPhrasesAsync(CancellationToken cancellationToken)
    {
        using PeriodicTimer timer = new(BuildLoadingSplashPhraseInterval);
        while (await timer.WaitForNextTickAsync(cancellationToken))
        {
            buildLoadingSplashPhraseIndex = (buildLoadingSplashPhraseIndex + 1) % BuildLoadingSplashPhrases.Length;
            BuildLoadingSplashPhrase = BuildLoadingSplashPhrases[buildLoadingSplashPhraseIndex];
        }
    }

    private async void RefreshProjectWorkspace()
    {
        if (SelectedProject is null)
        {
            return;
        }

        ModProject project = SelectedProject;
        PrepareWorkspaceOptions(project);
        await StartWorkspaceLoad(project, importPendingDownloads: true);
        if (IsSameProject(SelectedProject, project))
        {
            RefreshSelectedProjectSizeAfterMutation();
            ActivityMessage = $"Сборка обновлена: {project.Name}";
        }
    }

    private async Task ImportPendingDownloadsAsync(
        ModProject project,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        bool wasProcessingDownload = IsProcessingDownload;
        try
        {
            IsProcessingDownload = true;
            Task importTask = downloadCatalogService.ImportPendingLinksAsync(project, cancellationToken);
            await RunDownloadOperationWithLiveRefreshAsync(project, importTask, cancellationToken);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (Exception exception)
        {
            ActivityMessage = $"Не удалось перенести входящие NXM-ссылки: {exception.Message}";
        }
        finally
        {
            IsProcessingDownload = wasProcessingDownload;
        }
    }

    private async Task LoadDownloadsFromProjectAsync(ModProject project)
    {
        try
        {
            string? selectedDownloadId = SelectedDownload?.Id;
            IReadOnlyList<DownloadEntry> downloads = await downloadCatalogService.GetDownloadsAsync(project);
            SyncDownloads(downloads, selectedDownloadId);
            OnPropertyChanged(nameof(HasDownloads));
            OnPropertyChanged(nameof(DownloadCountText));
        }
        catch (Exception exception)
        {
            ActivityMessage = $"Не удалось обновить загрузки: {exception.Message}";
        }
    }

    private void PrepareWorkspaceOptions(ModProject project)
    {
        AvailableProfiles.Clear();
        AvailableProfiles.Add(DefaultProfileName(project));
        SelectedProfile = AvailableProfiles.FirstOrDefault() ?? string.Empty;

        SyncExecutableMenu(project.Executables, lastSelectedExecutableId);
        CoerceSelectedWorkspaceTabForCapabilities();
    }

    private void CoerceSelectedWorkspaceTabForCapabilities()
    {
        if (SelectedWorkspaceTabIndex == PluginsWorkspaceTabIndex && !CanShowPluginsPanel)
        {
            SelectedWorkspaceTabIndex = DataWorkspaceTabIndex;
            return;
        }

        if (SelectedWorkspaceTabIndex == BuildWorkspaceTabIndex && !CanShowBuildOverviewPanel)
        {
            SelectedWorkspaceTabIndex = DataWorkspaceTabIndex;
        }
    }

    private void SyncExecutableMenu(
        IReadOnlyList<GameExecutableEntry> executables,
        string? preferredExecutableId)
    {
        AvailableExecutables.Clear();
        foreach (GameExecutableEntry executable in executables.Where(item => !string.IsNullOrWhiteSpace(item.ExecutablePath)))
        {
            AvailableExecutables.Add(ExecutableMenuItem.FromExecutable(executable));
        }
        if (!AvailableExecutables.Any(item => item.Executable is not null))
        {
            AvailableExecutables.Add(ExecutableMenuItem.Placeholder());
        }
        AvailableExecutables.Add(ExecutableMenuItem.More());

        ExecutableMenuItem? preferred = AvailableExecutables.FirstOrDefault(item =>
            !item.OpensManager &&
            !string.IsNullOrWhiteSpace(preferredExecutableId) &&
            string.Equals(item.Id, preferredExecutableId, StringComparison.OrdinalIgnoreCase));

        SelectedExecutable = preferred ?? AvailableExecutables.FirstOrDefault(item => !item.OpensManager);
        OnPropertyChanged(nameof(SelectedProjectExecutablesText));
    }

    private void RestorePreviousExecutableSelection()
    {
        ExecutableMenuItem? previous = AvailableExecutables.FirstOrDefault(item =>
            !item.OpensManager &&
            string.Equals(item.Id, lastSelectedExecutableId, StringComparison.OrdinalIgnoreCase));
        previous ??= AvailableExecutables.FirstOrDefault(item => !item.OpensManager);

        if (ReferenceEquals(selectedExecutable, previous))
        {
            return;
        }

        selectedExecutable = previous;
        OnPropertyChanged(nameof(SelectedExecutable));
        OnPropertyChanged(nameof(SelectedExecutableToolTip));
        (LaunchSelectedExecutableCommand as RelayCommand)?.RaiseCanExecuteChanged();
    }

    private async void OpenExecutableManager()
    {
        if (SelectedProject is null)
        {
            return;
        }

        IReadOnlyList<GameExecutableEntry>? edited = executableManagerDialogService.EditExecutables(
            SelectedProject.Executables,
            SelectedProject.GamePath,
            SelectedProject.ProjectDirectory);
        if (edited is null)
        {
            return;
        }

        try
        {
            IReadOnlyList<GameExecutableEntry> saved = await coreBridgeService.SaveGameExecutablesAsync(
                SelectedProject.ConfigPath,
                edited);
            SelectedProject.Executables = saved.ToList();
            SyncExecutableMenu(SelectedProject.Executables, lastSelectedExecutableId);
            OnPropertyChanged(nameof(SelectedProjectExecutablesText));
            ActivityMessage = saved.Count == 0
                ? "Список исполняемых файлов очищен."
                : $"Исполняемые файлы сохранены: {saved.Count}.";
        }
        catch (Exception exception)
        {
            ActivityMessage = $"Не удалось сохранить исполняемые файлы: {exception.Message}";
        }
    }

    private async void OpenBuildSettings()
    {
        if (SelectedProject is null)
        {
            return;
        }

        BuildSettingsResult? saved = buildSettingsDialogService.EditBuildPaths(SelectedProject);
        if (saved is null)
        {
            return;
        }

        SelectedProject.ApplyPathSettings(saved.Paths);
        SelectedProject.Executables = saved.Executables.Select(executable => executable.Clone()).ToList();
        SyncExecutableMenu(SelectedProject.Executables, lastSelectedExecutableId);
        OnPropertyChanged(nameof(WorkspaceSubtitle));
        OnPropertyChanged(nameof(SelectedProjectConfigText));
        OnPropertyChanged(nameof(SelectedProjectExecutablesText));
        NotifyCapabilityPropertiesChanged();
        ActivityMessage = "Пути сборки сохранены. Обновляю рабочую область...";

        PrepareWorkspaceOptions(SelectedProject);
        await StartWorkspaceLoad(SelectedProject, importPendingDownloads: false);
        if (SelectedProject is not null)
        {
            ActivityMessage = "Пути сборки применены.";
            RefreshSelectedProjectSizeAfterMutation();
        }
    }

    private void RefreshSelectedProjectSize(bool forceRecalculate = false)
    {
        if (SelectedProject is null)
        {
            selectedProjectSizeCacheKey = string.Empty;
            SelectedProjectSizeText = string.Empty;
            return;
        }

        string projectDirectory = SelectedProject.ProjectDirectory;
        string cacheKey = ProjectSizeCacheKey(projectDirectory);
        selectedProjectSizeCacheKey = cacheKey;

        if (string.IsNullOrWhiteSpace(projectDirectory) || !Directory.Exists(projectDirectory))
        {
            projectSizeTextCache.Remove(cacheKey);
            CancelProjectSizeCalculation(cacheKey);
            SelectedProjectSizeText = T("Папка не найдена");
            return;
        }

        if (projectSizeTextCache.TryGetValue(cacheKey, out string? cachedText) && cachedText is not null)
        {
            SelectedProjectSizeText = cachedText;
            if (!forceRecalculate)
            {
                return;
            }
        }
        else
        {
            SelectedProjectSizeText = T("Считаем...");
        }

        if (forceRecalculate)
        {
            CancelProjectSizeCalculation(cacheKey);
        }
        else if (projectSizeCalculations.ContainsKey(cacheKey))
        {
            return;
        }

        CancellationTokenSource cancellation = new();
        projectSizeCalculations[cacheKey] = cancellation;
        _ = RefreshSelectedProjectSizeAsync(projectDirectory, cacheKey, cancellation);
    }

    private void RefreshSelectedProjectSizeAfterMutation()
    {
        RefreshSelectedProjectSize(forceRecalculate: true);
    }

    private void ForgetProjectSize(ModProject project)
    {
        string cacheKey = ProjectSizeCacheKey(project.ProjectDirectory);
        if (string.IsNullOrWhiteSpace(cacheKey))
        {
            return;
        }

        projectSizeTextCache.Remove(cacheKey);
        CancelProjectSizeCalculation(cacheKey);
    }

    private void MoveProjectSizeCache(ModProject oldProject, ModProject newProject)
    {
        string oldKey = ProjectSizeCacheKey(oldProject.ProjectDirectory);
        string newKey = ProjectSizeCacheKey(newProject.ProjectDirectory);
        if (string.IsNullOrWhiteSpace(oldKey) ||
            string.IsNullOrWhiteSpace(newKey) ||
            string.Equals(oldKey, newKey, StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        CancelProjectSizeCalculation(oldKey);
        if (projectSizeTextCache.Remove(oldKey, out string? cachedText) && cachedText is not null)
        {
            projectSizeTextCache[newKey] = cachedText;
        }
    }

    private async Task RefreshSelectedProjectSizeAsync(
        string projectDirectory,
        string cacheKey,
        CancellationTokenSource cancellation)
    {
        try
        {
            ulong size = await Task.Run(
                () => CalculateDirectorySize(projectDirectory, cancellation.Token),
                cancellation.Token);

            if (!IsActiveProjectSizeCalculation(cacheKey, cancellation) || cancellation.IsCancellationRequested)
            {
                return;
            }

            string sizeText = TransferDriveOption.FormatBytes(size);
            projectSizeTextCache[cacheKey] = sizeText;
            if (IsSelectedProjectSizeCacheKey(cacheKey))
            {
                SelectedProjectSizeText = sizeText;
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception)
        {
            if (IsActiveProjectSizeCalculation(cacheKey, cancellation) && !cancellation.IsCancellationRequested)
            {
                string unavailableText = T("Недоступно");
                projectSizeTextCache[cacheKey] = unavailableText;
                if (IsSelectedProjectSizeCacheKey(cacheKey))
                {
                    SelectedProjectSizeText = unavailableText;
                }
            }
        }
        finally
        {
            if (IsActiveProjectSizeCalculation(cacheKey, cancellation))
            {
                projectSizeCalculations.Remove(cacheKey);
            }

            cancellation.Dispose();
        }
    }

    private void CancelProjectSizeCalculation(string cacheKey)
    {
        if (!projectSizeCalculations.Remove(cacheKey, out CancellationTokenSource? cancellation))
        {
            return;
        }

        try
        {
            cancellation.Cancel();
        }
        catch (ObjectDisposedException)
        {
        }
    }

    private bool IsActiveProjectSizeCalculation(string cacheKey, CancellationTokenSource cancellation)
    {
        return projectSizeCalculations.TryGetValue(cacheKey, out CancellationTokenSource? current) &&
            ReferenceEquals(current, cancellation);
    }

    private bool IsSelectedProjectSizeCacheKey(string cacheKey)
    {
        return string.Equals(selectedProjectSizeCacheKey, cacheKey, StringComparison.OrdinalIgnoreCase);
    }

    internal static string ProjectSizeCacheKey(string projectDirectory)
    {
        if (string.IsNullOrWhiteSpace(projectDirectory))
        {
            return string.Empty;
        }

        string trimmed = projectDirectory.Trim();
        try
        {
            trimmed = Path.GetFullPath(trimmed);
        }
        catch (Exception exception) when (exception is ArgumentException or NotSupportedException or PathTooLongException)
        {
        }

        return trimmed.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
    }

    internal static ulong CalculateDirectorySize(string projectDirectory, CancellationToken cancellationToken)
    {
        System.IO.EnumerationOptions options = new()
        {
            RecurseSubdirectories = true,
            IgnoreInaccessible = true,
            AttributesToSkip = FileAttributes.ReparsePoint
        };

        ulong totalBytes = 0;
        int filesSinceCancellationCheck = 0;
        foreach (string filePath in Directory.EnumerateFiles(projectDirectory, "*", options))
        {
            if (++filesSinceCancellationCheck >= 256)
            {
                cancellationToken.ThrowIfCancellationRequested();
                filesSinceCancellationCheck = 0;
            }

            try
            {
                totalBytes += (ulong)Math.Max(0, new FileInfo(filePath).Length);
            }
            catch (IOException)
            {
            }
            catch (UnauthorizedAccessException)
            {
            }
        }

        cancellationToken.ThrowIfCancellationRequested();
        return totalBytes;
    }

    private void RefreshSelectedProjectModCounts()
    {
        projectModCountCalculationCancellation?.Cancel();

        if (SelectedProject is null)
        {
            selectedProjectActiveModCount = null;
            selectedProjectDisabledModCount = null;
            selectedProjectModCountUnavailable = false;
            OnPropertyChanged(nameof(ActiveModCountText));
            OnPropertyChanged(nameof(DisabledModCountText));
            OnPropertyChanged(nameof(ProjectModBreakdownText));
            return;
        }

        if (IsProjectWorkspaceOpen && Mods.Count > 0)
        {
            UpdateSelectedProjectModCounts(Mods);
            return;
        }

        selectedProjectActiveModCount = null;
        selectedProjectDisabledModCount = null;
        selectedProjectModCountUnavailable = false;
        OnPropertyChanged(nameof(ActiveModCountText));
        OnPropertyChanged(nameof(DisabledModCountText));
        OnPropertyChanged(nameof(ProjectModBreakdownText));

        ModProject project = SelectedProject;
        string profileName = DefaultProfileName(project);
        CancellationTokenSource cancellation = new();
        projectModCountCalculationCancellation = cancellation;
        _ = RefreshSelectedProjectModCountsAsync(project, profileName, cancellation);
    }

    private async Task RefreshSelectedProjectModCountsAsync(
        ModProject project,
        string profileName,
        CancellationTokenSource cancellation)
    {
        try
        {
            IReadOnlyList<ModEntry> mods = await modCatalogService.GetInstalledModsAsync(
                project,
                profileName,
                cancellation.Token);

            if (projectModCountCalculationCancellation != cancellation ||
                cancellation.IsCancellationRequested ||
                !IsSameProject(SelectedProject, project))
            {
                return;
            }

            UpdateSelectedProjectModCounts(mods);
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception)
        {
            if (projectModCountCalculationCancellation == cancellation && !cancellation.IsCancellationRequested)
            {
                selectedProjectActiveModCount = null;
                selectedProjectDisabledModCount = null;
                selectedProjectModCountUnavailable = true;
                OnPropertyChanged(nameof(ActiveModCountText));
                OnPropertyChanged(nameof(DisabledModCountText));
                OnPropertyChanged(nameof(ProjectModBreakdownText));
            }
        }
        finally
        {
            if (projectModCountCalculationCancellation == cancellation)
            {
                projectModCountCalculationCancellation = null;
            }

            cancellation.Dispose();
        }
    }

    private void UpdateSelectedProjectModCounts(IEnumerable<ModEntry> mods)
    {
        (int active, int disabled) = CountInstalledModStates(mods);
        selectedProjectActiveModCount = active;
        selectedProjectDisabledModCount = disabled;
        selectedProjectModCountUnavailable = false;
        OnPropertyChanged(nameof(ActiveModCountText));
        OnPropertyChanged(nameof(DisabledModCountText));
        OnPropertyChanged(nameof(ProjectModBreakdownText));
    }

    internal static (int Active, int Disabled) CountInstalledModStates(IEnumerable<ModEntry> mods)
    {
        int active = 0;
        int disabled = 0;
        foreach (ModEntry mod in mods)
        {
            if (!mod.IsMod)
            {
                continue;
            }

            if (mod.IsEnabled)
            {
                active++;
            }
            else
            {
                disabled++;
            }
        }

        return (active, disabled);
    }

    internal static IReadOnlyList<ModEntry> ResolveVisibleModsForSearch(
        IReadOnlyList<ModEntry> mods,
        string searchText)
    {
        string[] searchTerms = SplitModSearchTerms(searchText);
        if (searchTerms.Length == 0)
        {
            List<ModEntry> collapsedAwareRows = new(mods.Count);
            foreach (ModEntry mod in mods)
            {
                if (!mod.IsHidden)
                {
                    collapsedAwareRows.Add(mod);
                }
            }

            return collapsedAwareRows;
        }

        List<ModEntry> visibleRows = new(mods.Count);
        for (int index = 0; index < mods.Count;)
        {
            ModEntry row = mods[index];
            if (!row.IsSeparator)
            {
                if (ModMatchesSearch(row, searchTerms))
                {
                    visibleRows.Add(row);
                }

                index++;
                continue;
            }

            ModEntry separator = row;
            List<ModEntry> sectionRows = new();
            List<ModEntry> matchingSectionRows = new();
            index++;
            while (index < mods.Count && !mods[index].IsSeparator)
            {
                ModEntry sectionRow = mods[index];
                sectionRows.Add(sectionRow);
                if (ModMatchesSearch(sectionRow, searchTerms))
                {
                    matchingSectionRows.Add(sectionRow);
                }

                index++;
            }

            bool separatorMatches = ModMatchesSearch(separator, searchTerms);
            if (separatorMatches)
            {
                visibleRows.Add(separator);
                visibleRows.AddRange(sectionRows);
            }
            else if (matchingSectionRows.Count > 0)
            {
                visibleRows.Add(separator);
                visibleRows.AddRange(matchingSectionRows);
            }
        }

        return visibleRows;
    }

    internal static IReadOnlyList<GameTemplateOption> ResolveVisibleTemplatesForSearch(
        IReadOnlyList<GameTemplateOption> templates,
        string searchText)
    {
        string[] searchTerms = SplitSearchTerms(searchText);
        if (searchTerms.Length == 0)
        {
            return templates.ToList();
        }

        List<GameTemplateOption> visibleTemplates = new(templates.Count);
        foreach (GameTemplateOption template in templates)
        {
            if (TemplateMatchesSearch(template, searchTerms))
            {
                visibleTemplates.Add(template);
            }
        }

        return visibleTemplates;
    }

    private static string[] SplitModSearchTerms(string searchText)
    {
        return SplitSearchTerms(searchText);
    }

    private static string[] SplitSearchTerms(string searchText)
    {
        return string.IsNullOrWhiteSpace(searchText)
            ? Array.Empty<string>()
            : searchText.Split(
                new[] { ' ', '\t', '\r', '\n' },
                StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
    }

    private static bool ModMatchesSearch(ModEntry mod, IReadOnlyList<string> searchTerms)
    {
        foreach (string searchTerm in searchTerms)
        {
            if (!ModContainsSearchTerm(mod, searchTerm))
            {
                return false;
            }
        }

        return true;
    }

    private static bool ModContainsSearchTerm(ModEntry mod, string searchTerm)
    {
        return ContainsSearchText(mod.DisplayName, searchTerm) ||
            ContainsSearchText(mod.Name, searchTerm) ||
            ContainsSearchText(mod.SeparatorTitle, searchTerm) ||
            ContainsSearchText(ModFolderName(mod), searchTerm) ||
            ContainsSearchText(mod.VersionText, searchTerm) ||
            ContainsSearchText(mod.LatestVersionText, searchTerm) ||
            ContainsSearchText(mod.UpdateStatus, searchTerm) ||
            ContainsSearchText(mod.ConflictStatus, searchTerm) ||
            ContainsSearchText(mod.ModUuid, searchTerm);
    }

    private static bool TemplateMatchesSearch(GameTemplateOption template, IReadOnlyList<string> searchTerms)
    {
        foreach (string searchTerm in searchTerms)
        {
            if (!TemplateContainsSearchTerm(template, searchTerm))
            {
                return false;
            }
        }

        return true;
    }

    private static bool TemplateContainsSearchTerm(GameTemplateOption template, string searchTerm)
    {
        return ContainsSearchText(template.DisplayName, searchTerm) ||
            ContainsSearchText(template.GameName, searchTerm) ||
            ContainsSearchText(template.Summary, searchTerm) ||
            ContainsSearchText(template.Id, searchTerm) ||
            ContainsSearchText(template.UiTemplateId, searchTerm) ||
            template.ArchiveExtensions.Any(extension => ContainsSearchText(extension, searchTerm)) ||
            template.RequiredFiles.Any(file => ContainsSearchText(file, searchTerm));
    }

    private static bool ContainsSearchText(string? text, string searchTerm)
    {
        return !string.IsNullOrWhiteSpace(text) &&
            text.IndexOf(searchTerm, StringComparison.OrdinalIgnoreCase) >= 0;
    }

    internal static string FormatProjectTimestamp(DateTimeOffset? timestamp, string fallback)
    {
        return timestamp.HasValue
            ? timestamp.Value.LocalDateTime.ToString("dd.MM.yyyy HH:mm")
            : fallback;
    }

    private static string DefaultProfileName(ModProject project)
    {
        string defaultProfile = project.Template?.DefaultProfile ?? string.Empty;
        return string.IsNullOrWhiteSpace(defaultProfile) ? "Default" : defaultProfile;
    }

    private async Task LoadModsFromProjectAsync(ModProject project)
    {
        try
        {
            string? selectedModId = SelectedMod is null ? null : ModListItemKey(SelectedMod);
            IReadOnlyList<ModEntry> mods = await modCatalogService.GetInstalledModsAsync(project, SelectedProfile);
            SyncMods(mods, selectedModId);
            NotifyModCountPropertiesChanged();
            RaiseModCommandStateChanged();
        }
        catch (Exception exception)
        {
            ActivityMessage = $"Не удалось обновить моды: {exception.Message}";
        }
    }

    private async void CheckModUpdates()
    {
        if (SelectedProject is null)
        {
            return;
        }

        ModProject project = SelectedProject;
        try
        {
            IsCheckingModUpdates = true;
            ActivityMessage = "Проверяю обновления Nexus...";
            string? selectedModId = SelectedMod is null ? null : ModListItemKey(SelectedMod);
            IReadOnlyList<ModEntry> mods = await modCatalogService.CheckModUpdatesAsync(project, SelectedProfile);
            SyncMods(mods, selectedModId);
            NotifyModCountPropertiesChanged();
            RaiseModCommandStateChanged();
            RefreshSelectedProjectSizeAfterMutation();
            ActivityMessage = "Проверка обновлений завершена.";
        }
        catch (Exception exception)
        {
            string errorMessage = exception.Message;
            try
            {
                string? selectedModId = SelectedMod is null ? null : ModListItemKey(SelectedMod);
                IReadOnlyList<ModEntry> mods = await modCatalogService.GetInstalledModsAsync(project, SelectedProfile);
                SyncMods(mods, selectedModId);
                NotifyModCountPropertiesChanged();
                RaiseModCommandStateChanged();
                ActivityMessage = $"Не удалось проверить обновления: {errorMessage}";
            }
            catch (Exception refreshException)
            {
                ActivityMessage =
                    $"Не удалось проверить обновления: {errorMessage}. Не удалось обновить моды: {refreshException.Message}";
            }
        }
        finally
        {
            IsCheckingModUpdates = false;
        }
    }

    private async Task LoadSelectedModRootFileTreeAsync()
    {
        SelectedModFileTree.Clear();
        OnPropertyChanged(nameof(HasSelectedModFileTree));

        if (SelectedProject is null || SelectedMod is not { IsMod: true })
        {
            return;
        }

        ModEntry selected = SelectedMod;
        try
        {
            IsLoadingModFiles = true;
            IReadOnlyList<ModFileTreeEntry> entries = await modCatalogService.GetModFileTreeAsync(
                SelectedProject,
                selected,
                string.Empty);
            if (SelectedMod is null || !IsSameModListItem(SelectedMod, ModListItemKey(selected)))
            {
                return;
            }

            foreach (ModFileTreeEntry entry in entries)
            {
                SelectedModFileTree.Add(new ModFileTreeNode(entry));
            }

            OnPropertyChanged(nameof(HasSelectedModFileTree));
        }
        catch (Exception exception)
        {
            ActivityMessage = $"Не удалось прочитать дерево файлов: {exception.Message}";
        }
        finally
        {
            IsLoadingModFiles = false;
        }
    }

    public async Task LoadModFileTreeNodeAsync(ModFileTreeNode node)
    {
        if (SelectedProject is null ||
            SelectedMod is null ||
            node.IsPlaceholder ||
            !node.IsDirectory ||
            node.IsLoaded)
        {
            return;
        }

        ModEntry selected = SelectedMod;
        try
        {
            IReadOnlyList<ModFileTreeEntry> entries = await modCatalogService.GetModFileTreeAsync(
                SelectedProject,
                selected,
                node.RelativePath);

            if (SelectedMod is null || !IsSameModListItem(SelectedMod, ModListItemKey(selected)))
            {
                return;
            }

            node.Children.Clear();
            foreach (ModFileTreeEntry entry in entries)
            {
                node.Children.Add(new ModFileTreeNode(entry));
            }

            node.IsLoaded = true;
        }
        catch (Exception exception)
        {
            ActivityMessage = $"Не удалось раскрыть папку мода: {exception.Message}";
        }
    }

    private async Task LoadPluginsFromProjectAsync(ModProject project)
    {
        if (!ProjectWorkspaceLoadService.ShouldRequestPluginSection(project))
        {
            SyncPlugins(Array.Empty<PluginEntry>(), null);
            OnPropertyChanged(nameof(HasPlugins));
            OnPropertyChanged(nameof(PluginCountText));
            return;
        }

        try
        {
            string? selectedPluginId = SelectedPlugin?.Id;
            IReadOnlyList<PluginEntry> plugins = await pluginCatalogService.GetPluginsAsync(project, SelectedProfile);
            SyncPlugins(plugins, selectedPluginId);
            OnPropertyChanged(nameof(HasPlugins));
            OnPropertyChanged(nameof(PluginCountText));
        }
        catch (Exception exception)
        {
            ActivityMessage = $"Не удалось обновить плагины: {exception.Message}";
            SyncPlugins(Array.Empty<PluginEntry>(), null);
            OnPropertyChanged(nameof(HasPlugins));
            OnPropertyChanged(nameof(PluginCountText));
        }
    }

    private async void RefreshProfileScopedLists()
    {
        if (!IsProjectWorkspaceOpen || SelectedProject is null)
        {
            return;
        }

        try
        {
            CancelWorkspaceLoad();
            string? selectedModId = SelectedMod is null ? null : ModListItemKey(SelectedMod);
            string? selectedPluginId = SelectedPlugin?.Id;
            ProjectWorkspaceProfileLoadResult result = await projectWorkspaceLoadService.LoadProfileScopedAsync(
                SelectedProject,
                SelectedProfile);
            ApplyProfileLoadResult(result, selectedModId, selectedPluginId);
            if (result.HasErrors)
            {
                ActivityMessage = BuildProfileLoadErrorMessage(result);
            }
        }
        catch (Exception exception)
        {
            ActivityMessage = $"Не удалось обновить профиль: {exception.Message}";
        }
    }

    private async Task RunDownloadOperationWithLiveRefreshAsync(
        ModProject project,
        Task operation,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        await LoadDownloadsFromProjectAsync(project);

        while (!operation.IsCompleted)
        {
            cancellationToken.ThrowIfCancellationRequested();
            Task delayTask = Task.Delay(TimeSpan.FromMilliseconds(500), cancellationToken);
            Task completedTask = await Task.WhenAny(operation, delayTask);
            if (completedTask == operation)
            {
                break;
            }

            cancellationToken.ThrowIfCancellationRequested();
            await LoadDownloadsFromProjectAsync(project);
        }

        cancellationToken.ThrowIfCancellationRequested();
        await operation;
        cancellationToken.ThrowIfCancellationRequested();
        await LoadDownloadsFromProjectAsync(project);
        if (IsSameProject(SelectedProject, project))
        {
            RefreshSelectedProjectSizeAfterMutation();
        }
    }

    private void SyncDownloads(IReadOnlyList<DownloadEntry> downloads, string? selectedDownloadId)
    {
        HashSet<string> selectedDownloadIds = SelectedDownloads()
            .Select(download => download.Id)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);

        for (int index = Downloads.Count - 1; index >= 0; --index)
        {
            if (!downloads.Any(download => IsSamePath(download.Id, Downloads[index].Id)))
            {
                Downloads.RemoveAt(index);
            }
        }

        for (int index = 0; index < downloads.Count; ++index)
        {
            DownloadEntry download = downloads[index];
            int existingIndex = IndexOfDownload(download.Id);
            if (existingIndex < 0)
            {
                Downloads.Insert(index, download);
                continue;
            }

            if (existingIndex != index)
            {
                Downloads.Move(existingIndex, index);
            }

            Downloads[index] = download;
        }

        RestoreDownloadSelection(selectedDownloadIds, selectedDownloadId);
    }

    private void SyncMods(IReadOnlyList<ModEntry> mods, string? selectedModId)
    {
        HashSet<string> selectedModIds = SelectedVisibleMods()
            .Select(ModListItemKey)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);

        OrderedCollectionSyncService.Sync(
            Mods,
            mods,
            ModListItemKey,
            AreEquivalentModEntries);

        modCollapseService.Apply(Mods);
        RebuildVisibleMods();

        RestoreModSelection(selectedModIds, selectedModId);
    }

    private void ApplyAllLoadedModsEnabledState(bool isEnabled)
    {
        ApplyAllModEnabledState(Mods, isEnabled);
        NotifyModCountPropertiesChanged();
        RaiseModCommandStateChanged();
    }

    internal static int ApplyAllModEnabledState(IEnumerable<ModEntry> mods, bool isEnabled)
    {
        int changed = 0;
        foreach (ModEntry mod in mods)
        {
            if (!mod.IsMod || mod.IsEnabled == isEnabled)
            {
                continue;
            }

            mod.IsEnabled = isEnabled;
            ++changed;
        }

        return changed;
    }

    private void SyncPlugins(IReadOnlyList<PluginEntry> plugins, string? selectedPluginId)
    {
        HashSet<string> selectedPluginIds = SelectedVisiblePlugins()
            .Select(PluginListItemKey)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);

        OrderedCollectionSyncService.Sync(
            Plugins,
            plugins,
            plugin => plugin.Id,
            AreEquivalentPluginEntries);

        pluginCollapseService.Apply(Plugins);

        RestorePluginSelection(selectedPluginIds, selectedPluginId);
    }

    private void ToggleModSeparator(ModEntry? separator)
    {
        if (separator is not { IsSeparator: true })
        {
            return;
        }

        modCollapseService.Toggle(separator);
        modCollapseService.Apply(Mods);
        RebuildVisibleMods();
        ClearHiddenModSelection();
        if (SelectedMod is { IsHidden: true })
        {
            SelectedMod = separator;
        }
    }

    private void TogglePluginSeparator(PluginEntry? separator)
    {
        if (separator is not { IsSeparator: true })
        {
            return;
        }

        pluginCollapseService.Toggle(separator);
        pluginCollapseService.Apply(Plugins);
        ClearHiddenPluginSelection();
    }

    private IReadOnlyList<ModEntry> SelectedVisibleMods()
    {
        return VisibleMods.Where(mod => mod.IsSelected).ToList();
    }

    private IReadOnlyList<PluginEntry> VisiblePlugins()
    {
        return Plugins.Where(plugin => !plugin.IsHidden).ToList();
    }

    private IReadOnlyList<PluginEntry> SelectedVisiblePlugins()
    {
        return Plugins.Where(plugin => !plugin.IsHidden && plugin.IsSelected).ToList();
    }

    private IReadOnlyList<DownloadEntry> SelectedDownloads()
    {
        return Downloads.Where(download => download.IsSelected).ToList();
    }

    private IReadOnlyList<ModEntry> ResolveModActionTargets(ModEntry? requestedMod)
    {
        IReadOnlyList<ModEntry> selected = SelectedVisibleMods();
        if (requestedMod is not null)
        {
            return requestedMod.IsSelected && selected.Count > 0
                ? selected
                : new[] { requestedMod };
        }

        return selected.Count > 0
            ? selected
            : SelectedMod is null
                ? Array.Empty<ModEntry>()
                : new[] { SelectedMod };
    }

    private IReadOnlyList<PluginEntry> ResolvePluginActionTargets(PluginEntry? requestedPlugin)
    {
        IReadOnlyList<PluginEntry> selected = SelectedVisiblePlugins();
        if (requestedPlugin is not null)
        {
            return requestedPlugin.IsSelected && selected.Count > 0
                ? selected
                : new[] { requestedPlugin };
        }

        return selected.Count > 0
            ? selected
            : SelectedPlugin is null
                ? Array.Empty<PluginEntry>()
                : new[] { SelectedPlugin };
    }

    private IReadOnlyList<DownloadEntry> ResolveDownloadActionTargets(DownloadEntry? requestedDownload)
    {
        IReadOnlyList<DownloadEntry> selected = SelectedDownloads();
        if (requestedDownload is not null)
        {
            return requestedDownload.IsSelected && selected.Count > 0
                ? selected
                : new[] { requestedDownload };
        }

        return selected.Count > 0
            ? selected
            : SelectedDownload is null
                ? Array.Empty<DownloadEntry>()
                : new[] { SelectedDownload };
    }

    private ModEntry? ResolveFocusedModAfterSelection(ModEntry? preferred)
    {
        if (preferred is { IsSelected: true })
        {
            return preferred;
        }

        if (SelectedMod is { IsSelected: true })
        {
            return SelectedMod;
        }

        return SelectedVisibleMods().FirstOrDefault();
    }

    private PluginEntry? ResolveFocusedPluginAfterSelection(PluginEntry? preferred)
    {
        if (preferred is { IsSelected: true })
        {
            return preferred;
        }

        if (SelectedPlugin is { IsSelected: true })
        {
            return SelectedPlugin;
        }

        return SelectedVisiblePlugins().FirstOrDefault();
    }

    private DownloadEntry? ResolveFocusedDownloadAfterSelection(DownloadEntry? preferred)
    {
        if (preferred is { IsSelected: true })
        {
            return preferred;
        }

        if (SelectedDownload is { IsSelected: true })
        {
            return SelectedDownload;
        }

        return SelectedDownloads().FirstOrDefault();
    }

    private void RestoreModSelection(IReadOnlySet<string> selectedModIds, string? preferredModId)
    {
        HashSet<string> visibleModIds = VisibleMods
            .Select(ModListItemKey)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);

        foreach (ModEntry mod in Mods)
        {
            string modId = ModListItemKey(mod);
            mod.IsSelected = visibleModIds.Contains(modId) && selectedModIds.Contains(modId);
        }

        ModEntry? focused = !string.IsNullOrWhiteSpace(preferredModId)
            ? VisibleMods.FirstOrDefault(mod => IsSameModListItem(mod, preferredModId))
            : null;
        focused = focused is { IsSelected: true }
            ? focused
            : SelectedVisibleMods().FirstOrDefault() ?? focused ?? VisibleMods.FirstOrDefault();

        if (focused is not null && !focused.IsSelected)
        {
            RangeSelectionService.Clear(Mods, static (mod, value) => mod.IsSelected = value);
            focused.IsSelected = true;
        }

        modSelectionAnchorId = focused is null ? null : ModListItemKey(focused);
        SelectedMod = focused;
    }

    private void RestorePluginSelection(IReadOnlySet<string> selectedPluginIds, string? preferredPluginId)
    {
        IReadOnlyList<PluginEntry> visiblePlugins = VisiblePlugins();
        foreach (PluginEntry plugin in Plugins)
        {
            plugin.IsSelected = !plugin.IsHidden && selectedPluginIds.Contains(PluginListItemKey(plugin));
        }

        PluginEntry? focused = !string.IsNullOrWhiteSpace(preferredPluginId)
            ? visiblePlugins.FirstOrDefault(plugin => IsSamePluginListItem(plugin, preferredPluginId))
            : null;
        focused = focused is { IsSelected: true }
            ? focused
            : SelectedVisiblePlugins().FirstOrDefault() ?? focused ?? visiblePlugins.FirstOrDefault();

        if (focused is not null && !focused.IsSelected)
        {
            RangeSelectionService.Clear(visiblePlugins, static (plugin, value) => plugin.IsSelected = value);
            focused.IsSelected = true;
        }

        pluginSelectionAnchorId = focused is null ? null : PluginListItemKey(focused);
        SelectedPlugin = focused;
    }

    private void RestoreDownloadSelection(IReadOnlySet<string> selectedDownloadIds, string? preferredDownloadId)
    {
        foreach (DownloadEntry download in Downloads)
        {
            download.IsSelected = selectedDownloadIds.Contains(download.Id);
        }

        DownloadEntry? focused = !string.IsNullOrWhiteSpace(preferredDownloadId)
            ? Downloads.FirstOrDefault(download => IsSamePath(download.Id, preferredDownloadId))
            : null;
        focused = focused is { IsSelected: true }
            ? focused
            : SelectedDownloads().FirstOrDefault() ?? focused ?? Downloads.FirstOrDefault();

        if (focused is not null && !focused.IsSelected)
        {
            RangeSelectionService.Clear(Downloads, static (download, value) => download.IsSelected = value);
            focused.IsSelected = true;
        }

        downloadSelectionAnchorId = focused?.Id;
        SelectedDownload = focused;
    }

    private void ClearHiddenPluginSelection()
    {
        foreach (PluginEntry plugin in Plugins.Where(plugin => plugin.IsHidden))
        {
            plugin.IsSelected = false;
        }

        if (SelectedPlugin is { IsHidden: true })
        {
            FocusPluginSelection(SelectedVisiblePlugins().FirstOrDefault());
            return;
        }

        RaisePluginCommandStateChanged();
    }

    private void ClearHiddenModSelection()
    {
        foreach (ModEntry mod in Mods.Where(mod => mod.IsHidden))
        {
            mod.IsSelected = false;
        }

        if (SelectedMod is { IsHidden: true })
        {
            FocusModSelection(SelectedVisibleMods().FirstOrDefault());
            return;
        }

        RaiseModCommandStateChanged();
    }

    private void ClearSelectionsExcept(SelectionScope activeScope)
    {
        if (activeScope != SelectionScope.Mods)
        {
            ClearModSelection();
        }

        if (activeScope != SelectionScope.Plugins)
        {
            ClearPluginSelection();
        }

        if (activeScope != SelectionScope.Downloads)
        {
            ClearDownloadSelection();
        }
    }

    private void ClearModSelection()
    {
        RangeSelectionService.Clear(Mods, static (item, value) => item.IsSelected = value);
        modSelectionAnchorId = null;
        selectedMod = null;
        OnPropertyChanged(nameof(SelectedMod));
        OnPropertyChanged(nameof(SelectedModTitle));
        OnPropertyChanged(nameof(SelectedModVersionText));
        OnPropertyChanged(nameof(SelectedModUpdateText));
        OnPropertyChanged(nameof(SelectedModConflictText));
        OnPropertyChanged(nameof(SelectedModFileCountText));
        SelectedModFileTree.Clear();
        OnPropertyChanged(nameof(HasSelectedModFileTree));
        RaiseModCommandStateChanged();
    }

    private void ClearPluginSelection()
    {
        RangeSelectionService.Clear(VisiblePlugins(), static (item, value) => item.IsSelected = value);
        pluginSelectionAnchorId = null;
        selectedPlugin = null;
        OnPropertyChanged(nameof(SelectedPlugin));
        OnPropertyChanged(nameof(SelectedPluginTitle));
        OnPropertyChanged(nameof(SelectedPluginStatusText));
        RaisePluginCommandStateChanged();
    }

    private void ClearDownloadSelection()
    {
        RangeSelectionService.Clear(Downloads, static (item, value) => item.IsSelected = value);
        downloadSelectionAnchorId = null;
        selectedDownload = null;
        OnPropertyChanged(nameof(SelectedDownload));
        OnPropertyChanged(nameof(SelectedDownloadTitle));
        OnPropertyChanged(nameof(SelectedDownloadStatusText));
        OnPropertyChanged(nameof(SelectedDownloadSourceText));
        RaiseDownloadCommandStateChanged();
    }

    private int IndexOfDownload(string id)
    {
        for (int index = 0; index < Downloads.Count; ++index)
        {
            if (IsSamePath(Downloads[index].Id, id))
            {
                return index;
            }
        }

        return -1;
    }

    private int IndexOfPlugin(string id)
    {
        for (int index = 0; index < Plugins.Count; ++index)
        {
            if (IsSamePlugin(Plugins[index].Id, id))
            {
                return index;
            }
        }

        return -1;
    }

    private int IndexOfPluginListItem(string id)
    {
        for (int index = 0; index < Plugins.Count; ++index)
        {
            if (IsSamePluginListItem(Plugins[index], id))
            {
                return index;
            }
        }

        return -1;
    }

    private int IndexOfMod(string id)
    {
        for (int index = 0; index < Mods.Count; ++index)
        {
            if (IsSameModListItem(Mods[index], id))
            {
                return index;
            }
        }

        return -1;
    }

    private int ResolveFullModInsertionIndex(int visibleInsertionIndex)
    {
        int clampedVisibleIndex = Math.Clamp(visibleInsertionIndex, 0, VisibleMods.Count);
        if (clampedVisibleIndex >= VisibleMods.Count)
        {
            return Mods.Count;
        }

        int anchorIndex = IndexOfMod(ModListItemKey(VisibleMods[clampedVisibleIndex]));
        return anchorIndex >= 0 ? anchorIndex : Mods.Count;
    }

    private void RebuildVisibleMods()
    {
        VisibleMods = new ObservableCollection<ModEntry>(
            ResolveVisibleModsForSearch(Mods, ModSearchText));
    }

    private void RebuildVisibleTemplates()
    {
        VisibleTemplates = new ObservableCollection<GameTemplateOption>(
            ResolveVisibleTemplatesForSearch(AvailableTemplates, GameSearchText));
    }

    private ModEntry? ResolveVisibleModSelection(string? selectedModId)
    {
        if (!string.IsNullOrWhiteSpace(selectedModId))
        {
            ModEntry? resolved = VisibleMods.FirstOrDefault(mod => IsSameModListItem(mod, selectedModId));
            if (resolved is not null)
            {
                return resolved;
            }
        }

        return VisibleMods.FirstOrDefault();
    }

    private int ModOrderMoveSpanLength(int sourceIndex)
    {
        if (sourceIndex < 0 || sourceIndex >= Mods.Count || !Mods[sourceIndex].IsSeparator)
        {
            return 1;
        }

        int end = sourceIndex + 1;
        while (end < Mods.Count && !Mods[end].IsSeparator)
        {
            ++end;
        }

        return end - sourceIndex;
    }

    private int PluginOrderMoveSpanLength(int sourceIndex)
    {
        if (sourceIndex < 0 || sourceIndex >= Plugins.Count || !Plugins[sourceIndex].IsSeparator)
        {
            return 1;
        }

        int end = sourceIndex + 1;
        while (end < Plugins.Count && !Plugins[end].IsSeparator)
        {
            ++end;
        }

        return end - sourceIndex;
    }

    private int PreviousModBlockStart(int sourceIndex)
    {
        if (sourceIndex <= 0 || sourceIndex >= Mods.Count || !Mods[sourceIndex].IsSeparator)
        {
            return Math.Max(0, sourceIndex - 1);
        }

        for (int index = sourceIndex - 1; index >= 0; --index)
        {
            if (Mods[index].IsSeparator)
            {
                return index;
            }
        }

        return sourceIndex - 1;
    }

    private int PreviousPluginBlockStart(int sourceIndex)
    {
        if (sourceIndex <= 0 || sourceIndex >= Plugins.Count || !Plugins[sourceIndex].IsSeparator)
        {
            return Math.Max(0, sourceIndex - 1);
        }

        for (int index = sourceIndex - 1; index >= 0; --index)
        {
            if (Plugins[index].IsSeparator)
            {
                return index;
            }
        }

        return sourceIndex - 1;
    }

    private int NextModBlockEnd(int sourceIndex)
    {
        int nextStart = sourceIndex + ModOrderMoveSpanLength(sourceIndex);
        if (nextStart >= Mods.Count)
        {
            return nextStart;
        }

        int end = nextStart + 1;
        while (end < Mods.Count && !Mods[end].IsSeparator)
        {
            ++end;
        }

        return end;
    }

    private int NextPluginBlockEnd(int sourceIndex)
    {
        int nextStart = sourceIndex + PluginOrderMoveSpanLength(sourceIndex);
        if (nextStart >= Plugins.Count)
        {
            return nextStart;
        }

        int end = nextStart + 1;
        while (end < Plugins.Count && !Plugins[end].IsSeparator)
        {
            ++end;
        }

        return end;
    }

    private int NormalizePluginMoveTargetIndex(
        PluginEntry plugin,
        int targetIndex,
        int sourceIndex,
        int spanLength)
    {
        if (Plugins.Count == 0)
        {
            return 0;
        }

        int clampedTarget = Math.Clamp(targetIndex, 0, Plugins.Count - 1);
        if (plugin.IsSeparator)
        {
            if (PluginMoveSpanContainsLockedPlugin(sourceIndex, spanLength))
            {
                return sourceIndex;
            }

            if (!PluginMoveSpanContainsPlugin(sourceIndex, spanLength))
            {
                return clampedTarget;
            }

            int minSeparatorTarget = Math.Min(FirstUnlockedPluginTargetIndex(), Plugins.Count - 1);
            return Math.Clamp(clampedTarget, minSeparatorTarget, Plugins.Count - 1);
        }

        int minTarget = FirstUnlockedPluginTargetIndex();
        int maxTarget = Plugins.Count - 1;
        if (minTarget > maxTarget)
        {
            minTarget = maxTarget;
        }

        return Math.Clamp(clampedTarget, minTarget, maxTarget);
    }

    private bool PluginMoveSpanContainsPlugin(int sourceIndex, int spanLength)
    {
        if (sourceIndex < 0 || spanLength <= 0)
        {
            return false;
        }

        int end = Math.Min(Plugins.Count, sourceIndex + spanLength);
        for (int index = sourceIndex; index < end; ++index)
        {
            if (Plugins[index].IsPlugin)
            {
                return true;
            }
        }

        return false;
    }

    private bool PluginMoveSpanContainsLockedPlugin(int sourceIndex, int spanLength)
    {
        if (sourceIndex < 0 || spanLength <= 0)
        {
            return false;
        }

        int end = Math.Min(Plugins.Count, sourceIndex + spanLength);
        for (int index = sourceIndex; index < end; ++index)
        {
            if (Plugins[index] is { IsPlugin: true, IsLocked: true })
            {
                return true;
            }
        }

        return false;
    }

    private int FirstUnlockedPluginTargetIndex()
    {
        int lastLockedPluginIndex = -1;
        for (int index = 0; index < Plugins.Count; ++index)
        {
            if (Plugins[index] is { IsPlugin: true, IsLocked: true })
            {
                lastLockedPluginIndex = index;
            }
        }

        return lastLockedPluginIndex + 1;
    }

    private static bool IsInsertionInsideMoveSpan(int sourceIndex, int spanLength, int insertionIndex)
    {
        return spanLength > 1 &&
            insertionIndex > sourceIndex &&
            insertionIndex <= sourceIndex + spanLength;
    }

    private static bool IsTargetInsideMoveSpan(int sourceIndex, int spanLength, int targetIndex)
    {
        return spanLength > 1 &&
            targetIndex >= sourceIndex &&
            targetIndex < sourceIndex + spanLength;
    }

    private static int MoveTargetToDestinationIndex(
        int sourceIndex,
        int spanLength,
        int targetIndex,
        int totalCount)
    {
        if (totalCount <= 0)
        {
            return 0;
        }

        if (spanLength <= 1)
        {
            return Math.Clamp(targetIndex, 0, totalCount - 1);
        }

        int maxDestination = Math.Max(0, totalCount - spanLength);
        int destination = targetIndex > sourceIndex
            ? targetIndex + 1 - spanLength
            : targetIndex;
        return Math.Clamp(destination, 0, maxDestination);
    }

    private static void MoveRange<T>(
        ObservableCollection<T> items,
        int sourceIndex,
        int spanLength,
        int destinationIndex)
    {
        if (spanLength <= 0 ||
            sourceIndex < 0 ||
            sourceIndex + spanLength > items.Count ||
            destinationIndex == sourceIndex)
        {
            return;
        }

        List<T> moving = items.Skip(sourceIndex).Take(spanLength).ToList();
        for (int index = 0; index < spanLength; ++index)
        {
            items.RemoveAt(sourceIndex);
        }

        int insertIndex = Math.Clamp(destinationIndex, 0, items.Count);
        for (int index = 0; index < moving.Count; ++index)
        {
            items.Insert(insertIndex + index, moving[index]);
        }
    }

    private void OpenProjectDirectory()
    {
        OpenDirectory(SelectedProject?.ProjectDirectory, "Папка сборки не найдена.");
    }

    private void OpenGameDirectory()
    {
        OpenDirectory(SelectedProject?.Paths.GameDirectory ?? SelectedProject?.GamePath, "Папка игры не найдена.");
    }

    private void OpenModsDirectory()
    {
        OpenProjectSubdirectory("mods", "Папка модов открыта.");
    }

    private void OpenProfilesDirectory()
    {
        OpenProjectSubdirectory("profiles", "Папка профилей открыта.");
    }

    private void OpenDownloadsDirectory()
    {
        OpenProjectSubdirectory("downloads", "Папка загрузок открыта.");
    }

    private void OpenModInExplorer(ModEntry? mod)
    {
        ModEntry? modToOpen = mod ?? SelectedMod;
        if (modToOpen is null || !CanOpenInstalledModInExplorer(modToOpen))
        {
            return;
        }

        SelectedMod = modToOpen;
        OpenDirectory(modToOpen.Id, "Папка мода не найдена.", "Папка мода открыта.");
    }

    private async void AddDownloadFile()
    {
        if (SelectedProject is null)
        {
            return;
        }

        string downloadsDirectory = SelectedProject.Paths.DownloadsDirectory;
        string? selectedPath = modArchivePickerService.PickArchive(downloadsDirectory);
        if (string.IsNullOrWhiteSpace(selectedPath))
        {
            return;
        }

        await ImportDownloadFilesAsync(new[] { selectedPath });
    }

    public async Task ImportDownloadFilesAsync(IReadOnlyList<string> sourcePaths)
    {
        if (SelectedProject is null || sourcePaths.Count == 0)
        {
            return;
        }

        List<string> paths = sourcePaths
            .Where(path => !string.IsNullOrWhiteSpace(path))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
        if (paths.Count == 0)
        {
            return;
        }

        using var operation = logService.BeginOperation(
            paths.Count == 1 ? "ImportDownloadFile" : "ImportDownloadFiles",
            $"project=\"{SelectedProject.Name}\", fileCount={paths.Count}");
        try
        {
            IsProcessingDownload = true;
            DownloadEntry? lastImportedDownload = null;
            List<string> failures = new();
            foreach (string path in paths)
            {
                try
                {
                    lastImportedDownload = await downloadCatalogService.ImportLocalFileAsync(SelectedProject, path);
                }
                catch (Exception exception)
                {
                    string fileName = Path.GetFileName(path);
                    failures.Add($"{fileName}: {exception.Message}");
                    logService.Warning("Downloads", $"Failed to import dropped download file. sourcePath=\"{path}\"", exception);
                }
            }

            if (lastImportedDownload is null)
            {
                throw new InvalidOperationException(failures.FirstOrDefault() ?? "Файлы не удалось добавить.");
            }

            await LoadDownloadsFromProjectAsync(SelectedProject);
            RefreshSelectedProjectSizeAfterMutation();
            SelectedDownload = Downloads.FirstOrDefault(download => IsSamePath(download.Id, lastImportedDownload.Id)) ?? SelectedDownload;
            int importedCount = paths.Count - failures.Count;
            ActivityMessage = failures.Count == 0
                ? importedCount == 1
                    ? "Файл добавлен в загрузки."
                    : $"Файлы добавлены в загрузки: {importedCount}"
                : $"Добавлено: {importedCount}. Не удалось: {failures.Count}. {failures[0]}";
            operation.Complete($"imported={importedCount}, failed={failures.Count}");
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ActivityMessage = $"Не удалось добавить загрузку: {exception.Message}";
        }
        finally
        {
            IsProcessingDownload = false;
        }
    }

    private async void CreateModSeparator()
    {
        if (SelectedProject is null)
        {
            return;
        }

        string? separatorName = modInstallDialogService.PickSeparatorName("Новый разделитель");
        if (string.IsNullOrWhiteSpace(separatorName))
        {
            return;
        }

        IReadOnlyList<string> selectedModIds = ResolveModActionTargets(null)
            .Where(target => target.IsMod)
            .OrderBy(target => IndexOfMod(ModListItemKey(target)))
            .Select(ModListItemKey)
            .ToList();
        int targetIndex = selectedModIds.Count > 0
            ? selectedModIds
                .Select(IndexOfMod)
                .Where(index => index >= 0)
                .DefaultIfEmpty(Mods.Count)
                .Min()
            : SelectedMod is null
                ? Mods.Count
                : Math.Min(Mods.Count, SelectedMod.Order + 1);

        using var operation = logService.BeginOperation(
            "CreateModSeparator",
            $"project=\"{SelectedProject.Name}\", profile=\"{SelectedProfile}\", title=\"{separatorName}\", targetIndex={targetIndex}, selectedMods={selectedModIds.Count}");
        try
        {
            IsProcessingDownload = true;
            IReadOnlyList<ModEntry> mods = await modCatalogService.CreateModSeparatorAsync(
                SelectedProject,
                SelectedProfile,
                separatorName,
                targetIndex);
            SyncMods(mods, null);
            ModEntry? createdSeparator = Mods.FirstOrDefault(mod =>
                mod.IsSeparator &&
                mod.Order == targetIndex &&
                string.Equals(mod.DisplayName, separatorName, StringComparison.OrdinalIgnoreCase)) ??
                Mods.ElementAtOrDefault(targetIndex);
            if (createdSeparator is not null && selectedModIds.Count > 0)
            {
                createdSeparator = await MoveModsUnderSeparatorAsync(createdSeparator, selectedModIds);
            }

            RangeSelectionService.Clear(VisibleMods, static (item, value) => item.IsSelected = value);
            if (createdSeparator is not null)
            {
                createdSeparator.IsSelected = true;
                modSelectionAnchorId = ModListItemKey(createdSeparator);
            }

            SelectedMod = createdSeparator ?? SelectedMod;
            NotifyModCountPropertiesChanged();
            RaiseModCommandStateChanged();
            RefreshSelectedProjectSizeAfterMutation();
            ActivityMessage = selectedModIds.Count > 0
                ? $"Разделитель создан, моды помещены: {selectedModIds.Count}"
                : $"Разделитель создан: {separatorName}";
            operation.Complete($"groupedMods={selectedModIds.Count}");
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ActivityMessage = $"Не удалось создать разделитель: {exception.Message}";
            await LoadModsFromProjectAsync(SelectedProject);
        }
        finally
        {
            IsProcessingDownload = false;
        }
    }

    private async void MoveSelectedModUp()
    {
        if (SelectedMod is null)
        {
            return;
        }

        int sourceIndex = IndexOfMod(ModListItemKey(SelectedMod));
        if (sourceIndex <= 0)
        {
            return;
        }

        int targetIndex = SelectedMod.IsSeparator
            ? PreviousModBlockStart(sourceIndex)
            : sourceIndex - 1;
        await MoveModToIndexAsync(SelectedMod, targetIndex);
    }

    private async void MoveSelectedModDown()
    {
        if (SelectedMod is null)
        {
            return;
        }

        int sourceIndex = IndexOfMod(ModListItemKey(SelectedMod));
        if (sourceIndex < 0 || sourceIndex >= Mods.Count - 1)
        {
            return;
        }

        int targetIndex = SelectedMod.IsSeparator
            ? NextModBlockEnd(sourceIndex) - 1
            : sourceIndex + 1;
        await MoveModToIndexAsync(SelectedMod, targetIndex);
    }

    public async Task MoveModToInsertionIndexAsync(ModEntry mod, int insertionIndex)
    {
        if (SelectedProject is null || !CanMoveModOrderItem(mod))
        {
            return;
        }

        int sourceIndex = IndexOfMod(ModListItemKey(mod));
        if (sourceIndex < 0)
        {
            return;
        }

        int spanLength = ModOrderMoveSpanLength(sourceIndex);
        int clampedInsertionIndex = Math.Clamp(ResolveFullModInsertionIndex(insertionIndex), 0, Mods.Count);
        if (IsInsertionInsideMoveSpan(sourceIndex, spanLength, clampedInsertionIndex))
        {
            return;
        }

        int targetIndex = clampedInsertionIndex > sourceIndex
            ? clampedInsertionIndex - 1
            : clampedInsertionIndex;

        await MoveModToIndexAsync(mod, targetIndex);
    }

    private async Task MoveModToIndexAsync(ModEntry mod, int targetIndex)
    {
        if (SelectedProject is null || !CanMoveModOrderItem(mod))
        {
            return;
        }

        int sourceIndex = IndexOfMod(ModListItemKey(mod));
        if (sourceIndex < 0)
        {
            return;
        }

        int spanLength = ModOrderMoveSpanLength(sourceIndex);
        int clampedTargetIndex = Math.Clamp(targetIndex, 0, Math.Max(0, Mods.Count - 1));
        if (IsTargetInsideMoveSpan(sourceIndex, spanLength, clampedTargetIndex))
        {
            return;
        }

        int destinationIndex = MoveTargetToDestinationIndex(
            sourceIndex,
            spanLength,
            clampedTargetIndex,
            Mods.Count);
        if (destinationIndex == sourceIndex)
        {
            return;
        }

        using var operation = logService.BeginOperation(
            "MoveModOrderItem",
            $"project=\"{SelectedProject.Name}\", profile=\"{SelectedProfile}\", item=\"{mod.DisplayName}\", sourceIndex={sourceIndex}, targetIndex={clampedTargetIndex}");
        try
        {
            IsProcessingDownload = true;
            MoveRange(Mods, sourceIndex, spanLength, destinationIndex);
            modCollapseService.Apply(Mods);
            RebuildVisibleMods();

            IReadOnlyList<ModEntry> mods = await modCatalogService.MoveModOrderItemAsync(
                SelectedProject,
                SelectedProfile,
                mod,
                clampedTargetIndex);
            SyncMods(mods, ModListItemKey(mod));
            NotifyModCountPropertiesChanged();
            RaiseModCommandStateChanged();
            RefreshSelectedProjectSizeAfterMutation();
            ActivityMessage = mod.IsSeparator
                ? $"Разделитель перемещён: {mod.DisplayName}"
                : $"Порядок модов обновлён: {mod.DisplayName}";
            operation.Complete();
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ActivityMessage = mod.IsSeparator
                ? $"Не удалось переместить разделитель: {exception.Message}"
                : $"Не удалось переместить мод: {exception.Message}";
            if (SelectedProject is not null)
            {
                await LoadModsFromProjectAsync(SelectedProject);
            }
        }
        finally
        {
            IsProcessingDownload = false;
        }
    }

    private async Task<ModEntry?> MoveModsUnderSeparatorAsync(ModEntry separator, IReadOnlyList<string> modIds)
    {
        if (SelectedProject is null || modIds.Count == 0)
        {
            return separator;
        }

        string separatorId = ModListItemKey(separator);
        for (int offset = 0; offset < modIds.Count; ++offset)
        {
            ModEntry? currentSeparator = Mods.FirstOrDefault(mod => IsSameModListItem(mod, separatorId));
            ModEntry? mod = Mods.FirstOrDefault(entry => IsSameModListItem(entry, modIds[offset]));
            if (currentSeparator is null || mod is not { IsMod: true })
            {
                continue;
            }

            int separatorIndex = IndexOfMod(ModListItemKey(currentSeparator));
            int targetIndex = Math.Min(Mods.Count - 1, separatorIndex + 1 + offset);
            IReadOnlyList<ModEntry> mods = await modCatalogService.MoveModOrderItemAsync(
                SelectedProject,
                SelectedProfile,
                mod,
                targetIndex);
            SyncMods(mods, separatorId);
        }

        return Mods.FirstOrDefault(mod => IsSameModListItem(mod, separatorId));
    }

    private async void MoveSelectedPluginUp()
    {
        if (SelectedPlugin is null)
        {
            return;
        }

        int sourceIndex = IndexOfPlugin(SelectedPlugin.Id);
        if (sourceIndex < 0)
        {
            return;
        }

        int targetIndex = SelectedPlugin.IsSeparator
            ? PreviousPluginBlockStart(sourceIndex)
            : sourceIndex - 1;
        await MovePluginToIndexAsync(SelectedPlugin, targetIndex);
    }

    private async void CreatePluginSeparator()
    {
        if (SelectedProject is null || !CanShowLoadOrderPanel)
        {
            return;
        }

        string? separatorName = modInstallDialogService.PickSeparatorName("Новый разделитель");
        if (string.IsNullOrWhiteSpace(separatorName))
        {
            return;
        }

        IReadOnlyList<string> selectedPluginIds = ResolvePluginActionTargets(null)
            .Where(target => target is { IsPlugin: true, CanMove: true })
            .OrderBy(target => IndexOfPluginListItem(PluginListItemKey(target)))
            .Select(PluginListItemKey)
            .ToList();
        int targetIndex = selectedPluginIds.Count > 0
            ? selectedPluginIds
                .Select(IndexOfPluginListItem)
                .Where(index => index >= 0)
                .DefaultIfEmpty(Plugins.Count)
                .Min()
            : SelectedPlugin is null
                ? Plugins.Count
                : Math.Min(Plugins.Count, SelectedPlugin.Order + 1);

        using var operation = logService.BeginOperation(
            "CreatePluginSeparator",
            $"project=\"{SelectedProject.Name}\", profile=\"{SelectedProfile}\", title=\"{separatorName}\", targetIndex={targetIndex}, selectedPlugins={selectedPluginIds.Count}");
        try
        {
            IsProcessingPlugins = true;
            IReadOnlyList<PluginEntry> plugins = await pluginCatalogService.CreatePluginSeparatorAsync(
                SelectedProject,
                SelectedProfile,
                separatorName,
                targetIndex);
            SyncPlugins(plugins, null);
            PluginEntry? createdSeparator = Plugins.FirstOrDefault(plugin =>
                plugin.IsSeparator &&
                plugin.Order >= Math.Min(targetIndex, Math.Max(0, Plugins.Count - 1)) &&
                string.Equals(plugin.DisplayName, separatorName, StringComparison.OrdinalIgnoreCase)) ??
                Plugins.ElementAtOrDefault(Math.Min(targetIndex, Math.Max(0, Plugins.Count - 1)));
            if (createdSeparator is not null && selectedPluginIds.Count > 0)
            {
                createdSeparator = await MovePluginsUnderSeparatorAsync(createdSeparator, selectedPluginIds);
            }

            RangeSelectionService.Clear(VisiblePlugins(), static (item, value) => item.IsSelected = value);
            if (createdSeparator is not null)
            {
                createdSeparator.IsSelected = true;
                pluginSelectionAnchorId = PluginListItemKey(createdSeparator);
            }

            SelectedPlugin = createdSeparator ?? SelectedPlugin;
            OnPropertyChanged(nameof(HasPlugins));
            OnPropertyChanged(nameof(PluginCountText));
            RaisePluginCommandStateChanged();
            RefreshSelectedProjectSizeAfterMutation();
            ActivityMessage = selectedPluginIds.Count > 0
                ? $"Разделитель плагинов создан, плагины помещены: {selectedPluginIds.Count}"
                : $"Разделитель плагинов создан: {separatorName}";
            operation.Complete($"groupedPlugins={selectedPluginIds.Count}");
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ActivityMessage = $"Не удалось создать разделитель плагинов: {exception.Message}";
            await LoadPluginsFromProjectAsync(SelectedProject);
        }
        finally
        {
            IsProcessingPlugins = false;
        }
    }

    private async void MoveSelectedPluginDown()
    {
        if (SelectedPlugin is null)
        {
            return;
        }

        int sourceIndex = IndexOfPlugin(SelectedPlugin.Id);
        if (sourceIndex < 0)
        {
            return;
        }

        int targetIndex = SelectedPlugin.IsSeparator
            ? NextPluginBlockEnd(sourceIndex) - 1
            : sourceIndex + 1;
        await MovePluginToIndexAsync(SelectedPlugin, targetIndex);
    }

    public async Task MovePluginToInsertionIndexAsync(PluginEntry plugin, int insertionIndex)
    {
        if (SelectedProject is null || !CanMovePluginOrderItem(plugin))
        {
            return;
        }

        int sourceIndex = IndexOfPlugin(plugin.Id);
        if (sourceIndex < 0)
        {
            return;
        }

        int spanLength = PluginOrderMoveSpanLength(sourceIndex);
        int clampedInsertionIndex = Math.Clamp(insertionIndex, 0, Plugins.Count);
        if (IsInsertionInsideMoveSpan(sourceIndex, spanLength, clampedInsertionIndex))
        {
            return;
        }

        int targetIndex = clampedInsertionIndex > sourceIndex
            ? clampedInsertionIndex - 1
            : clampedInsertionIndex;

        await MovePluginToIndexAsync(plugin, targetIndex);
    }

    public async Task MovePluginToIndexAsync(PluginEntry plugin, int targetIndex)
    {
        if (SelectedProject is null || !CanMovePluginOrderItem(plugin))
        {
            return;
        }

        int sourceIndex = IndexOfPlugin(plugin.Id);
        if (sourceIndex < 0)
        {
            return;
        }

        int spanLength = PluginOrderMoveSpanLength(sourceIndex);
        int clampedTargetIndex = NormalizePluginMoveTargetIndex(
            plugin,
            targetIndex,
            sourceIndex,
            spanLength);
        if (IsTargetInsideMoveSpan(sourceIndex, spanLength, clampedTargetIndex))
        {
            return;
        }

        int destinationIndex = MoveTargetToDestinationIndex(
            sourceIndex,
            spanLength,
            clampedTargetIndex,
            Plugins.Count);
        if (destinationIndex == sourceIndex)
        {
            return;
        }

        using var operation = logService.BeginOperation(
            "MovePlugin",
            $"project=\"{SelectedProject.Name}\", profile=\"{SelectedProfile}\", item=\"{plugin.DisplayName}\", sourceIndex={sourceIndex}, targetIndex={clampedTargetIndex}");
        try
        {
            IsProcessingPlugins = true;
            MoveRange(Plugins, sourceIndex, spanLength, destinationIndex);
            pluginCollapseService.Apply(Plugins);

            IReadOnlyList<PluginEntry> plugins = await pluginCatalogService.MovePluginAsync(
                SelectedProject,
                SelectedProfile,
                plugin,
                clampedTargetIndex);
            SyncPlugins(plugins, plugin.Id);
            OnPropertyChanged(nameof(HasPlugins));
            OnPropertyChanged(nameof(PluginCountText));
            RaisePluginCommandStateChanged();
            RefreshSelectedProjectSizeAfterMutation();
            ActivityMessage = plugin.IsSeparator
                ? $"Разделитель плагинов перемещён: {plugin.DisplayName}"
                : $"Порядок плагинов обновлён: {plugin.Name}";
            operation.Complete();
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ActivityMessage = plugin.IsSeparator
                ? $"Не удалось переместить разделитель плагинов: {exception.Message}"
                : $"Не удалось переместить плагин: {exception.Message}";
            if (SelectedProject is not null)
            {
                await LoadPluginsFromProjectAsync(SelectedProject);
            }
        }
        finally
        {
            IsProcessingPlugins = false;
        }
    }

    private async Task<PluginEntry?> MovePluginsUnderSeparatorAsync(PluginEntry separator, IReadOnlyList<string> pluginIds)
    {
        if (SelectedProject is null || pluginIds.Count == 0 || !CanShowLoadOrderPanel)
        {
            return separator;
        }

        string separatorId = PluginListItemKey(separator);
        for (int offset = 0; offset < pluginIds.Count; ++offset)
        {
            PluginEntry? currentSeparator = Plugins.FirstOrDefault(plugin => IsSamePluginListItem(plugin, separatorId));
            PluginEntry? plugin = Plugins.FirstOrDefault(entry => IsSamePluginListItem(entry, pluginIds[offset]));
            if (currentSeparator is null || plugin is not { IsPlugin: true, CanMove: true })
            {
                continue;
            }

            int separatorIndex = IndexOfPluginListItem(separatorId);
            int targetIndex = Math.Min(Plugins.Count - 1, separatorIndex + 1 + offset);
            IReadOnlyList<PluginEntry> plugins = await pluginCatalogService.MovePluginAsync(
                SelectedProject,
                SelectedProfile,
                plugin,
                targetIndex);
            SyncPlugins(plugins, separatorId);
        }

        return Plugins.FirstOrDefault(plugin => IsSamePluginListItem(plugin, separatorId));
    }

    private async void DeleteSelectedPlugin(PluginEntry? plugin)
    {
        if (SelectedProject is null || !CanShowLoadOrderPanel)
        {
            return;
        }

        IReadOnlyList<PluginEntry> separatorsToDelete = ResolvePluginActionTargets(plugin)
            .Where(target => target.IsSeparator)
            .OrderByDescending(target => IndexOfPluginListItem(PluginListItemKey(target)))
            .ToList();
        if (separatorsToDelete.Count == 0)
        {
            return;
        }

        int fallbackIndex = separatorsToDelete
            .Select(separator => IndexOfPluginListItem(PluginListItemKey(separator)))
            .Where(index => index >= 0)
            .DefaultIfEmpty(0)
            .Min();
        using var operation = logService.BeginOperation(
            "DeletePluginSeparators",
            $"project=\"{SelectedProject.Name}\", profile=\"{SelectedProfile}\", count={separatorsToDelete.Count}");
        try
        {
            IsProcessingPlugins = true;
            IReadOnlyList<PluginEntry>? plugins = null;
            foreach (PluginEntry separator in separatorsToDelete)
            {
                plugins = await pluginCatalogService.DeletePluginSeparatorAsync(
                    SelectedProject,
                    SelectedProfile,
                    separator);
            }

            if (plugins is not null)
            {
                SyncPlugins(plugins, null);
            }
            else
            {
                await LoadPluginsFromProjectAsync(SelectedProject);
            }

            SelectedPlugin = Plugins.ElementAtOrDefault(Math.Min(fallbackIndex, Math.Max(0, Plugins.Count - 1)));
            if (SelectedPlugin is not null)
            {
                RangeSelectionService.Clear(VisiblePlugins(), static (item, value) => item.IsSelected = value);
                SelectedPlugin.IsSelected = true;
                pluginSelectionAnchorId = PluginListItemKey(SelectedPlugin);
            }

            OnPropertyChanged(nameof(HasPlugins));
            OnPropertyChanged(nameof(PluginCountText));
            RaisePluginCommandStateChanged();
            RefreshSelectedProjectSizeAfterMutation();
            ActivityMessage = separatorsToDelete.Count == 1
                ? $"Разделитель плагинов удалён: {separatorsToDelete[0].DisplayName}"
                : $"Разделители плагинов удалены: {separatorsToDelete.Count}";
            operation.Complete($"deleted={separatorsToDelete.Count}");
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ActivityMessage = $"Не удалось удалить разделители плагинов: {exception.Message}";
            await LoadPluginsFromProjectAsync(SelectedProject);
        }
        finally
        {
            IsProcessingPlugins = false;
        }
    }

    private async void TogglePluginEnabled(PluginEntry? plugin)
    {
        if (SelectedProject is null || plugin is not { IsPlugin: true } || !CanShowPluginsPanel)
        {
            return;
        }

        using var operation = logService.BeginOperation(
            "SetPluginEnabled",
            $"project=\"{SelectedProject.Name}\", profile=\"{SelectedProfile}\", plugin=\"{plugin.Name}\", enabled={plugin.IsEnabled}");
        try
        {
            IsProcessingPlugins = true;
            IReadOnlyList<PluginEntry> plugins = await pluginCatalogService.SetPluginEnabledAsync(
                SelectedProject,
                SelectedProfile,
                plugin,
                plugin.IsEnabled);
            SyncPlugins(plugins, plugin.Id);
            OnPropertyChanged(nameof(HasPlugins));
            OnPropertyChanged(nameof(PluginCountText));
            RefreshSelectedProjectSizeAfterMutation();
            ActivityMessage = plugin.IsEnabled
                ? $"Плагин включён: {plugin.Name}"
                : $"Плагин отключён: {plugin.Name}";
            operation.Complete();
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ActivityMessage = $"Не удалось изменить плагин: {exception.Message}";
            if (SelectedProject is not null)
            {
                await LoadPluginsFromProjectAsync(SelectedProject);
            }
        }
        finally
        {
            IsProcessingPlugins = false;
        }
    }

    private async void SetSelectedPluginsEnabled(PluginEntry? plugin, bool isEnabled)
    {
        if (SelectedProject is null || !CanShowPluginsPanel)
        {
            return;
        }

        IReadOnlyList<PluginEntry> pluginsToUpdate = ResolvePluginActionTargets(plugin)
            .Where(target => target is { CanToggle: true } && target.IsEnabled != isEnabled)
            .OrderBy(target => IndexOfPluginListItem(PluginListItemKey(target)))
            .ToList();
        if (pluginsToUpdate.Count == 0)
        {
            return;
        }

        FocusPluginSelection(pluginsToUpdate[0]);

        using var operation = logService.BeginOperation(
            "SetPluginsEnabled",
            $"project=\"{SelectedProject.Name}\", profile=\"{SelectedProfile}\", count={pluginsToUpdate.Count}, enabled={isEnabled}");
        try
        {
            IsProcessingPlugins = true;
            IReadOnlyList<PluginEntry>? refreshedPlugins = null;
            foreach (PluginEntry pluginToUpdate in pluginsToUpdate)
            {
                refreshedPlugins = await pluginCatalogService.SetPluginEnabledAsync(
                    SelectedProject,
                    SelectedProfile,
                    pluginToUpdate,
                    isEnabled);
            }

            if (refreshedPlugins is not null)
            {
                SyncPlugins(refreshedPlugins, pluginsToUpdate[0].Id);
            }
            else
            {
                await LoadPluginsFromProjectAsync(SelectedProject);
            }

            OnPropertyChanged(nameof(HasPlugins));
            OnPropertyChanged(nameof(PluginCountText));
            RefreshSelectedProjectSizeAfterMutation();
            ActivityMessage = pluginsToUpdate.Count == 1
                ? isEnabled
                    ? $"Плагин включён: {pluginsToUpdate[0].Name}"
                    : $"Плагин отключён: {pluginsToUpdate[0].Name}"
                : isEnabled
                    ? $"Плагины включены: {pluginsToUpdate.Count}"
                    : $"Плагины отключены: {pluginsToUpdate.Count}";
            operation.Complete($"updated={pluginsToUpdate.Count}");
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ActivityMessage = isEnabled
                ? $"Не удалось включить плагины: {exception.Message}"
                : $"Не удалось отключить плагины: {exception.Message}";
            if (SelectedProject is not null)
            {
                await LoadPluginsFromProjectAsync(SelectedProject);
            }
        }
        finally
        {
            IsProcessingPlugins = false;
        }
    }

    private bool CanSetSelectedModEnabled(ModEntry? mod, bool isEnabled)
    {
        IReadOnlyList<ModEntry> targets = ResolveModActionTargets(mod);
        return IsProjectWorkspaceOpen &&
            HasSelectedProject &&
            targets.Any(target => target.IsMod && target.IsEnabled != isEnabled) &&
            !IsProcessingDownload &&
            !IsWorkspaceOperationBlocked;
    }

    private bool CanSetAllModsEnabled(bool isEnabled)
    {
        return IsProjectWorkspaceOpen &&
            HasSelectedProject &&
            Mods.Any(mod => mod.IsMod && mod.IsEnabled != isEnabled) &&
            !IsProcessingDownload &&
            !IsWorkspaceOperationBlocked;
    }

    private bool CanOpenSelectedModInExplorer(ModEntry? mod)
    {
        IReadOnlyList<ModEntry> targets = ResolveModActionTargets(mod);
        return IsProjectWorkspaceOpen &&
            HasSelectedProject &&
            targets.Count == 1 &&
            CanOpenInstalledModInExplorer(targets[0]) &&
            !IsProcessingDownload &&
            !IsWorkspaceOperationBlocked;
    }

    private bool CanDeleteSelectedMod(ModEntry? mod)
    {
        return IsProjectWorkspaceOpen &&
            HasSelectedProject &&
            ResolveModActionTargets(mod).Count > 0 &&
            !IsProcessingDownload &&
            !IsWorkspaceOperationBlocked;
    }

    private bool CanDeleteSelectedPlugin(PluginEntry? plugin)
    {
        return CanShowLoadOrderPanel &&
            IsProjectWorkspaceOpen &&
            HasSelectedProject &&
            ResolvePluginActionTargets(plugin).Any(target => target.IsSeparator) &&
            !IsProcessingPlugins &&
            !IsWorkspaceOperationBlocked;
    }

    private bool CanSetSelectedPluginsEnabled(PluginEntry? plugin, bool isEnabled)
    {
        return CanShowPluginsPanel &&
            IsProjectWorkspaceOpen &&
            HasSelectedProject &&
            ResolvePluginActionTargets(plugin).Any(target => target is { CanToggle: true } && target.IsEnabled != isEnabled) &&
            !IsProcessingPlugins &&
            !IsWorkspaceOperationBlocked;
    }

    private bool CanInstallSelectedDownload(DownloadEntry? download)
    {
        IReadOnlyList<DownloadEntry> targets = ResolveDownloadActionTargets(download);
        return IsProjectWorkspaceOpen &&
            HasSelectedProject &&
            targets.Count == 1 &&
            targets[0].CanInstall &&
            !IsProcessingDownload &&
            !IsWorkspaceOperationBlocked;
    }

    private bool CanDeleteSelectedDownload(DownloadEntry? download)
    {
        return IsProjectWorkspaceOpen &&
            HasSelectedProject &&
            ResolveDownloadActionTargets(download).Any(target => target.CanDelete) &&
            !IsProcessingDownload &&
            !IsWorkspaceOperationBlocked;
    }

    private bool CanOpenSelectedDownloadInExplorer(DownloadEntry? download)
    {
        IReadOnlyList<DownloadEntry> targets = ResolveDownloadActionTargets(download);
        return IsProjectWorkspaceOpen &&
            HasSelectedProject &&
            targets.Count == 1 &&
            !string.IsNullOrWhiteSpace(targets[0].LocalPath) &&
            !IsWorkspaceOperationBlocked;
    }

    private async void SetSelectedModEnabled(ModEntry? mod, bool isEnabled)
    {
        if (SelectedProject is null)
        {
            return;
        }

        IReadOnlyList<ModEntry> modsToUpdate = ResolveModActionTargets(mod)
            .Where(target => target.IsMod && target.IsEnabled != isEnabled)
            .OrderBy(target => IndexOfMod(ModListItemKey(target)))
            .ToList();
        if (modsToUpdate.Count == 0)
        {
            return;
        }

        FocusModSelection(modsToUpdate[0]);

        using var operation = logService.BeginOperation(
            "SetInstalledModsEnabled",
            $"project=\"{SelectedProject.Name}\", profile=\"{SelectedProfile}\", count={modsToUpdate.Count}, enabled={isEnabled}");
        try
        {
            IsProcessingDownload = true;
            foreach (ModEntry modToUpdate in modsToUpdate)
            {
                await modCatalogService.SetInstalledModEnabledAsync(SelectedProject, modToUpdate, isEnabled);
            }

            await LoadModsFromProjectAsync(SelectedProject);
            await LoadPluginsFromProjectAsync(SelectedProject);
            RefreshSelectedProjectSizeAfterMutation();
            ActivityMessage = modsToUpdate.Count == 1
                ? isEnabled
                    ? $"Мод включён: {modsToUpdate[0].Name}"
                    : $"Мод выключен: {modsToUpdate[0].Name}"
                : isEnabled
                    ? $"Моды включены: {modsToUpdate.Count}"
                    : $"Моды выключены: {modsToUpdate.Count}";
            operation.Complete($"updated={modsToUpdate.Count}");
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ActivityMessage = isEnabled
                ? $"Не удалось включить моды: {exception.Message}"
                : $"Не удалось выключить моды: {exception.Message}";
        }
        finally
        {
            IsProcessingDownload = false;
        }
    }

    private async void SetAllModsEnabled(bool isEnabled)
    {
        if (SelectedProject is null)
        {
            return;
        }

        using var operation = logService.BeginOperation(
            "SetAllInstalledModsEnabled",
            $"project=\"{SelectedProject.Name}\", profile=\"{SelectedProfile}\", enabled={isEnabled}");
        try
        {
            IsProcessingDownload = true;
            await modCatalogService.SetAllInstalledModsEnabledAsync(SelectedProject, isEnabled);
            ApplyAllLoadedModsEnabledState(isEnabled);
            await LoadPluginsFromProjectAsync(SelectedProject);
            ActivityMessage = isEnabled
                ? "Все моды включены."
                : "Все моды выключены.";
            operation.Complete();
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ActivityMessage = isEnabled
                ? $"Не удалось включить все моды: {exception.Message}"
                : $"Не удалось выключить все моды: {exception.Message}";
        }
        finally
        {
            IsProcessingDownload = false;
        }
    }

    private async void DeleteSelectedMod(ModEntry? mod)
    {
        if (SelectedProject is null)
        {
            return;
        }

        IReadOnlyList<ModEntry> itemsToDelete = ResolveModActionTargets(mod)
            .OrderByDescending(target => IndexOfMod(ModListItemKey(target)))
            .ToList();
        if (itemsToDelete.Count == 0)
        {
            return;
        }

        int modCount = itemsToDelete.Count(item => item.IsMod);
        int separatorCount = itemsToDelete.Count(item => item.IsSeparator);
        using var operation = logService.BeginOperation(
            "DeleteSelectedModItems",
            $"project=\"{SelectedProject.Name}\", profile=\"{SelectedProfile}\", count={itemsToDelete.Count}, mods={modCount}, separators={separatorCount}");
        try
        {
            IsProcessingDownload = true;
            ModOperationProcess.Start(
                itemsToDelete.Count == 1
                    ? itemsToDelete[0].IsSeparator ? "Удаление разделителя" : "Удаление мода"
                    : "Удаление выбранных модов",
                "Подготовка удаления",
                itemsToDelete.Count == 1
                    ? itemsToDelete[0].DisplayName
                    : $"Элементов: {itemsToDelete.Count}");

            for (int index = 0; index < itemsToDelete.Count; ++index)
            {
                ModEntry item = itemsToDelete[index];
                int progress = 18 + (int)Math.Round((index + 1) * 52.0 / itemsToDelete.Count);
                if (item.IsSeparator)
                {
                    ModOperationProcess.ApplyProgress("Удаляю разделитель", item.DisplayName, progress);
                    await modCatalogService.DeleteModSeparatorAsync(
                        SelectedProject,
                        SelectedProfile,
                        item);
                }
                else
                {
                    ModOperationProcess.ApplyProgress("Удаляю файлы мода", item.Name, progress);
                    await modCatalogService.DeleteInstalledModAsync(SelectedProject, item);
                }
            }

            ModOperationProcess.ApplyProgress("Обновляю список модов", "Синхронизирую профиль", 78);
            await LoadModsFromProjectAsync(SelectedProject);
            ModOperationProcess.ApplyProgress("Обновляю плагины", "Проверяю load order", 88);
            await LoadPluginsFromProjectAsync(SelectedProject);
            NotifyModCountPropertiesChanged();
            RefreshSelectedProjectSizeAfterMutation();
            ActivityMessage = itemsToDelete.Count == 1
                ? itemsToDelete[0].IsSeparator
                    ? $"Разделитель удалён: {itemsToDelete[0].DisplayName}"
                    : $"Мод удалён: {itemsToDelete[0].Name}"
                : $"Выбранные элементы удалены: {itemsToDelete.Count}";
            await CompleteModOperationSplashAsync(
                itemsToDelete.Count == 1
                    ? itemsToDelete[0].IsSeparator ? "Разделитель удалён" : "Мод удалён"
                    : "Выбранное удалено",
                itemsToDelete.Count == 1 ? itemsToDelete[0].DisplayName : $"Элементов: {itemsToDelete.Count}");
            operation.Complete($"deleted={itemsToDelete.Count}, mods={modCount}, separators={separatorCount}");
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ModOperationProcess.Fail(exception.Message);
            ActivityMessage = $"Не удалось удалить выбранные элементы: {exception.Message}";
        }
        finally
        {
            IsProcessingDownload = false;
        }
    }

    private async void InstallSelectedDownload(DownloadEntry? download)
    {
        await InstallDownloadAsync(download, null);
    }

    public async Task InstallDownloadAtInsertionIndexAsync(DownloadEntry download, int insertionIndex)
    {
        await InstallDownloadAsync(download, insertionIndex);
    }

    private async Task InstallDownloadAsync(DownloadEntry? download, int? insertionIndex)
    {
        if (SelectedProject is null)
        {
            return;
        }

        DownloadEntry? downloadToInstall = download ?? SelectedDownload;
        if (downloadToInstall is null)
        {
            return;
        }

        SelectedDownload = downloadToInstall;

        using var operation = logService.BeginOperation(
            insertionIndex.HasValue ? "InstallDownloadAtModIndex" : "InstallDownload",
            $"project=\"{SelectedProject.Name}\", downloadPath=\"{downloadToInstall.LocalPath}\", insertionIndex={insertionIndex?.ToString() ?? "append"}");
        try
        {
            IsProcessingDownload = true;
            ModOperationProcess.Start(
                "Проверка архива",
                "Подготовка установки",
                $"Готовлю архив: {downloadToInstall.Name}");
            IReadOnlyList<string>? fomodSelection = null;
            bool isFomodInstall = false;
            string modName;
            ContentLayoutPreview? layoutPreview = null;
            ExistingModInstallMode existingModMode = ExistingModInstallMode.FailIfExists;
            ModOperationProcess.ApplyProgress("Проверяю FOMOD", downloadToInstall.Name, 10);
            FomodInstallerInfo fomodInstaller = await downloadCatalogService.AnalyzeFomodDownloadAsync(
                SelectedProject,
                downloadToInstall);
            if (fomodInstaller.IsFomod)
            {
                isFomodInstall = true;
                modName = ResolveFomodInstallName(fomodInstaller, downloadToInstall);
                ModOperationProcess.ApplyProgress(
                    "Открываю FOMOD",
                    string.IsNullOrWhiteSpace(fomodInstaller.ModuleName) ? downloadToInstall.Name : fomodInstaller.ModuleName,
                    16);
                fomodSelection = modInstallDialogService.PickFomodSelections(fomodInstaller);
                if (fomodSelection is null)
                {
                    ModOperationProcess.Reset();
                    ActivityMessage = $"Установка FOMOD отменена: {modName}";
                    operation.Complete($"cancelled=true, fomod=true, modName=\"{modName}\"");
                    return;
                }
            }
            else
            {
                ModOperationProcess.ApplyProgress("Анализирую размещение", downloadToInstall.Name, 14);
                layoutPreview = await downloadCatalogService.AnalyzeDownloadContentLayoutAsync(
                    SelectedProject,
                    downloadToInstall,
                    ExistingModInstallMode.FailIfExists);
                ModOperationProcess.Reset();
                string? selectedModName = modInstallDialogService.PickModName(downloadToInstall.Name, layoutPreview);
                if (string.IsNullOrWhiteSpace(selectedModName))
                {
                    ActivityMessage = layoutPreview is { CanInstall: false }
                        ? ContentLayoutPreviewBlockerText(layoutPreview)
                        : ActivityMessage;
                    operation.Complete($"cancelled=true, fomod=false, layoutBlocked={layoutPreview is { CanInstall: false }}");
                    return;
                }

                modName = selectedModName.Trim();
            }

            ModEntry? existingMod = FindInstalledModByName(Mods, modName);
            if (existingMod is not null)
            {
                ExistingModInstallMode? selectedMode = modInstallDialogService.PickExistingModInstallMode(existingMod.DisplayName);
                if (!selectedMode.HasValue)
                {
                    ModOperationProcess.Reset();
                    ActivityMessage = $"Установка отменена: {modName}";
                    operation.Complete($"cancelled=true, fomod={isFomodInstall}, modName=\"{modName}\", existingMod=true");
                    return;
                }

                existingModMode = selectedMode.Value;
            }

            if (isFomodInstall && fomodSelection is not null)
            {
                ModOperationProcess.ApplyProgress("Проверяю размещение", modName, 20);
                layoutPreview = await downloadCatalogService.AnalyzeFomodDownloadContentLayoutAsync(
                    SelectedProject,
                    downloadToInstall,
                    existingModMode,
                    fomodSelection);
                if (!layoutPreview.CanInstall)
                {
                    string blockerMessage = ContentLayoutPreviewBlockerText(layoutPreview);
                    ActivityMessage = blockerMessage;
                    ModOperationProcess.Fail(blockerMessage);
                    operation.Fail(new InvalidOperationException(blockerMessage));
                    return;
                }
            }

            ModOperationProcess.Start(
                InstallOperationTitle(existingModMode),
                "Подготовка установки",
                $"Готовлю архив: {downloadToInstall.Name}");
            int targetInsertionIndex = insertionIndex.HasValue
                ? Math.Clamp(ResolveFullModInsertionIndex(insertionIndex.Value), 0, Mods.Count)
                : Mods.Count;
            ModOperationProcess.ApplyProgress(InstallProgressStep(existingModMode), modName, 24);
            ModEntry installedMod = isFomodInstall
                ? await downloadCatalogService.InstallFomodDownloadAsync(
                    SelectedProject,
                    downloadToInstall,
                    modName,
                    existingModMode,
                    fomodSelection ?? Array.Empty<string>())
                : await downloadCatalogService.InstallDownloadAsync(
                    SelectedProject,
                    downloadToInstall,
                    modName,
                    existingModMode);
            RefreshSelectedProjectSizeAfterMutation();

            if (insertionIndex.HasValue)
            {
                int targetIndex = targetInsertionIndex >= Mods.Count
                    ? Mods.Count
                    : targetInsertionIndex;
                try
                {
                    ModOperationProcess.ApplyProgress("Обновляю порядок модов", "Ищу установленный мод в профиле", 62);
                    IReadOnlyList<ModEntry> refreshedMods = await modCatalogService.GetInstalledModsAsync(
                        SelectedProject,
                        SelectedProfile);
                    ModEntry installedOrderEntry = ResolveInstalledModOrderEntry(refreshedMods, installedMod);
                    ModOperationProcess.ApplyProgress("Ставлю мод на место", $"Позиция {targetIndex + 1}", 72);
                    IReadOnlyList<ModEntry> mods = await modCatalogService.MoveModOrderItemAsync(
                        SelectedProject,
                        SelectedProfile,
                        installedOrderEntry,
                        targetIndex);
                    SyncMods(mods, installedMod.Id);
                    NotifyModCountPropertiesChanged();
                    RaiseModCommandStateChanged();
                }
                catch (Exception placementException)
                {
                    ModOperationProcess.ApplyProgress("Восстанавливаю список", "Обновляю данные после ошибки размещения", 78);
                    await LoadModsFromProjectAsync(SelectedProject);
                    await LoadPluginsFromProjectAsync(SelectedProject);
                    await LoadDownloadsFromProjectAsync(SelectedProject);
                    SelectedMod = ResolveVisibleModSelection(installedMod.Id);
                    ActivityMessage = $"Мод установлен, но не удалось переместить в выбранное место: {placementException.Message}";
                    ModOperationProcess.Fail(ActivityMessage);
                    operation.Fail(placementException);
                    return;
                }
            }
            else
            {
                ModOperationProcess.ApplyProgress("Обновляю список модов", "Синхронизирую профиль", 70);
                await LoadModsFromProjectAsync(SelectedProject);
            }

            ModOperationProcess.ApplyProgress("Обновляю плагины и загрузки", "Проверяю load order и архивы", 88);
            await Task.WhenAll(
                LoadPluginsFromProjectAsync(SelectedProject),
                LoadDownloadsFromProjectAsync(SelectedProject));
            SelectedMod = ResolveVisibleModSelection(installedMod.Id);
            ActivityMessage = InstallSuccessMessage(existingModMode, installedMod.Name, insertionIndex.HasValue);
            await CompleteModOperationSplashAsync(InstallCompleteStep(existingModMode), installedMod.Name);
            operation.Complete($"installedMod=\"{installedMod.Name}\", id=\"{installedMod.Id}\", existingModMode=\"{existingModMode}\", fomod={isFomodInstall}, insertionIndex={targetInsertionIndex}");
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ModOperationProcess.Fail(exception.Message);
            ActivityMessage = $"Не удалось установить мод: {exception.Message}";
        }
        finally
        {
            IsProcessingDownload = false;
        }
    }

    private async void DeleteSelectedDownload(DownloadEntry? download)
    {
        if (SelectedProject is null)
        {
            return;
        }

        IReadOnlyList<DownloadEntry> downloadsToDelete = ResolveDownloadActionTargets(download)
            .Where(target => target.CanDelete)
            .OrderByDescending(target => IndexOfDownload(target.Id))
            .ToList();
        if (downloadsToDelete.Count == 0)
        {
            return;
        }

        FocusDownloadSelection(downloadsToDelete[0]);

        using var operation = logService.BeginOperation(
            "DeleteDownloads",
            $"project=\"{SelectedProject.Name}\", count={downloadsToDelete.Count}");
        try
        {
            IsProcessingDownload = true;
            foreach (DownloadEntry downloadToDelete in downloadsToDelete)
            {
                await downloadCatalogService.DeleteDownloadAsync(SelectedProject, downloadToDelete);
            }

            await LoadDownloadsFromProjectAsync(SelectedProject);
            RefreshSelectedProjectSizeAfterMutation();
            ActivityMessage = downloadsToDelete.Count == 1
                ? $"Загрузка удалена: {downloadsToDelete[0].FileName}"
                : $"Загрузки удалены: {downloadsToDelete.Count}";
            operation.Complete($"deleted={downloadsToDelete.Count}");
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ActivityMessage = $"Не удалось удалить загрузки: {exception.Message}";
        }
        finally
        {
            IsProcessingDownload = false;
        }
    }

    private async void CancelDownload(DownloadEntry? download)
    {
        if (SelectedProject is null)
        {
            return;
        }

        DownloadEntry? downloadToCancel = download ?? SelectedDownload;
        if (downloadToCancel?.IsDownloading != true)
        {
            return;
        }

        SelectedDownload = downloadToCancel;

        using var operation = logService.BeginOperation(
            "CancelDownload",
            $"project=\"{SelectedProject.Name}\", downloadPath=\"{downloadToCancel.LocalPath}\"");
        try
        {
            await downloadCatalogService.CancelDownloadAsync(SelectedProject, downloadToCancel);
            await LoadDownloadsFromProjectAsync(SelectedProject);
            RefreshSelectedProjectSizeAfterMutation();
            ActivityMessage = $"Отмена скачивания: {downloadToCancel.Name}";
            operation.Complete();
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ActivityMessage = $"Не удалось отменить скачивание: {exception.Message}";
        }
    }

    private async void ResumeDownload(DownloadEntry? download)
    {
        if (SelectedProject is null)
        {
            return;
        }

        DownloadEntry? downloadToResume = download ?? SelectedDownload;
        if (downloadToResume?.CanResume != true)
        {
            return;
        }

        SelectedDownload = downloadToResume;

        using var operation = logService.BeginOperation(
            "ResumeDownload",
            $"project=\"{SelectedProject.Name}\", downloadPath=\"{downloadToResume.LocalPath}\"");
        try
        {
            IsProcessingDownload = true;
            Task<DownloadEntry> resumeTask = downloadCatalogService.ResumeDownloadAsync(SelectedProject, downloadToResume);
            await RunDownloadOperationWithLiveRefreshAsync(SelectedProject, resumeTask);
            DownloadEntry resumedDownload = await resumeTask;
            ActivityMessage = resumedDownload.CanInstall
                ? $"Скачивание завершено: {resumedDownload.Name}"
                : $"Скачивание приостановлено: {resumedDownload.Name}";
            operation.Complete($"download=\"{resumedDownload.Name}\", canInstall={resumedDownload.CanInstall}");
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ActivityMessage = $"Не удалось возобновить скачивание: {exception.Message}";
        }
        finally
        {
            IsProcessingDownload = false;
        }
    }

    private void OpenDownloadInExplorer(DownloadEntry? download)
    {
        if (SelectedProject is null)
        {
            return;
        }

        DownloadEntry? downloadToOpen = download ?? SelectedDownload;
        if (downloadToOpen is null)
        {
            return;
        }

        SelectedDownload = downloadToOpen;

        if (!string.IsNullOrWhiteSpace(downloadToOpen.LocalPath) && File.Exists(downloadToOpen.LocalPath))
        {
            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = "explorer.exe",
                    Arguments = $"/select,\"{downloadToOpen.LocalPath}\"",
                    UseShellExecute = true
                });
                return;
            }
            catch (Exception exception)
            {
                ActivityMessage = $"Не удалось открыть архив: {exception.Message}";
                return;
            }
        }

        string downloadsDirectory = SelectedProject.Paths.DownloadsDirectory;
        Directory.CreateDirectory(downloadsDirectory);
        OpenDirectory(downloadsDirectory, "Папка загрузок не найдена.", "Архив не найден. Открыта папка загрузок.");
    }

    private void RegisterNxmProtocol()
    {
        bool isRegistered = nxmProtocolService.RegisterCurrentUserHandler();
        ActivityMessage = isRegistered
            ? "Fluxora перехватывает NXM-ссылки Mod Manager."
            : "Не удалось зарегистрировать NXM-перехватчик. Проверьте, что C++ core доступен.";
    }

    private async void LaunchSelectedExecutable()
    {
        if (SelectedProject is null || SelectedExecutable?.Executable is not { } executable)
        {
            return;
        }

        ModProject project = SelectedProject;
        string processName = ExecutableLaunchProcessViewModel.ResolveProcessName(
            executable.ExecutablePath,
            executable.DisplayTitle);
        LaunchTrackingMetadata launchTracking = ResolveLaunchTrackingMetadata(project, executable);
        bool tracksExpectedChildProcess = UsesExpectedChildProcessTracking(launchTracking);
        ExecutableLaunchProcess.Start(
            executable.ExecutablePath,
            executable.DisplayTitle,
            project.Name,
            tracksExpectedChildProcess,
            launchTracking.HandoffDisplayName);

        using var operation = logService.BeginOperation(
            "LaunchExecutable",
            $"project=\"{project.Name}\", configPath=\"{project.ConfigPath}\", executableId=\"{executable.Id}\"");
        try
        {
            GameExecutableLaunchResult result = await coreBridgeService.LaunchGameExecutableAsync(
                project.ConfigPath,
                executable.Id);
            project.LastLaunchedAt = DateTimeOffset.Now;
            OnPropertyChanged(nameof(SelectedProjectLastLaunchText));
            AttachLaunchedExecutable(result, project, executable);
            ActivityMessage = $"Процесс запущен: {ExecutableLaunchProcess.ProcessName}";
            operation.Complete(
                $"displayName=\"{result.DisplayName}\", resolvedPath=\"{result.ResolvedExecutablePath}\", pid={result.ProcessId}");
        }
        catch (Exception exception)
        {
            operation.Fail(exception);
            ReleaseLaunchedExecutableProcess();
            launchSessionStore.Clear();
            ExecutableLaunchProcess.Fail(exception.Message);
            ActivityMessage = $"Не удалось запустить executable: {exception.Message}";
            logService.OperationError(
                "Launch",
                $"Launch process failed before tracking attached. process=\"{processName}\"",
                exception);
        }
    }

    private void RestoreActiveLaunchSession()
    {
        ExecutableLaunchSession? session = launchSessionStore.Load();
        if (session is null)
        {
            return;
        }

        try
        {
            Process process = Process.GetProcessById(session.ProcessId);
            if (process.HasExited ||
                session.ProcessStartTimeUtc == default ||
                !TryReadProcessStartTimeUtc(process, out DateTimeOffset currentStartTimeUtc) ||
                !IsSameProcessStartTime(session.ProcessStartTimeUtc, currentStartTimeUtc))
            {
                process.Dispose();
                if (TryFindExpectedChildProcess(session, out Process? childProcess))
                {
                    AttachLaunchProcess(childProcess, CreateChildLaunchSession(session, childProcess), recovered: true);
                    ActivityMessage = $"Процесс запущен: {ExecutableLaunchProcess.ProcessName}";
                    return;
                }

                launchSessionStore.Clear();
                logService.OperationInfo(
                    "Launch",
                    $"Stale launch session discarded. pid={session.ProcessId}, process=\"{session.ProcessName}\"");
                return;
            }

            AttachLaunchProcess(process, session, recovered: true);
            ActivityMessage = $"Процесс запущен: {session.ProcessName}";
        }
        catch (Exception exception) when (exception is ArgumentException or InvalidOperationException or Win32Exception)
        {
            if (TryFindExpectedChildProcess(session, out Process? childProcess))
            {
                AttachLaunchProcess(childProcess, CreateChildLaunchSession(session, childProcess), recovered: true);
                ActivityMessage = $"Процесс запущен: {ExecutableLaunchProcess.ProcessName}";
                return;
            }

            launchSessionStore.Clear();
            logService.OperationWarning(
                "Launch",
                $"Launch session could not be restored. pid={session.ProcessId}, process=\"{session.ProcessName}\"",
                exception);
        }
    }

    private void AttachLaunchedExecutable(
        GameExecutableLaunchResult result,
        ModProject project,
        GameExecutableEntry executable)
    {
        string processName = ExecutableLaunchProcessViewModel.ResolveProcessName(
            result.ResolvedExecutablePath,
            result.DisplayName);

        if (result.ProcessId <= 0)
        {
            throw new InvalidOperationException("Native core did not return the launched process id.");
        }

        ExecutableLaunchSession session = CreateLaunchSession(
            result,
            project,
            executable,
            processStartTimeUtc: default);

        Process process;
        try
        {
            process = Process.GetProcessById(result.ProcessId);
        }
        catch (ArgumentException)
        {
            if (ShouldWatchForExpectedChildProcess(session))
            {
                BeginDetachedExpectedChildProcessHandoff(session, $"{processName} · процесс уже закрыт");
                return;
            }

            launchSessionStore.Clear();
            ExecutableLaunchProcess.MarkExited($"{processName} · процесс уже закрыт");
            logService.OperationInfo(
                "Launch",
                $"Launched process exited before tracking attached. pid={result.ProcessId}, process=\"{processName}\"");
            return;
        }

        DateTimeOffset processStartTimeUtc = TryReadProcessStartTimeUtc(process, out DateTimeOffset startTimeUtc)
            ? startTimeUtc
            : default;

        session = CreateLaunchSession(result, project, executable, processStartTimeUtc);

        AttachLaunchProcess(process, session, recovered: false);
    }

    private static ExecutableLaunchSession CreateLaunchSession(
        GameExecutableLaunchResult result,
        ModProject project,
        GameExecutableEntry executable,
        DateTimeOffset processStartTimeUtc)
    {
        string processName = ExecutableLaunchProcessViewModel.ResolveProcessName(
            result.ResolvedExecutablePath,
            result.DisplayName);
        LaunchTrackingMetadata launchTracking = ResolveLaunchTrackingMetadata(result, project, executable);

        return new ExecutableLaunchSession
        {
            ProcessId = result.ProcessId,
            ProcessName = processName,
            LauncherProcessId = result.ProcessId,
            LauncherProcessName = processName,
            DisplayName = result.DisplayName,
            ExecutableId = executable.Id,
            ResolvedExecutablePath = result.ResolvedExecutablePath,
            ResolvedWorkingDirectory = result.ResolvedWorkingDirectory,
            ProjectName = project.Name,
            ConfigPath = project.ConfigPath,
            LaunchTrackingKind = launchTracking.Kind,
            HandoffDisplayName = launchTracking.HandoffDisplayName,
            HandoffTimeoutMs = launchTracking.HandoffTimeoutMs > 0
                ? launchTracking.HandoffTimeoutMs
                : ExecutableLaunchSession.DefaultHandoffTimeoutMs,
            ExpectedChildProcessNames = launchTracking.ExpectedChildProcessNames
                .Where(name => !string.IsNullOrWhiteSpace(name))
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToList(),
            StartedAtUtc = DateTimeOffset.UtcNow,
            ProcessStartTimeUtc = processStartTimeUtc
        };
    }

    private void AttachLaunchProcess(Process process, ExecutableLaunchSession session, bool recovered)
    {
        ReleaseLaunchedExecutableProcess();

        launchedExecutableProcess = process;
        activeLaunchSession = session;
        isLaunchHandoffPending = false;
        process.EnableRaisingEvents = true;
        process.Exited += OnLaunchedExecutableExited;

        try
        {
            launchSessionStore.Save(session);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            logService.OperationWarning(
                "Launch",
                $"Launch session persistence failed; tracking current process in memory only. pid={session.ProcessId}, process=\"{session.ProcessName}\"",
                exception);
        }

        ExecutableLaunchProcess.MarkLaunched(session, recovered);
        StartExpectedChildProcessWatcher(session);
        logService.OperationInfo(
            "Launch",
            $"Tracking launched process. pid={session.ProcessId}, process=\"{session.ProcessName}\", recovered={recovered}");

        if (ShouldWatchForExpectedChildProcess(session) &&
            TryFindExpectedChildProcess(session, out Process? childProcess))
        {
            PromoteExpectedChildProcess(childProcess, session);
            return;
        }

        if (process.HasExited)
        {
            HandleLaunchedExecutableExited(process, FormatProcessExitText(process));
        }
    }

    private void CloseExecutableLaunchProcess()
    {
        if (isLaunchHandoffPending)
        {
            ActivityMessage = BuildLaunchHandoffActivityMessage(activeLaunchSession);
            return;
        }

        if (ExecutableLaunchProcess.IsProcessRunning && launchedExecutableProcess is not null)
        {
            TerminateTrackedLaunchProcess();
            return;
        }

        launchSessionStore.Clear();
        ReleaseLaunchedExecutableProcess();
        ExecutableLaunchProcess.Close();
    }

    private void TerminateTrackedLaunchProcess()
    {
        Process? process = launchedExecutableProcess;
        if (process is null)
        {
            launchSessionStore.Clear();
            ExecutableLaunchProcess.MarkExited("Процесс уже не отслеживается");
            return;
        }

        try
        {
            if (isLaunchHandoffPending)
            {
                ActivityMessage = BuildLaunchHandoffActivityMessage(activeLaunchSession);
                return;
            }

            if (process.HasExited)
            {
                HandleLaunchedExecutableExited(process, FormatProcessExitText(process));
                return;
            }

            logService.OperationInfo(
                "Launch",
                $"User requested launched process termination. pid={process.Id}, process=\"{ExecutableLaunchProcess.ProcessName}\"");
            process.Kill();
            ActivityMessage = $"Закрываю процесс: {ExecutableLaunchProcess.ProcessName}";
        }
        catch (InvalidOperationException)
        {
            HandleLaunchedExecutableExited(process, $"{ExecutableLaunchProcess.ProcessName} · процесс уже закрыт");
        }
        catch (Exception exception) when (exception is Win32Exception or NotSupportedException)
        {
            ActivityMessage = $"Не удалось закрыть процесс: {exception.Message}";
            logService.OperationError(
                "Launch",
                $"Launched process termination failed. pid={activeLaunchSession?.ProcessId ?? 0}, process=\"{ExecutableLaunchProcess.ProcessName}\"",
                exception);
        }
    }

    private void OnLaunchedExecutableExited(object? sender, EventArgs e)
    {
        if (sender is not Process process)
        {
            return;
        }

        string exitText = FormatProcessExitText(process);
        RunOnUiThread(() => HandleLaunchedExecutableExited(process, exitText));
    }

    private void HandleLaunchedExecutableExited(Process process, string exitText)
    {
        if (!ReferenceEquals(process, launchedExecutableProcess))
        {
            return;
        }

        int processId = activeLaunchSession?.ProcessId ?? process.Id;
        string processName = activeLaunchSession?.ProcessName ?? ExecutableLaunchProcess.ProcessName;
        if (activeLaunchSession is { } session && ShouldWatchForExpectedChildProcess(session))
        {
            BeginExpectedChildProcessHandoff(process, session, exitText);
            return;
        }

        launchSessionStore.Clear();
        ReleaseLaunchedExecutableProcess();
        ExecutableLaunchProcess.MarkExited(exitText);
        CloseExecutableLaunchSplashAfterExitAsync();
        ActivityMessage = $"Процесс закрыт: {processName}";
        logService.OperationInfo(
            "Launch",
            $"Tracked launched process exited. pid={processId}, process=\"{processName}\"");
    }

    private void ReleaseLaunchedExecutableProcess()
    {
        StopExpectedChildProcessWatcher();
        CancelLaunchHandoffWait();
        isLaunchHandoffPending = false;

        if (launchedExecutableProcess is not null)
        {
            launchedExecutableProcess.Exited -= OnLaunchedExecutableExited;
            launchedExecutableProcess.Dispose();
            launchedExecutableProcess = null;
        }

        activeLaunchSession = null;
    }

    private void StartExpectedChildProcessWatcher(ExecutableLaunchSession session)
    {
        StopExpectedChildProcessWatcher();
        if (!ShouldWatchForExpectedChildProcess(session))
        {
            return;
        }

        try
        {
            int launcherProcessId = LauncherProcessIdFor(session);
            WqlEventQuery query = new(
                $"SELECT * FROM Win32_ProcessStartTrace WHERE ParentProcessID = {launcherProcessId}");
            launchedChildProcessWatcher = new ManagementEventWatcher(query);
            launchedChildProcessWatcher.EventArrived += OnExpectedChildProcessStarted;
            launchedChildProcessWatcher.Start();
            logService.OperationInfo(
                "Launch",
                $"Watching for launched child process. launcherPid={launcherProcessId}, expected=\"{string.Join(",", session.ExpectedChildProcessNames)}\"");
        }
        catch (Exception exception) when (IsManagementException(exception))
        {
            StopExpectedChildProcessWatcher();
            logService.OperationWarning(
                "Launch",
                $"Could not watch for launched child process. launcherPid={LauncherProcessIdFor(session)}",
                exception);
        }
    }

    private void StopExpectedChildProcessWatcher()
    {
        if (launchedChildProcessWatcher is null)
        {
            return;
        }

        try
        {
            launchedChildProcessWatcher.EventArrived -= OnExpectedChildProcessStarted;
            launchedChildProcessWatcher.Stop();
        }
        catch
        {
        }
        finally
        {
            launchedChildProcessWatcher.Dispose();
            launchedChildProcessWatcher = null;
        }
    }

    private void OnExpectedChildProcessStarted(object sender, EventArrivedEventArgs e)
    {
        try
        {
            ExecutableLaunchSession? session = activeLaunchSession;
            if (session is null || !ShouldWatchForExpectedChildProcess(session))
            {
                return;
            }

            string processName = Convert.ToString(e.NewEvent.Properties["ProcessName"]?.Value) ?? string.Empty;
            if (!IsExpectedChildProcessName(session, processName))
            {
                return;
            }

            int processId = Convert.ToInt32(e.NewEvent.Properties["ProcessID"]?.Value);
            Process childProcess = Process.GetProcessById(processId);
            RunOnUiThread(() => PromoteExpectedChildProcess(childProcess, session));
        }
        catch (Exception exception) when (
            exception is ArgumentException or InvalidOperationException or Win32Exception ||
            IsManagementException(exception))
        {
            logService.OperationWarning("Launch", "Expected child process event could not be handled.", exception);
        }
    }

    private void BeginExpectedChildProcessHandoff(
        Process launcherProcess,
        ExecutableLaunchSession session,
        string launcherExitText)
    {
        if (TryFindExpectedChildProcess(session, out Process? childProcess))
        {
            PromoteExpectedChildProcess(childProcess, session);
            return;
        }

        if (isLaunchHandoffPending)
        {
            return;
        }

        isLaunchHandoffPending = true;
        ExecutableLaunchProcess.MarkWaitingForChildProcess(session);
        ActivityMessage = BuildLaunchHandoffActivityMessage(session);
        logService.OperationInfo(
            "Launch",
            $"Launcher exited; waiting for expected child process. launcherPid={launcherProcess.Id}, process=\"{session.ProcessName}\"");

        CancelLaunchHandoffWait();
        launchHandoffCancellation = new CancellationTokenSource();
        _ = CompleteExpectedChildProcessHandoffAsync(session, launcherExitText, launchHandoffCancellation.Token);
    }

    private void BeginDetachedExpectedChildProcessHandoff(
        ExecutableLaunchSession session,
        string launcherExitText)
    {
        ReleaseLaunchedExecutableProcess();
        activeLaunchSession = session;

        try
        {
            launchSessionStore.Save(session);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            logService.OperationWarning(
                "Launch",
                $"Detached launch session persistence failed; tracking handoff in memory only. pid={session.ProcessId}, process=\"{session.ProcessName}\"",
                exception);
        }

        StartExpectedChildProcessWatcher(session);
        if (TryFindExpectedChildProcess(session, out Process? childProcess))
        {
            PromoteExpectedChildProcess(childProcess, session);
            return;
        }

        isLaunchHandoffPending = true;
        ExecutableLaunchProcess.MarkWaitingForChildProcess(session);
        ActivityMessage = BuildLaunchHandoffActivityMessage(session);
        logService.OperationInfo(
            "Launch",
            $"Launcher exited before tracking attached; waiting for expected child process. launcherPid={LauncherProcessIdFor(session)}, process=\"{session.ProcessName}\"");

        CancelLaunchHandoffWait();
        launchHandoffCancellation = new CancellationTokenSource();
        _ = CompleteExpectedChildProcessHandoffAsync(session, launcherExitText, launchHandoffCancellation.Token);
    }

    private async Task CompleteExpectedChildProcessHandoffAsync(
        ExecutableLaunchSession session,
        string launcherExitText,
        CancellationToken cancellationToken)
    {
        try
        {
            int handoffTimeoutMs = session.HandoffTimeoutMs > 0
                ? session.HandoffTimeoutMs
                : ExecutableLaunchSession.DefaultHandoffTimeoutMs;
            await Task.Delay(TimeSpan.FromMilliseconds(handoffTimeoutMs), cancellationToken);
        }
        catch (OperationCanceledException)
        {
            return;
        }

        RunOnUiThread(() =>
        {
            if (!isLaunchHandoffPending ||
                activeLaunchSession is null ||
                !string.Equals(activeLaunchSession.SessionId, session.SessionId, StringComparison.OrdinalIgnoreCase))
            {
                return;
            }

            if (TryFindExpectedChildProcess(session, out Process? childProcess))
            {
                PromoteExpectedChildProcess(childProcess, session);
                return;
            }

            string processName = session.ProcessName;
            int processId = session.ProcessId;
            launchSessionStore.Clear();
            ReleaseLaunchedExecutableProcess();
            ExecutableLaunchProcess.MarkExited(launcherExitText);
            CloseExecutableLaunchSplashAfterExitAsync();
            ActivityMessage = $"Процесс закрыт: {processName}";
            logService.OperationInfo(
                "Launch",
                $"Expected child process was not found after launcher exit. launcherPid={processId}, process=\"{processName}\"");
        });
    }

    private async void CloseExecutableLaunchSplashAfterExitAsync()
    {
        await Task.Delay(ExecutableLaunchSplashCompletionHold);
        if (ExecutableLaunchProcess.IsCompleted &&
            !ExecutableLaunchProcess.IsProcessRunning &&
            !ExecutableLaunchProcess.IsStarting)
        {
            ExecutableLaunchProcess.Close();
        }
    }

    private void CancelLaunchHandoffWait()
    {
        if (launchHandoffCancellation is null)
        {
            return;
        }

        launchHandoffCancellation.Cancel();
        launchHandoffCancellation.Dispose();
        launchHandoffCancellation = null;
    }

    private void PromoteExpectedChildProcess(Process childProcess, ExecutableLaunchSession launcherSession)
    {
        if (activeLaunchSession is null ||
            !string.Equals(activeLaunchSession.SessionId, launcherSession.SessionId, StringComparison.OrdinalIgnoreCase) ||
            !ShouldWatchForExpectedChildProcess(activeLaunchSession))
        {
            childProcess.Dispose();
            return;
        }

        ExecutableLaunchSession childSession = CreateChildLaunchSession(launcherSession, childProcess);
        AttachLaunchProcess(childProcess, childSession, recovered: false);
        ActivityMessage = $"Приложение запущено: {childSession.ProcessName}";
        logService.OperationInfo(
            "Launch",
            $"Switched tracking from launcher to child process. launcherPid={LauncherProcessIdFor(launcherSession)}, childPid={childSession.ProcessId}, child=\"{childSession.ProcessName}\"");
    }

    private ExecutableLaunchSession CreateChildLaunchSession(
        ExecutableLaunchSession launcherSession,
        Process childProcess)
    {
        string childProcessPath = TryReadProcessImagePath(childProcess, out string imagePath)
            ? imagePath
            : string.Empty;
        string childProcessName = !string.IsNullOrWhiteSpace(childProcessPath)
            ? ExecutableLaunchProcessViewModel.ResolveProcessName(childProcessPath)
            : SafeProcessName(childProcess);
        DateTimeOffset processStartTimeUtc = TryReadProcessStartTimeUtc(childProcess, out DateTimeOffset startTimeUtc)
            ? startTimeUtc
            : default;

        return new ExecutableLaunchSession
        {
            SessionId = launcherSession.SessionId,
            ProcessId = childProcess.Id,
            ProcessName = childProcessName,
            LauncherProcessId = LauncherProcessIdFor(launcherSession),
            LauncherProcessName = string.IsNullOrWhiteSpace(launcherSession.LauncherProcessName)
                ? launcherSession.ProcessName
                : launcherSession.LauncherProcessName,
            DisplayName = launcherSession.DisplayName,
            ExecutableId = launcherSession.ExecutableId,
            ResolvedExecutablePath = string.IsNullOrWhiteSpace(childProcessPath)
                ? childProcessName
                : childProcessPath,
            ResolvedWorkingDirectory = !string.IsNullOrWhiteSpace(childProcessPath)
                ? Path.GetDirectoryName(childProcessPath) ?? launcherSession.ResolvedWorkingDirectory
                : launcherSession.ResolvedWorkingDirectory,
            ProjectName = launcherSession.ProjectName,
            ConfigPath = launcherSession.ConfigPath,
            LaunchTrackingKind = launcherSession.LaunchTrackingKind,
            HandoffDisplayName = launcherSession.HandoffDisplayName,
            HandoffTimeoutMs = launcherSession.HandoffTimeoutMs,
            ExpectedChildProcessNames = launcherSession.ExpectedChildProcessNames.ToList(),
            StartedAtUtc = launcherSession.StartedAtUtc,
            ProcessStartTimeUtc = processStartTimeUtc
        };
    }

    private bool TryFindExpectedChildProcess(
        ExecutableLaunchSession session,
        [NotNullWhen(true)] out Process? process)
    {
        process = null;
        if (session.ExpectedChildProcessNames.Count == 0)
        {
            return false;
        }

        try
        {
            int launcherProcessId = LauncherProcessIdFor(session);
            using ManagementObjectSearcher searcher = new(
                $"SELECT ProcessId, Name FROM Win32_Process WHERE ParentProcessId = {launcherProcessId}");
            using ManagementObjectCollection results = searcher.Get();
            foreach (ManagementObject result in results)
            {
                using (result)
                {
                    string processName = Convert.ToString(result["Name"]) ?? string.Empty;
                    if (!IsExpectedChildProcessName(session, processName))
                    {
                        continue;
                    }

                    int processId = Convert.ToInt32(result["ProcessId"]);
                    Process candidate = Process.GetProcessById(processId);
                    if (!candidate.HasExited)
                    {
                        process = candidate;
                        return true;
                    }

                    candidate.Dispose();
                }
            }
        }
        catch (Exception exception) when (
            exception is ArgumentException or InvalidOperationException or Win32Exception ||
            IsManagementException(exception))
        {
            logService.OperationWarning(
                "Launch",
                $"Expected child process lookup failed. launcherPid={LauncherProcessIdFor(session)}",
                exception);
        }

        return false;
    }

    private static bool TryReadProcessStartTimeUtc(Process process, out DateTimeOffset startTimeUtc)
    {
        try
        {
            startTimeUtc = new DateTimeOffset(process.StartTime).ToUniversalTime();
            return true;
        }
        catch (Exception exception) when (exception is InvalidOperationException or Win32Exception)
        {
            startTimeUtc = default;
            return false;
        }
    }

    private static bool IsSameProcessStartTime(DateTimeOffset expectedUtc, DateTimeOffset actualUtc)
    {
        return (expectedUtc - actualUtc).Duration() <= LaunchSessionStartTimeTolerance;
    }

    private string FormatProcessExitText(Process process)
    {
        string processName = activeLaunchSession?.ProcessName ?? ExecutableLaunchProcess.ProcessName;
        try
        {
            return $"{processName} · код выхода {process.ExitCode}";
        }
        catch (Exception exception) when (exception is InvalidOperationException or Win32Exception)
        {
            return processName;
        }
    }

    private static bool TryReadProcessImagePath(Process process, out string imagePath)
    {
        try
        {
            imagePath = process.MainModule?.FileName ?? string.Empty;
            return !string.IsNullOrWhiteSpace(imagePath);
        }
        catch (Exception exception) when (exception is InvalidOperationException or Win32Exception or NotSupportedException)
        {
            imagePath = string.Empty;
            return false;
        }
    }

    private static string SafeProcessName(Process process)
    {
        try
        {
            string processName = process.ProcessName;
            return string.IsNullOrWhiteSpace(processName) ? "process.exe" : $"{processName}.exe";
        }
        catch (InvalidOperationException)
        {
            return "process.exe";
        }
    }

    private static bool ShouldWatchForExpectedChildProcess(ExecutableLaunchSession session)
    {
        return session.TracksExpectedChildProcess &&
            LauncherProcessIdFor(session) > 0 &&
            session.ExpectedChildProcessNames.Count > 0;
    }

    private static bool UsesExpectedChildProcessTracking(LaunchTrackingMetadata launchTracking)
    {
        return string.Equals(launchTracking.Kind, "expectedChildProcess", StringComparison.OrdinalIgnoreCase) &&
            launchTracking.ExpectedChildProcessNames.Count > 0;
    }

    private static LaunchTrackingMetadata ResolveLaunchTrackingMetadata(
        ModProject project,
        GameExecutableEntry executable)
    {
        LaunchTrackingMetadata? templateMetadata = ShouldUseTemplateLaunchTracking(project, executable, null)
            ? project.Template?.LaunchTrackingMetadata
            : null;
        return NormalizeLaunchTrackingMetadata(
            templateMetadata?.Kind,
            templateMetadata?.ExpectedChildProcessNames,
            templateMetadata?.HandoffDisplayName,
            templateMetadata?.HandoffTimeoutMs,
            project.GameName,
            executable.DisplayTitle);
    }

    internal static LaunchTrackingMetadata ResolveLaunchTrackingMetadata(
        GameExecutableLaunchResult result,
        ModProject project,
        GameExecutableEntry executable)
    {
        LaunchTrackingMetadata nested = result.LaunchTrackingMetadata ?? new LaunchTrackingMetadata();
        LaunchTrackingMetadata? templateMetadata = ShouldUseTemplateLaunchTracking(project, executable, result.ExecutableDisplayMetadata)
            ? project.Template?.LaunchTrackingMetadata
            : null;
        string? kind = !string.IsNullOrWhiteSpace(nested.Kind) && !string.Equals(nested.Kind, "directProcess", StringComparison.OrdinalIgnoreCase)
            ? nested.Kind
            : !string.IsNullOrWhiteSpace(result.LaunchTrackingKind) && !string.Equals(result.LaunchTrackingKind, "directProcess", StringComparison.OrdinalIgnoreCase)
                ? result.LaunchTrackingKind
                : templateMetadata?.Kind;
        IReadOnlyList<string> expectedChildProcessNames = nested.ExpectedChildProcessNames.Count > 0
            ? nested.ExpectedChildProcessNames
            : result.ExpectedChildProcessNames.Count > 0
                ? result.ExpectedChildProcessNames
                : (IReadOnlyList<string>?)templateMetadata?.ExpectedChildProcessNames ?? Array.Empty<string>();
        string? handoffDisplayName = !string.IsNullOrWhiteSpace(nested.HandoffDisplayName)
            ? nested.HandoffDisplayName
            : !string.IsNullOrWhiteSpace(result.HandoffDisplayName)
                ? result.HandoffDisplayName
                : templateMetadata?.HandoffDisplayName;
        int? handoffTimeoutMs = nested.HandoffTimeoutMs > 0
            ? nested.HandoffTimeoutMs
            : result.HandoffTimeoutMs > 0
                ? result.HandoffTimeoutMs
                : templateMetadata?.HandoffTimeoutMs;

        return NormalizeLaunchTrackingMetadata(
            kind,
            expectedChildProcessNames,
            handoffDisplayName,
            handoffTimeoutMs,
            project.GameName,
            executable.DisplayTitle);
    }

    private static LaunchTrackingMetadata NormalizeLaunchTrackingMetadata(
        string? kind,
        IReadOnlyList<string>? expectedChildProcessNames,
        string? handoffDisplayName,
        int? handoffTimeoutMs,
        string gameDisplayName,
        string executableDisplayName)
    {
        List<string> childProcessNames = (expectedChildProcessNames ?? Array.Empty<string>())
            .Where(name => !string.IsNullOrWhiteSpace(name))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
        string normalizedKind = string.IsNullOrWhiteSpace(kind)
            ? "directProcess"
            : kind.Trim();
        if (!string.Equals(normalizedKind, "expectedChildProcess", StringComparison.OrdinalIgnoreCase))
        {
            childProcessNames.Clear();
        }

        return new LaunchTrackingMetadata
        {
            Kind = normalizedKind,
            ExpectedChildProcessNames = childProcessNames,
            HandoffDisplayName = !string.IsNullOrWhiteSpace(handoffDisplayName)
                ? handoffDisplayName.Trim()
                : !string.IsNullOrWhiteSpace(gameDisplayName)
                    ? gameDisplayName.Trim()
                    : executableDisplayName.Trim(),
            HandoffTimeoutMs = handoffTimeoutMs.GetValueOrDefault() > 0
                ? handoffTimeoutMs.GetValueOrDefault()
                : ExecutableLaunchSession.DefaultHandoffTimeoutMs
        };
    }

    private static bool ShouldUseTemplateLaunchTracking(
        ModProject project,
        GameExecutableEntry executable,
        ExecutableDisplayMetadata? resultMetadata)
    {
        if (executable.ExecutableDisplayMetadata.IsScriptExtender ||
            resultMetadata?.IsScriptExtender == true)
        {
            return true;
        }

        string executableName = Path.GetFileName(executable.ExecutablePath);
        return project.Template?.ExecutableDisplayMetadata.Any(metadata =>
            metadata.IsScriptExtender &&
            !string.IsNullOrWhiteSpace(metadata.ExecutableName) &&
            string.Equals(metadata.ExecutableName, executableName, StringComparison.OrdinalIgnoreCase)) == true;
    }

    private static string BuildLaunchHandoffActivityMessage(ExecutableLaunchSession? session)
    {
        if (session is null)
        {
            return "Ожидаю приложение после запуска.";
        }

        string displayName = !string.IsNullOrWhiteSpace(session.HandoffDisplayName)
            ? session.HandoffDisplayName
            : !string.IsNullOrWhiteSpace(session.DisplayName)
                ? session.DisplayName
                : session.ProcessName;
        return string.IsNullOrWhiteSpace(session.ProjectName)
            ? $"Ожидаю приложение: {displayName}"
            : $"Ожидаю приложение: {session.ProjectName} · {displayName}";
    }

    private static int LauncherProcessIdFor(ExecutableLaunchSession session)
    {
        return session.LauncherProcessId > 0 ? session.LauncherProcessId : session.ProcessId;
    }

    private static bool IsExpectedChildProcessName(ExecutableLaunchSession session, string processName)
    {
        return session.ExpectedChildProcessNames.Any(expected =>
            string.Equals(expected, processName, StringComparison.OrdinalIgnoreCase));
    }

    private static bool IsManagementException(Exception exception)
    {
        return exception is ManagementException or UnauthorizedAccessException or COMException;
    }

    private static void RunOnUiThread(Action action)
    {
        System.Windows.Threading.Dispatcher? dispatcher = System.Windows.Application.Current?.Dispatcher;
        if (dispatcher is not null && !dispatcher.CheckAccess())
        {
            dispatcher.BeginInvoke(action);
            return;
        }

        action();
    }

    private void OpenProjectSubdirectory(string relativePath, string successMessage)
    {
        if (SelectedProject is null)
        {
            return;
        }

        string directory = relativePath switch
        {
            "mods" => SelectedProject.Paths.ModsDirectory,
            "profiles" => SelectedProject.Paths.ProfilesDirectory,
            "downloads" => SelectedProject.Paths.DownloadsDirectory,
            _ => Path.Combine(SelectedProject.ProjectDirectory, relativePath)
        };
        Directory.CreateDirectory(directory);
        OpenDirectory(directory, "Папка сборки не найдена.", successMessage);
    }

    private void OpenDirectory(string? directory, string missingMessage, string? successMessage = null)
    {
        if (string.IsNullOrWhiteSpace(directory) || !Directory.Exists(directory))
        {
            ActivityMessage = missingMessage;
            return;
        }

        if (StartShell(directory))
        {
            ActivityMessage = successMessage ?? $"Папка открыта: {directory}";
        }
    }

    private bool StartShell(string path)
    {
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = path,
                UseShellExecute = true
            });
            return true;
        }
        catch (Exception exception)
        {
            ActivityMessage = $"Не удалось открыть путь: {exception.Message}";
            return false;
        }
    }

    private bool CanMoveSelectedPlugin(int direction)
    {
        if (!IsProjectWorkspaceOpen ||
            SelectedPlugin is not { IsLocked: false } plugin ||
            IsProcessingPlugins ||
            IsWorkspaceOperationBlocked)
        {
            return false;
        }

        if (SelectedVisiblePlugins().Count > 1)
        {
            return false;
        }

        int index = IndexOfPlugin(plugin.Id);
        if (index < 0)
        {
            return false;
        }

        if (plugin.IsSeparator)
        {
            int spanLength = PluginOrderMoveSpanLength(index);
            if (PluginMoveSpanContainsLockedPlugin(index, spanLength))
            {
                return false;
            }

            return direction < 0
                ? index > 0
                : index + spanLength < Plugins.Count;
        }

        int targetIndex = index + direction;
        return targetIndex >= 0 &&
            targetIndex < Plugins.Count &&
            !Plugins[targetIndex].IsLocked;
    }

    private void RaiseWizardStateChanged()
    {
        OnPropertyChanged(nameof(IsNameStep));
        OnPropertyChanged(nameof(IsGameStep));
        OnPropertyChanged(nameof(IsGameExeStep));
        OnPropertyChanged(nameof(IsInstallStep));
        OnPropertyChanged(nameof(CreateProjectStepTitle));
        OnPropertyChanged(nameof(CreateProjectStepSubtitle));
        OnPropertyChanged(nameof(CreateProjectStepCounter));
        (BrowseGamePathCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (BrowseInstallRootCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (CancelCreateProjectCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (PreviousCreateStepCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (NextCreateStepCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (CreateProjectCommand as RelayCommand)?.RaiseCanExecuteChanged();
    }

    private void RaiseBusyCommandStateChanged()
    {
        (OpenCreateProjectCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (InstallFluxPackCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (OpenProjectCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (OpenProjectBuildCommand as RelayCommand<ModProject>)?.RaiseCanExecuteChanged();
        (RenameProjectCommand as RelayCommand<ModProject>)?.RaiseCanExecuteChanged();
        (DeleteProjectCommand as RelayCommand<ModProject>)?.RaiseCanExecuteChanged();
        (PackageProjectCommand as RelayCommand<ModProject>)?.RaiseCanExecuteChanged();
        RaiseWorkspaceCommandStateChanged();
    }

    private void RaiseWorkspaceStateChanged()
    {
        OnPropertyChanged(nameof(IsProjectWorkspaceOpen));
        OnPropertyChanged(nameof(IsHomeViewOpen));
        OnPropertyChanged(nameof(IsModlistViewOpen));
        OnPropertyChanged(nameof(WorkspaceTitle));
        OnPropertyChanged(nameof(WorkspaceSubtitle));
        NotifyCapabilityPropertiesChanged();
        RaiseWorkspaceCommandStateChanged();
    }

    private void NotifyCapabilityPropertiesChanged()
    {
        OnPropertyChanged(nameof(CanShowPluginsPanel));
        OnPropertyChanged(nameof(CanShowLoadOrderPanel));
        OnPropertyChanged(nameof(CanShowIniPanel));
        OnPropertyChanged(nameof(CanShowSavePanel));
        OnPropertyChanged(nameof(CanShowScriptExtenderPanel));
        OnPropertyChanged(nameof(CanShowRootFilesPanel));
        OnPropertyChanged(nameof(CanShowExecutablePanel));
        OnPropertyChanged(nameof(CanShowContentLayoutReviewPanel));
        OnPropertyChanged(nameof(CanShowHealthDiagnosticsPanel));
        OnPropertyChanged(nameof(CanShowBuildOverviewPanel));
        OnPropertyChanged(nameof(SelectedProjectContentLayoutSummaryText));
        OnPropertyChanged(nameof(SelectedProjectContentLayoutDetails));
        OnPropertyChanged(nameof(SelectedProjectContentLayoutWarnings));
        OnPropertyChanged(nameof(SelectedProjectContentLayoutBlockers));
        OnPropertyChanged(nameof(SelectedProjectContentLayoutDataFolder));
        OnPropertyChanged(nameof(HasSelectedProjectContentLayoutDataFolder));
        OnPropertyChanged(nameof(SelectedProjectRootFilesText));
        OnPropertyChanged(nameof(SelectedProjectScriptExtenderText));
        OnPropertyChanged(nameof(SelectedProjectIniProfilesText));
        OnPropertyChanged(nameof(SelectedProjectSaveProfilesText));
        OnPropertyChanged(nameof(SelectedProjectHealthStatusText));
        OnPropertyChanged(nameof(SelectedProjectHealthFindings));
    }

    private void RaiseWorkspaceCommandStateChanged()
    {
        (BackToProjectsCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (RefreshWorkspaceCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (OpenBuildSettingsCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (OpenProjectDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (OpenGameDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (OpenHomeProjectDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (OpenHomeGameDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (PackageProjectCommand as RelayCommand<ModProject>)?.RaiseCanExecuteChanged();
        (OpenModsDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (OpenProfilesDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (OpenDownloadsDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (AddDownloadFileCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (CheckModUpdatesCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (CreateModSeparatorCommand as RelayCommand)?.RaiseCanExecuteChanged();
        RaiseModCommandStateChanged();
        RaisePluginCommandStateChanged();
        RaiseDownloadCommandStateChanged();
        (LaunchSelectedExecutableCommand as RelayCommand)?.RaiseCanExecuteChanged();
    }

    private void RaiseModCommandStateChanged()
    {
        (MoveSelectedModUpCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (MoveSelectedModDownCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (DeleteSelectedModCommand as RelayCommand<ModEntry>)?.RaiseCanExecuteChanged();
        (OpenModInExplorerCommand as RelayCommand<ModEntry>)?.RaiseCanExecuteChanged();
        (EnableSelectedModCommand as RelayCommand<ModEntry>)?.RaiseCanExecuteChanged();
        (EnableAllModsCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (DisableSelectedModCommand as RelayCommand<ModEntry>)?.RaiseCanExecuteChanged();
        (DisableAllModsCommand as RelayCommand)?.RaiseCanExecuteChanged();
    }

    private void RaisePluginCommandStateChanged()
    {
        (MoveSelectedPluginUpCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (MoveSelectedPluginDownCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (CreatePluginSeparatorCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (DeleteSelectedPluginCommand as RelayCommand<PluginEntry>)?.RaiseCanExecuteChanged();
        (EnableSelectedPluginCommand as RelayCommand<PluginEntry>)?.RaiseCanExecuteChanged();
        (DisableSelectedPluginCommand as RelayCommand<PluginEntry>)?.RaiseCanExecuteChanged();
        (TogglePluginEnabledCommand as RelayCommand<PluginEntry>)?.RaiseCanExecuteChanged();
    }

    private void RaiseDownloadCommandStateChanged()
    {
        (InstallSelectedDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
        (DeleteSelectedDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
        (CancelDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
        (ResumeDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
        (OpenDownloadInExplorerCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
    }

    private void RaiseProjectCollectionStateChanged()
    {
        OnPropertyChanged(nameof(HasProjects));
        OnPropertyChanged(nameof(ProjectCountText));
    }

    private void AddOrReplaceProject(ModProject project)
    {
        int existingIndex = -1;
        for (int index = 0; index < Projects.Count; index++)
        {
            ModProject candidate = Projects[index];
            if (IsSamePath(candidate.ConfigPath, project.ConfigPath) ||
                IsSamePath(candidate.ProjectDirectory, project.ProjectDirectory))
            {
                existingIndex = index;
                break;
            }
        }

        if (existingIndex >= 0)
        {
            Projects[existingIndex] = project;
        }
        else
        {
            Projects.Add(project);
        }

        RaiseProjectCollectionStateChanged();
    }

    private void ReplaceProject(ModProject oldProject, ModProject newProject)
    {
        int existingIndex = -1;
        for (int index = 0; index < Projects.Count; index++)
        {
            ModProject candidate = Projects[index];
            if (IsSameProject(candidate, oldProject) ||
                IsSameProject(candidate, newProject))
            {
                existingIndex = index;
                break;
            }
        }

        if (existingIndex >= 0)
        {
            Projects[existingIndex] = newProject;
        }
        else
        {
            Projects.Add(newProject);
        }

        RaiseProjectCollectionStateChanged();
    }

    private void RemoveProject(ModProject project)
    {
        for (int index = Projects.Count - 1; index >= 0; --index)
        {
            if (IsSameProject(Projects[index], project))
            {
                Projects.RemoveAt(index);
            }
        }

        RaiseProjectCollectionStateChanged();
    }

    private void ClearWorkspaceData()
    {
        Mods.Clear();
        VisibleMods = new ObservableCollection<ModEntry>();
        Plugins.Clear();
        Downloads.Clear();
        SelectedModFileTree.Clear();
        AvailableProfiles.Clear();
        AvailableExecutables.Clear();
        SelectedMod = null;
        SelectedPlugin = null;
        SelectedDownload = null;
        modSelectionAnchorId = null;
        pluginSelectionAnchorId = null;
        downloadSelectionAnchorId = null;
        SelectedExecutable = null;
        NotifyModCountPropertiesChanged(updateSelectedProjectCounts: false);
        OnPropertyChanged(nameof(HasPlugins));
        OnPropertyChanged(nameof(HasDownloads));
        OnPropertyChanged(nameof(HasSelectedModFileTree));
        OnPropertyChanged(nameof(PluginCountText));
        OnPropertyChanged(nameof(DownloadCountText));
    }

    private static bool IsSamePath(string left, string right)
    {
        return !string.IsNullOrWhiteSpace(left) &&
            !string.IsNullOrWhiteSpace(right) &&
            string.Equals(left, right, StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsSameProject(ModProject? left, ModProject? right)
    {
        return left is not null &&
            right is not null &&
            (IsSamePath(left.ConfigPath, right.ConfigPath) ||
             IsSamePath(left.ProjectDirectory, right.ProjectDirectory) ||
             (!string.IsNullOrWhiteSpace(left.Id) &&
              string.Equals(left.Id, right.Id, StringComparison.OrdinalIgnoreCase)));
    }

    private static string FormatImportActivityMessage(string message, string warning)
    {
        return string.IsNullOrWhiteSpace(warning)
            ? message
            : $"{message}. {warning}";
    }

    private static string InstallOperationTitle(ExistingModInstallMode mode) => mode switch
    {
        ExistingModInstallMode.Replace => "Замена мода",
        ExistingModInstallMode.Merge => "Объединение мода",
        _ => "Установка мода"
    };

    private static string InstallProgressStep(ExistingModInstallMode mode) => mode switch
    {
        ExistingModInstallMode.Replace => "Заменяю мод",
        ExistingModInstallMode.Merge => "Объединяю файлы мода",
        _ => "Устанавливаю мод"
    };

    private static string InstallCompleteStep(ExistingModInstallMode mode) => mode switch
    {
        ExistingModInstallMode.Replace => "Мод заменён",
        ExistingModInstallMode.Merge => "Мод объединён",
        _ => "Мод установлен"
    };

    private static string ContentLayoutPreviewBlockerText(ContentLayoutPreview preview)
    {
        string blocker = preview.ValidationFindings
            .FirstOrDefault(finding => finding.BlocksInstall)
            ?.Message ?? string.Empty;
        if (!string.IsNullOrWhiteSpace(blocker))
        {
            return $"Установка заблокирована: {blocker}";
        }

        return string.IsNullOrWhiteSpace(preview.ExplanationSummary)
            ? "Установка заблокирована правилами размещения."
            : preview.ExplanationSummary;
    }

    private static string InstallSuccessMessage(
        ExistingModInstallMode mode,
        string modName,
        bool insertedAtSelectedPosition) => mode switch
    {
        ExistingModInstallMode.Replace => $"Мод заменён: {modName}",
        ExistingModInstallMode.Merge => $"Мод объединён: {modName}",
        _ => insertedAtSelectedPosition
            ? $"Мод установлен в выбранное место: {modName}"
            : $"Мод установлен: {modName}"
    };

    private static string FormatFluxPackExportMessage(FluxPackSummary summary)
    {
        string fileName = string.IsNullOrWhiteSpace(summary.OutputPath)
            ? "FluxPack"
            : Path.GetFileName(summary.OutputPath);
        string generated = summary.GeneratedAssetsIncluded
            ? $"{summary.GeneratedAssetCount} generated"
            : $"{summary.GeneratedAssetCount} generated без включения";

        return $"FluxPack сохранён: {fileName}. Источники: {summary.SourceArchiveCount}, {generated}, патчи: {summary.CustomPatchCount}, конфиги: {summary.CustomConfigCount}.";
    }

    private static string FormatFluxPackInstallMessage(FluxPackInstallResult result)
    {
        string buildName = string.IsNullOrWhiteSpace(result.BuildName)
            ? "сборка"
            : result.BuildName;
        ulong failedSources = result.FailedSourceCount + result.PendingSourceCount;
        string warning = result.HasWarnings
            ? $" Ошибок источников: {failedSources}."
            : string.Empty;

        return $"FluxPack установлен: {buildName}. Источники: {result.InstalledSourceCount}/{result.TotalSourceCount}, конфиги: {result.AppliedConfigCount}, порядок: {result.AppliedProfileOrderItemCount}.{warning}";
    }

    private static string ModListItemKey(ModEntry mod)
    {
        return string.IsNullOrWhiteSpace(mod.OrderId) ? mod.Id : mod.OrderId;
    }

    private static string PluginListItemKey(PluginEntry plugin)
    {
        return string.IsNullOrWhiteSpace(plugin.OrderId) ? plugin.Id : plugin.OrderId;
    }

    internal static string ResolveFomodInstallName(
        FomodInstallerInfo installer,
        DownloadEntry download)
    {
        if (!string.IsNullOrWhiteSpace(installer.ModuleName))
        {
            return installer.ModuleName.Trim();
        }

        if (!string.IsNullOrWhiteSpace(download.Name))
        {
            return download.Name.Trim();
        }

        if (!string.IsNullOrWhiteSpace(download.FileName))
        {
            string fileName = Path.GetFileNameWithoutExtension(download.FileName);
            if (!string.IsNullOrWhiteSpace(fileName))
            {
                return fileName.Trim();
            }
        }

        if (!string.IsNullOrWhiteSpace(download.LocalPath))
        {
            string fileName = Path.GetFileNameWithoutExtension(download.LocalPath);
            if (!string.IsNullOrWhiteSpace(fileName))
            {
                return fileName.Trim();
            }
        }

        return "FOMOD";
    }

    internal static ModEntry? FindInstalledModByName(
        IEnumerable<ModEntry> mods,
        string modName)
    {
        string normalizedName = modName.Trim();
        if (string.IsNullOrWhiteSpace(normalizedName))
        {
            return null;
        }

        return mods.FirstOrDefault(mod =>
            mod.IsMod &&
            (string.Equals(mod.DisplayName, normalizedName, StringComparison.OrdinalIgnoreCase) ||
             string.Equals(ModFolderName(mod), normalizedName, StringComparison.OrdinalIgnoreCase)));
    }

    private static string ModFolderName(ModEntry mod)
    {
        if (string.IsNullOrWhiteSpace(mod.Id))
        {
            return string.Empty;
        }

        return Path.GetFileName(mod.Id.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
    }

    internal static bool CanOpenInstalledModInExplorer(ModEntry? mod)
    {
        return mod is { IsMod: true, Id: { } id } &&
            !string.IsNullOrWhiteSpace(id);
    }

    internal static ModEntry ResolveInstalledModOrderEntry(
        IReadOnlyList<ModEntry> refreshedMods,
        ModEntry installedMod)
    {
        ModEntry? resolved = refreshedMods.FirstOrDefault(mod =>
            mod.IsMod &&
            IsSamePath(mod.Id, installedMod.Id));

        if (resolved is null)
        {
            throw new InvalidOperationException("Installed mod was not found in the profile order.");
        }

        if (string.IsNullOrWhiteSpace(resolved.OrderId))
        {
            throw new InvalidOperationException("Installed mod profile order item was not found.");
        }

        return resolved;
    }

    private static bool IsSameModListItem(ModEntry mod, string id)
    {
        return (!string.IsNullOrWhiteSpace(mod.OrderId) &&
                string.Equals(mod.OrderId, id, StringComparison.OrdinalIgnoreCase)) ||
            (!string.IsNullOrWhiteSpace(mod.Id) &&
             string.Equals(mod.Id, id, StringComparison.OrdinalIgnoreCase));
    }

    private static bool IsSamePlugin(string left, string right)
    {
        return !string.IsNullOrWhiteSpace(left) &&
            !string.IsNullOrWhiteSpace(right) &&
            string.Equals(left, right, StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsSamePluginListItem(PluginEntry plugin, string id)
    {
        return (!string.IsNullOrWhiteSpace(plugin.OrderId) &&
                string.Equals(plugin.OrderId, id, StringComparison.OrdinalIgnoreCase)) ||
            (!string.IsNullOrWhiteSpace(plugin.Id) &&
             string.Equals(plugin.Id, id, StringComparison.OrdinalIgnoreCase));
    }

    private static bool AreEquivalentModEntries(ModEntry left, ModEntry right)
    {
        return string.Equals(left.Id, right.Id, StringComparison.OrdinalIgnoreCase) &&
            string.Equals(left.OrderId, right.OrderId, StringComparison.OrdinalIgnoreCase) &&
            string.Equals(left.Kind, right.Kind, StringComparison.OrdinalIgnoreCase) &&
            left.Order == right.Order &&
            string.Equals(left.ModUuid, right.ModUuid, StringComparison.OrdinalIgnoreCase) &&
            string.Equals(left.SeparatorTitle, right.SeparatorTitle, StringComparison.Ordinal) &&
            string.Equals(left.Name, right.Name, StringComparison.Ordinal) &&
            string.Equals(left.Version, right.Version, StringComparison.Ordinal) &&
            string.Equals(left.LatestVersion, right.LatestVersion, StringComparison.Ordinal) &&
            string.Equals(left.LastCheckedAt, right.LastCheckedAt, StringComparison.Ordinal) &&
            string.Equals(left.UpdateStatus, right.UpdateStatus, StringComparison.Ordinal) &&
            string.Equals(left.ConflictStatus, right.ConflictStatus, StringComparison.Ordinal) &&
            left.FileCount == right.FileCount &&
            left.ConflictingFileCount == right.ConflictingFileCount &&
            left.OverwrittenFileCount == right.OverwrittenFileCount &&
            left.OverwritingFileCount == right.OverwritingFileCount &&
            left.IsEnabled == right.IsEnabled &&
            left.CanCheckUpdates == right.CanCheckUpdates &&
            left.HasUpdate == right.HasUpdate;
    }

    private static bool AreEquivalentPluginEntries(PluginEntry left, PluginEntry right)
    {
        return string.Equals(left.Id, right.Id, StringComparison.OrdinalIgnoreCase) &&
            string.Equals(left.OrderId, right.OrderId, StringComparison.OrdinalIgnoreCase) &&
            string.Equals(left.Kind, right.Kind, StringComparison.OrdinalIgnoreCase) &&
            left.Order == right.Order &&
            string.Equals(left.Name, right.Name, StringComparison.Ordinal) &&
            string.Equals(left.SeparatorTitle, right.SeparatorTitle, StringComparison.Ordinal) &&
            string.Equals(left.Extension, right.Extension, StringComparison.OrdinalIgnoreCase) &&
            string.Equals(left.SourceMod, right.SourceMod, StringComparison.Ordinal) &&
            left.IsEnabled == right.IsEnabled &&
            left.IsMaster == right.IsMaster &&
            left.IsLight == right.IsLight &&
            left.IsLocked == right.IsLocked &&
            string.Equals(left.LockReason, right.LockReason, StringComparison.Ordinal);
    }

    private static string FormatBuildCount(int count)
    {
        int lastTwoDigits = count % 100;
        int lastDigit = count % 10;

        if (count == 0)
        {
            return T("Сборок пока нет");
        }

        if (lastTwoDigits is >= 11 and <= 14)
        {
            return F("{0} сборок", count);
        }

        return lastDigit switch
        {
            1 => F("{0} сборка", count),
            >= 2 and <= 4 => F("{0} сборки", count),
            _ => F("{0} сборок", count)
        };
    }

    private static string FormatModWord(int count)
    {
        int lastTwoDigits = count % 100;
        int lastDigit = count % 10;

        if (lastTwoDigits is >= 11 and <= 14)
        {
            return T("модов");
        }

        return lastDigit switch
        {
            1 => T("мод"),
            >= 2 and <= 4 => T("мода"),
            _ => T("модов")
        };
    }

    private static string FormatPluginWord(int count)
    {
        int lastTwoDigits = count % 100;
        int lastDigit = count % 10;

        if (lastTwoDigits is >= 11 and <= 14)
        {
            return T("плагинов");
        }

        return lastDigit switch
        {
            1 => T("плагин"),
            >= 2 and <= 4 => T("плагина"),
            _ => T("плагинов")
        };
    }

    private static string FormatDownloadWord(int count)
    {
        int lastTwoDigits = count % 100;
        int lastDigit = count % 10;

        if (lastTwoDigits is >= 11 and <= 14)
        {
            return T("загрузок");
        }

        return lastDigit switch
        {
            1 => T("загрузка"),
            >= 2 and <= 4 => T("загрузки"),
            _ => T("загрузок")
        };
    }

    private static string FormatSelectedDownloadStatus(DownloadEntry download)
    {
        List<string> parts = new() { download.Status };
        if (!string.IsNullOrWhiteSpace(download.ProgressText))
        {
            parts.Add(download.ProgressText);
        }
        else if (!string.IsNullOrWhiteSpace(download.SizeText))
        {
            parts.Add(download.SizeText);
        }

        if (!string.IsNullOrWhiteSpace(download.EtaText))
        {
            parts.Add(download.EtaText);
        }

        return string.Join("  ·  ", parts);
    }

    private void OnLocalizationChanged(object? sender, EventArgs e)
    {
        OnPropertyChanged(nameof(BuildLoadingSplashPhrase));
        OnPropertyChanged(nameof(BuildLoadingSplashDetail));
        OnPropertyChanged(nameof(SelectedExecutableToolTip));
        OnPropertyChanged(nameof(ValidationMessage));
        OnPropertyChanged(nameof(ActivityMessage));
        OnPropertyChanged(nameof(ProjectCountText));
        OnPropertyChanged(nameof(ModCountText));
        OnPropertyChanged(nameof(ModSearchResultText));
        OnPropertyChanged(nameof(ModListEmptyTitle));
        OnPropertyChanged(nameof(ModListEmptySubtitle));
        OnPropertyChanged(nameof(ProjectModBreakdownText));
        OnPropertyChanged(nameof(PluginCountText));
        OnPropertyChanged(nameof(DownloadCountText));
        OnPropertyChanged(nameof(SelectedProjectProfileFilesText));
        OnPropertyChanged(nameof(SelectedProjectLastLaunchText));
        OnPropertyChanged(nameof(SelectedModTitle));
        OnPropertyChanged(nameof(SelectedModVersionText));
        OnPropertyChanged(nameof(SelectedModUpdateText));
        OnPropertyChanged(nameof(SelectedModConflictText));
        OnPropertyChanged(nameof(SelectedPluginTitle));
        OnPropertyChanged(nameof(SelectedPluginStatusText));
        OnPropertyChanged(nameof(SelectedDownloadTitle));
        OnPropertyChanged(nameof(SelectedDownloadStatusText));
        OnPropertyChanged(nameof(CreateProjectStepTitle));
        OnPropertyChanged(nameof(CreateProjectStepSubtitle));
        OnPropertyChanged(nameof(CreateProjectStepCounter));
        OnPropertyChanged(nameof(TargetProjectDirectory));
    }

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        OnPropertyChanged(propertyName);
        return true;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }

    private static string T(string fallback)
    {
        return LocalizationManager.Current.Text(fallback);
    }

    private static string F(string fallback, params object?[] args)
    {
        return LocalizationManager.Current.Format(fallback, args);
    }

    private sealed class NullFluxPackPickerService : IFluxPackPickerService
    {
        public string? PickFluxPack(string selectedDirectory) => null;

        public string? PickFluxPackSavePath(string selectedDirectory, string suggestedFileName) => null;
    }

    private sealed class NullConfirmDialogService : IConfirmDialogService
    {
        public bool Confirm(ConfirmDialogOptions options) => false;
    }
}
