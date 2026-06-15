using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Input;
using Fluxora.App.Models;
using Fluxora.App.Services;

namespace Fluxora.App.ViewModels;

public sealed class MainWindowViewModel : INotifyPropertyChanged
{
    private const int DownloadsWorkspaceTabIndex = 2;
    private static readonly TimeSpan BuildDeletionSplashCompletionHold = TimeSpan.FromMilliseconds(620);
    private static readonly TimeSpan BuildLoadingSplashPhraseInterval = TimeSpan.FromMilliseconds(4350);
    private static readonly TimeSpan BuildLoadingSplashMinimumDuration = TimeSpan.FromMilliseconds(650);
    private static readonly string[] BuildLoadingSplashPhrases =
    {
        "Загружаем сборку",
        "Проверяем конфиг",
        "Собираем моды",
        "Подключаем плагины",
        "Почти готово",
        "Ещё чуть-чуть"
    };

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
    private readonly IFolderPickerService folderPickerService;
    private readonly IExecutablePickerService executablePickerService;
    private readonly IBuildConfigPickerService buildConfigPickerService;
    private readonly IModArchivePickerService modArchivePickerService;
    private readonly IModInstallDialogService modInstallDialogService;
    private readonly IExecutableManagerDialogService executableManagerDialogService;
    private readonly IBuildSettingsDialogService buildSettingsDialogService;
    private readonly IBuildDeletionDialogService buildDeletionDialogService;
    private readonly SeparatorCollapseService modCollapseService = new();
    private readonly SeparatorCollapseService pluginCollapseService = new();

    private string coreStatus = "C++ core: bridge pending";
    private string projectsDirectory = string.Empty;
    private ModProject? selectedProject;
    private ModEntry? selectedMod;
    private PluginEntry? selectedPlugin;
    private DownloadEntry? selectedDownload;
    private bool isProjectWorkspaceOpen;
    private bool isCreateProjectPanelOpen;
    private bool isCreatingProject;
    private bool isOpeningProject;
    private bool isProcessingDownload;
    private bool isProcessingPlugins;
    private bool isCheckingModUpdates;
    private bool isLoadingModFiles;
    private bool isBuildLoadingSplashVisible;
    private readonly BuildDeletionProcessViewModel buildDeletionProcess = new();
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
    private Task? workspaceLoadTask;
    private long workspaceLoadVersion;

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
        IFolderPickerService folderPickerService,
        IExecutablePickerService executablePickerService,
        IBuildConfigPickerService buildConfigPickerService,
        IModArchivePickerService modArchivePickerService,
        IModInstallDialogService modInstallDialogService,
        IExecutableManagerDialogService executableManagerDialogService,
        IBuildSettingsDialogService buildSettingsDialogService,
        IBuildDeletionDialogService buildDeletionDialogService)
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
        this.folderPickerService = folderPickerService;
        this.executablePickerService = executablePickerService;
        this.buildConfigPickerService = buildConfigPickerService;
        this.modArchivePickerService = modArchivePickerService;
        this.modInstallDialogService = modInstallDialogService;
        this.executableManagerDialogService = executableManagerDialogService;
        this.buildSettingsDialogService = buildSettingsDialogService;
        this.buildDeletionDialogService = buildDeletionDialogService;

        OpenCreateProjectCommand = new RelayCommand(OpenCreateProject, () => !IsCreatingProject && !IsOpeningProject && !IsTransferPanelOpen);
        OpenProjectCommand = new RelayCommand(OpenProjectFromConfig, () => !IsCreatingProject && !IsOpeningProject && !IsTransferPanelOpen);
        OpenProjectBuildCommand = new RelayCommand<ModProject>(
            OpenProjectBuild,
            project => project is not null && !IsCreatingProject && !IsOpeningProject && !IsTransferPanelOpen);
        RenameProjectCommand = new RelayCommand<ModProject>(
            RenameProject,
            project => project is not null && !IsCreatingProject && !IsOpeningProject && !IsTransferPanelOpen);
        DeleteProjectCommand = new RelayCommand<ModProject>(
            DeleteProject,
            project => project is not null && !IsCreatingProject && !IsOpeningProject && !IsTransferPanelOpen);
        BackToProjectsCommand = new RelayCommand(BackToProjects, () => IsProjectWorkspaceOpen && !IsOpeningProject);
        RefreshWorkspaceCommand = new RelayCommand(RefreshProjectWorkspace, () => IsProjectWorkspaceOpen && HasSelectedProject);
        OpenBuildSettingsCommand = new RelayCommand(OpenBuildSettings, () => IsProjectWorkspaceOpen && HasSelectedProject && !IsOpeningProject);
        OpenProjectDirectoryCommand = new RelayCommand(OpenProjectDirectory, () => IsProjectWorkspaceOpen && HasSelectedProject);
        OpenGameDirectoryCommand = new RelayCommand(OpenGameDirectory, () => IsProjectWorkspaceOpen && HasSelectedProject);
        OpenHomeProjectDirectoryCommand = new RelayCommand(OpenProjectDirectory, () => HasSelectedProject);
        OpenHomeGameDirectoryCommand = new RelayCommand(OpenGameDirectory, () => HasSelectedProject);
        OpenModsDirectoryCommand = new RelayCommand(OpenModsDirectory, () => IsProjectWorkspaceOpen && HasSelectedProject);
        OpenProfilesDirectoryCommand = new RelayCommand(OpenProfilesDirectory, () => IsProjectWorkspaceOpen && HasSelectedProject);
        OpenDownloadsDirectoryCommand = new RelayCommand(OpenDownloadsDirectory, () => IsProjectWorkspaceOpen && HasSelectedProject);
        AddDownloadFileCommand = new RelayCommand(AddDownloadFile, () => IsProjectWorkspaceOpen && HasSelectedProject && !IsProcessingDownload);
        CheckModUpdatesCommand = new RelayCommand(CheckModUpdates, () => IsProjectWorkspaceOpen && HasSelectedProject && !IsCheckingModUpdates);
        CreateModSeparatorCommand = new RelayCommand(CreateModSeparator, () => IsProjectWorkspaceOpen && HasSelectedProject && !IsProcessingDownload);
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
            mod => IsProjectWorkspaceOpen && HasSelectedProject && (mod ?? SelectedMod) is not null && !IsProcessingDownload);
        EnableSelectedModCommand = new RelayCommand<ModEntry>(
            mod => SetSelectedModEnabled(mod, true),
            mod => CanSetSelectedModEnabled(mod, true));
        EnableAllModsCommand = new RelayCommand(
            () => SetAllModsEnabled(true),
            () => IsProjectWorkspaceOpen && HasSelectedProject && HasMods && !IsProcessingDownload);
        DisableSelectedModCommand = new RelayCommand<ModEntry>(
            mod => SetSelectedModEnabled(mod, false),
            mod => CanSetSelectedModEnabled(mod, false));
        DisableAllModsCommand = new RelayCommand(
            () => SetAllModsEnabled(false),
            () => IsProjectWorkspaceOpen && HasSelectedProject && HasMods && !IsProcessingDownload);
        MoveSelectedPluginUpCommand = new RelayCommand(
            MoveSelectedPluginUp,
            () => CanMoveSelectedPlugin(-1));
        MoveSelectedPluginDownCommand = new RelayCommand(
            MoveSelectedPluginDown,
            () => CanMoveSelectedPlugin(1));
        CreatePluginSeparatorCommand = new RelayCommand(
            CreatePluginSeparator,
            () => IsProjectWorkspaceOpen && HasSelectedProject && !IsProcessingPlugins);
        TogglePluginSeparatorCommand = new RelayCommand<PluginEntry>(
            TogglePluginSeparator,
            plugin => plugin is { IsSeparator: true });
        DeleteSelectedPluginCommand = new RelayCommand<PluginEntry>(
            DeleteSelectedPlugin,
            plugin => IsProjectWorkspaceOpen && HasSelectedProject && (plugin ?? SelectedPlugin)?.IsSeparator == true && !IsProcessingPlugins);
        TogglePluginEnabledCommand = new RelayCommand<PluginEntry>(
            TogglePluginEnabled,
            plugin => IsProjectWorkspaceOpen && HasSelectedProject && plugin is { CanToggle: true } && !IsProcessingPlugins);
        InstallSelectedDownloadCommand = new RelayCommand<DownloadEntry>(
            InstallSelectedDownload,
            download => IsProjectWorkspaceOpen && HasSelectedProject && (download ?? SelectedDownload)?.CanInstall == true && !IsProcessingDownload);
        DeleteSelectedDownloadCommand = new RelayCommand<DownloadEntry>(
            DeleteSelectedDownload,
            download => IsProjectWorkspaceOpen && HasSelectedProject && (download ?? SelectedDownload)?.CanDelete == true && !IsProcessingDownload);
        CancelDownloadCommand = new RelayCommand<DownloadEntry>(
            CancelDownload,
            download => IsProjectWorkspaceOpen && HasSelectedProject && (download ?? SelectedDownload)?.IsDownloading == true);
        ResumeDownloadCommand = new RelayCommand<DownloadEntry>(
            ResumeDownload,
            download => IsProjectWorkspaceOpen && HasSelectedProject && (download ?? SelectedDownload)?.CanResume == true && !IsProcessingDownload);
        OpenDownloadInExplorerCommand = new RelayCommand<DownloadEntry>(
            OpenDownloadInExplorer,
            download => IsProjectWorkspaceOpen && HasSelectedProject && !string.IsNullOrWhiteSpace((download ?? SelectedDownload)?.LocalPath));
        RegisterNxmProtocolCommand = new RelayCommand(
            RegisterNxmProtocol,
            () => !IsProcessingDownload);
        LaunchSelectedExecutableCommand = new RelayCommand(
            LaunchSelectedExecutable,
            () => IsProjectWorkspaceOpen && HasSelectedProject && SelectedExecutable?.Executable is not null);
        BrowseGamePathCommand = new RelayCommand(BrowseGamePath, () => !IsCreatingProject);
        BrowseInstallRootCommand = new RelayCommand(BrowseInstallRoot, () => !IsCreatingProject);
        CancelCreateProjectCommand = new RelayCommand(CancelCreateProject, () => !IsCreatingProject);
        PreviousCreateStepCommand = new RelayCommand(MoveToPreviousStep, () => IsCreateProjectPanelOpen && !IsCreatingProject && CreateProjectStepIndex > 0);
        NextCreateStepCommand = new RelayCommand(MoveToNextStep, () => IsCreateProjectPanelOpen && !IsCreatingProject && CreateProjectStepIndex < 2);
        CreateProjectCommand = new RelayCommand(CreateProject, () => IsCreateProjectPanelOpen && !IsCreatingProject && CreateProjectStepIndex == 2);
        CloseTransferPanelCommand = new RelayCommand(CloseTransferPanel, () => IsTransferPanelClosable);
        CloseBuildDeletionProcessCommand = new RelayCommand(CloseBuildDeletionProcess, () => BuildDeletionProcess.CanClose);
        BuildDeletionProcess.PropertyChanged += OnBuildDeletionProcessPropertyChanged;
        LocalizationManager.Current.LanguageChanged += OnLocalizationChanged;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public ObservableCollection<ModProject> Projects { get; } = new();

    public ObservableCollection<ModEntry> Mods { get; } = new();

    public ObservableCollection<PluginEntry> Plugins { get; } = new();

    public ObservableCollection<DownloadEntry> Downloads { get; } = new();

    public ObservableCollection<ModFileTreeNode> SelectedModFileTree { get; } = new();

    public ObservableCollection<string> AvailableProfiles { get; } = new();

    public ObservableCollection<ExecutableMenuItem> AvailableExecutables { get; } = new();

    /// <summary>Game templates offered by the C++ core (base template excluded).</summary>
    public ObservableCollection<GameTemplateOption> AvailableTemplates { get; } = new();

    public ICommand OpenCreateProjectCommand { get; }
    public ICommand OpenProjectCommand { get; }
    public ICommand OpenProjectBuildCommand { get; }
    public ICommand RenameProjectCommand { get; }
    public ICommand DeleteProjectCommand { get; }
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
    public ICommand CloseTransferPanelCommand { get; }
    public ICommand CloseBuildDeletionProcessCommand { get; }

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
                OnPropertyChanged(nameof(SelectedProjectConfigText));
                OnPropertyChanged(nameof(SelectedProjectExecutablesText));
                OnPropertyChanged(nameof(SelectedProjectPluginExtensionsText));
                OnPropertyChanged(nameof(WorkspaceTitle));
                OnPropertyChanged(nameof(WorkspaceSubtitle));
                OnPropertyChanged(nameof(SelectedProjectProfileFilesText));
                RaiseWorkspaceCommandStateChanged();
            }
        }
    }

    public ModEntry? SelectedMod
    {
        get => selectedMod;
        set
        {
            if (SetField(ref selectedMod, value))
            {
                OnPropertyChanged(nameof(SelectedModTitle));
                OnPropertyChanged(nameof(SelectedModVersionText));
                OnPropertyChanged(nameof(SelectedModUpdateText));
                OnPropertyChanged(nameof(SelectedModConflictText));
                OnPropertyChanged(nameof(SelectedModFileCountText));
                OnPropertyChanged(nameof(HasSelectedModFileTree));
                RaiseModCommandStateChanged();
                _ = LoadSelectedModRootFileTreeAsync();
            }
        }
    }

    public PluginEntry? SelectedPlugin
    {
        get => selectedPlugin;
        set
        {
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
            if (SetField(ref selectedDownload, value))
            {
                OnPropertyChanged(nameof(SelectedDownloadTitle));
                OnPropertyChanged(nameof(SelectedDownloadStatusText));
                OnPropertyChanged(nameof(SelectedDownloadSourceText));
                (InstallSelectedDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
                (DeleteSelectedDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
                (CancelDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
                (ResumeDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
                (OpenDownloadInExplorerCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
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
        set => SetField(ref selectedWorkspaceTabIndex, value);
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

    public bool IsLoadingModFiles
    {
        get => isLoadingModFiles;
        private set => SetField(ref isLoadingModFiles, value);
    }

    public bool IsBuildLoadingSplashVisible
    {
        get => isBuildLoadingSplashVisible;
        private set => SetField(ref isBuildLoadingSplashVisible, value);
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

    public BuildDeletionProcessViewModel BuildDeletionProcess => buildDeletionProcess;

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
    public bool HasPlugins => Plugins.Count > 0;
    public bool HasDownloads => Downloads.Count > 0;
    public bool HasSelectedModFileTree => SelectedModFileTree.Count > 0;
    public bool HasTemplates => AvailableTemplates.Count > 0;
    public bool HasResolvedTemplate => SelectedResolvedTemplate is not null;
    public bool IsNameStep => CreateProjectStepIndex == 0;
    public bool IsGameStep => CreateProjectStepIndex == 1;
    public bool IsInstallStep => CreateProjectStepIndex == 2;

    public string ProjectCountText => FormatBuildCount(Projects.Count);

    public string ModCountText => InstalledModCount == 0
        ? T("Моды не установлены")
        : F("{0} {1}", InstalledModCount, FormatModWord(InstalledModCount));

    public string PluginCountText => InstalledPluginCount == 0
        ? T("Плагины не найдены")
        : F("{0} {1}", InstalledPluginCount, FormatPluginWord(InstalledPluginCount));

    public string DownloadCountText => Downloads.Count == 0
        ? T("Загрузок пока нет")
        : F("{0} {1}", Downloads.Count, FormatDownloadWord(Downloads.Count));

    public bool CanMoveModOrderItem(ModEntry? mod)
    {
        return IsProjectWorkspaceOpen &&
            HasSelectedProject &&
            mod is not null &&
            Mods.Count > 1 &&
            !IsProcessingDownload &&
            !IsCheckingModUpdates;
    }

    public bool CanMovePluginOrderItem(PluginEntry? plugin)
    {
        if (!IsProjectWorkspaceOpen ||
            !HasSelectedProject ||
            plugin is not { CanMove: true } ||
            Plugins.Count <= 1 ||
            IsProcessingPlugins)
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
        0 => T("Название сборки"),
        1 => T("Игра и шаблон"),
        _ => T("Папка установки")
    };

    public string CreateProjectStepSubtitle => CreateProjectStepIndex switch
    {
        0 => T("Назовите проект так, как он будет лежать на диске."),
        1 => T("Выберите игру: её шаблон наложится на базовый и задаст модули сборки."),
        _ => T("Fluxora развернёт разрешённый шаблон в выбранном каталоге.")
    };

    public string CreateProjectStepCounter => F("{0} из 3", CreateProjectStepIndex + 1);

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

            return SelectedProject.CreatedAt.LocalDateTime.ToString("dd.MM.yyyy HH:mm");
        }
    }

    public string SelectedProjectConfigText => SelectedProject?.ConfigPath ?? string.Empty;

    public string SelectedProjectExecutablesText => SelectedProject?.Executables is { Count: > 0 } executables
        ? string.Join("  ·  ", executables.Select(executable => executable.DisplayTitle))
        : string.Empty;

    public string SelectedProjectPluginExtensionsText => SelectedProject?.Template?.PluginExtensionsText ?? string.Empty;

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
        RaiseProjectCollectionStateChanged();

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
            }
            else
            {
                await downloadCatalogService.CaptureNxmLinksAsync(null, links, cancellationToken);
                ActivityMessage = "NXM-ссылка сохранена. Откройте или создайте сборку, чтобы перенести её в загрузки.";
            }
        }
        catch (Exception exception)
        {
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

        try
        {
            IsOpeningProject = true;
            ValidationMessage = string.Empty;
            ActivityMessage = $"Переименовываю сборку: {project.Name}...";

            bool wasSelected = IsSameProject(SelectedProject, project);
            ModProject renamedProject = await projectCatalogService.RenameProjectAsync(project, newName);
            ReplaceProject(project, renamedProject);

            if (wasSelected)
            {
                SelectedProject = renamedProject;
                if (IsProjectWorkspaceOpen)
                {
                    await OpenProjectWorkspaceAsync(renamedProject, importPendingDownloads: false);
                }
            }

            ActivityMessage = $"Сборка переименована: {renamedProject.Name}";
        }
        catch (Exception exception)
        {
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

            RemoveProject(project);

            if (wasSelected)
            {
                IsProjectWorkspaceOpen = false;
                ClearWorkspaceData();
                SelectedProject = Projects.FirstOrDefault();
            }

            ActivityMessage = $"Сборка удалена: {project.Name}";
        }
        catch (Exception exception)
        {
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

            IsCreatingProject = true;
            ActivityMessage = "Создаю структуру сборки из шаблона...";

            ModProject project = await projectCatalogService.CreateProjectAsync(
                ProjectName,
                template,
                GamePath,
                InstallRootDirectory);

            AddOrReplaceProject(project);
            SelectedProject = project;
            IsProjectWorkspaceOpen = false;
            IsCreateProjectPanelOpen = false;
            ActivityMessage = $"Сборка создана: {project.ProjectDirectory}. Данные: {project.ConfigPath}";
        }
        catch (Exception exception)
        {
            ValidationMessage = $"Не удалось создать сборку: {exception.Message}";
            ActivityMessage = string.Empty;
        }
        finally
        {
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

            if (string.IsNullOrWhiteSpace(GamePath))
            {
                ValidationMessage = "Выберите .exe игры.";
                return false;
            }
        }

        if (CreateProjectStepIndex == 2)
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

    private void CloseBuildDeletionProcess()
    {
        BuildDeletionProcess.Close();
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

    private void BackToProjects()
    {
        CancelWorkspaceLoad();
        IsProjectWorkspaceOpen = false;
        ClearWorkspaceData();
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
        OnPropertyChanged(nameof(HasMods));
        OnPropertyChanged(nameof(HasModOrderItems));
        OnPropertyChanged(nameof(ModCountText));
        RaiseModCommandStateChanged();
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
        string defaultProfile = project.Template?.DefaultProfile ?? string.Empty;
        AvailableProfiles.Add(string.IsNullOrWhiteSpace(defaultProfile) ? "Default" : defaultProfile);
        SelectedProfile = AvailableProfiles.FirstOrDefault() ?? string.Empty;

        SyncExecutableMenu(project.Executables, lastSelectedExecutableId);
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
        ActivityMessage = "Пути сборки сохранены. Обновляю рабочую область...";

        PrepareWorkspaceOptions(SelectedProject);
        await StartWorkspaceLoad(SelectedProject, importPendingDownloads: false);
        if (SelectedProject is not null)
        {
            ActivityMessage = "Пути сборки применены.";
        }
    }

    private async Task LoadModsFromProjectAsync(ModProject project)
    {
        try
        {
            string? selectedModId = SelectedMod is null ? null : ModListItemKey(SelectedMod);
            IReadOnlyList<ModEntry> mods = await modCatalogService.GetInstalledModsAsync(project, SelectedProfile);
            SyncMods(mods, selectedModId);
            OnPropertyChanged(nameof(HasMods));
            OnPropertyChanged(nameof(HasModOrderItems));
            OnPropertyChanged(nameof(ModCountText));
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
            OnPropertyChanged(nameof(HasMods));
            OnPropertyChanged(nameof(HasModOrderItems));
            OnPropertyChanged(nameof(ModCountText));
            RaiseModCommandStateChanged();
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
                OnPropertyChanged(nameof(HasMods));
                OnPropertyChanged(nameof(HasModOrderItems));
                OnPropertyChanged(nameof(ModCountText));
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
    }

    private void SyncDownloads(IReadOnlyList<DownloadEntry> downloads, string? selectedDownloadId)
    {
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

        SelectedDownload = !string.IsNullOrWhiteSpace(selectedDownloadId)
            ? Downloads.FirstOrDefault(download => IsSamePath(download.Id, selectedDownloadId)) ?? Downloads.FirstOrDefault()
            : Downloads.FirstOrDefault();
    }

    private void SyncMods(IReadOnlyList<ModEntry> mods, string? selectedModId)
    {
        OrderedCollectionSyncService.Sync(
            Mods,
            mods,
            ModListItemKey,
            AreEquivalentModEntries);

        modCollapseService.Apply(Mods);

        SelectedMod = !string.IsNullOrWhiteSpace(selectedModId)
            ? Mods.FirstOrDefault(mod => IsSameModListItem(mod, selectedModId)) ?? Mods.FirstOrDefault()
            : Mods.FirstOrDefault();
    }

    private void SyncPlugins(IReadOnlyList<PluginEntry> plugins, string? selectedPluginId)
    {
        OrderedCollectionSyncService.Sync(
            Plugins,
            plugins,
            plugin => plugin.Id,
            AreEquivalentPluginEntries);

        pluginCollapseService.Apply(Plugins);

        SelectedPlugin = !string.IsNullOrWhiteSpace(selectedPluginId)
            ? Plugins.FirstOrDefault(plugin => IsSamePlugin(plugin.Id, selectedPluginId)) ?? Plugins.FirstOrDefault()
            : Plugins.FirstOrDefault();
    }

    private void ToggleModSeparator(ModEntry? separator)
    {
        if (separator is not { IsSeparator: true })
        {
            return;
        }

        modCollapseService.Toggle(separator);
        modCollapseService.Apply(Mods);
    }

    private void TogglePluginSeparator(PluginEntry? separator)
    {
        if (separator is not { IsSeparator: true })
        {
            return;
        }

        pluginCollapseService.Toggle(separator);
        pluginCollapseService.Apply(Plugins);
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

        try
        {
            IsProcessingDownload = true;
            DownloadEntry importedDownload = await downloadCatalogService.ImportLocalFileAsync(SelectedProject, selectedPath);
            await LoadDownloadsFromProjectAsync(SelectedProject);
            SelectedDownload = Downloads.FirstOrDefault(download => IsSamePath(download.Id, importedDownload.Id)) ?? SelectedDownload;
            ActivityMessage = "Файл добавлен в загрузки.";
        }
        catch (Exception exception)
        {
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

        int targetIndex = SelectedMod is null
            ? Mods.Count
            : Math.Min(Mods.Count, SelectedMod.Order + 1);

        try
        {
            IsProcessingDownload = true;
            IReadOnlyList<ModEntry> mods = await modCatalogService.CreateModSeparatorAsync(
                SelectedProject,
                SelectedProfile,
                separatorName,
                targetIndex);
            SyncMods(mods, null);
            SelectedMod = Mods.FirstOrDefault(mod =>
                mod.IsSeparator &&
                mod.Order == targetIndex &&
                string.Equals(mod.DisplayName, separatorName, StringComparison.OrdinalIgnoreCase)) ??
                Mods.ElementAtOrDefault(targetIndex) ??
                SelectedMod;
            OnPropertyChanged(nameof(HasMods));
            OnPropertyChanged(nameof(HasModOrderItems));
            OnPropertyChanged(nameof(ModCountText));
            RaiseModCommandStateChanged();
            ActivityMessage = $"Разделитель создан: {separatorName}";
        }
        catch (Exception exception)
        {
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
        int clampedInsertionIndex = Math.Clamp(insertionIndex, 0, Mods.Count);
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

        try
        {
            IsProcessingDownload = true;
            MoveRange(Mods, sourceIndex, spanLength, destinationIndex);
            modCollapseService.Apply(Mods);

            IReadOnlyList<ModEntry> mods = await modCatalogService.MoveModOrderItemAsync(
                SelectedProject,
                SelectedProfile,
                mod,
                clampedTargetIndex);
            SyncMods(mods, ModListItemKey(mod));
            OnPropertyChanged(nameof(HasMods));
            OnPropertyChanged(nameof(HasModOrderItems));
            OnPropertyChanged(nameof(ModCountText));
            RaiseModCommandStateChanged();
            ActivityMessage = mod.IsSeparator
                ? $"Разделитель перемещён: {mod.DisplayName}"
                : $"Порядок модов обновлён: {mod.DisplayName}";
        }
        catch (Exception exception)
        {
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
        if (SelectedProject is null)
        {
            return;
        }

        string? separatorName = modInstallDialogService.PickSeparatorName("Новый разделитель");
        if (string.IsNullOrWhiteSpace(separatorName))
        {
            return;
        }

        int targetIndex = SelectedPlugin is null
            ? Plugins.Count
            : Math.Min(Plugins.Count, SelectedPlugin.Order + 1);

        try
        {
            IsProcessingPlugins = true;
            IReadOnlyList<PluginEntry> plugins = await pluginCatalogService.CreatePluginSeparatorAsync(
                SelectedProject,
                SelectedProfile,
                separatorName,
                targetIndex);
            SyncPlugins(plugins, null);
            SelectedPlugin = Plugins.FirstOrDefault(plugin =>
                plugin.IsSeparator &&
                plugin.Order >= Math.Min(targetIndex, Math.Max(0, Plugins.Count - 1)) &&
                string.Equals(plugin.DisplayName, separatorName, StringComparison.OrdinalIgnoreCase)) ??
                Plugins.ElementAtOrDefault(Math.Min(targetIndex, Math.Max(0, Plugins.Count - 1))) ??
                SelectedPlugin;
            OnPropertyChanged(nameof(HasPlugins));
            OnPropertyChanged(nameof(PluginCountText));
            RaisePluginCommandStateChanged();
            ActivityMessage = $"Разделитель плагинов создан: {separatorName}";
        }
        catch (Exception exception)
        {
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
            ActivityMessage = plugin.IsSeparator
                ? $"Разделитель плагинов перемещён: {plugin.DisplayName}"
                : $"Порядок плагинов обновлён: {plugin.Name}";
        }
        catch (Exception exception)
        {
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

    private async void DeleteSelectedPlugin(PluginEntry? plugin)
    {
        if (SelectedProject is null)
        {
            return;
        }

        PluginEntry? pluginToDelete = plugin ?? SelectedPlugin;
        if (pluginToDelete is not { IsSeparator: true })
        {
            return;
        }

        int fallbackIndex = Math.Max(0, pluginToDelete.Order - 1);
        try
        {
            IsProcessingPlugins = true;
            IReadOnlyList<PluginEntry> plugins = await pluginCatalogService.DeletePluginSeparatorAsync(
                SelectedProject,
                SelectedProfile,
                pluginToDelete);
            SyncPlugins(plugins, null);
            SelectedPlugin = Plugins.ElementAtOrDefault(Math.Min(fallbackIndex, Math.Max(0, Plugins.Count - 1)));
            OnPropertyChanged(nameof(HasPlugins));
            OnPropertyChanged(nameof(PluginCountText));
            RaisePluginCommandStateChanged();
            ActivityMessage = $"Разделитель плагинов удалён: {pluginToDelete.DisplayName}";
        }
        catch (Exception exception)
        {
            ActivityMessage = $"Не удалось удалить разделитель плагинов: {exception.Message}";
            await LoadPluginsFromProjectAsync(SelectedProject);
        }
        finally
        {
            IsProcessingPlugins = false;
        }
    }

    private async void TogglePluginEnabled(PluginEntry? plugin)
    {
        if (SelectedProject is null || plugin is not { IsPlugin: true })
        {
            return;
        }

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
            ActivityMessage = plugin.IsEnabled
                ? $"Плагин включён: {plugin.Name}"
                : $"Плагин отключён: {plugin.Name}";
        }
        catch (Exception exception)
        {
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

    private bool CanSetSelectedModEnabled(ModEntry? mod, bool isEnabled)
    {
        ModEntry? target = mod ?? SelectedMod;
        return IsProjectWorkspaceOpen &&
            HasSelectedProject &&
            target is not null &&
            target.IsMod &&
            target.IsEnabled != isEnabled &&
            !IsProcessingDownload;
    }

    private async void SetSelectedModEnabled(ModEntry? mod, bool isEnabled)
    {
        if (SelectedProject is null)
        {
            return;
        }

        ModEntry? modToUpdate = mod ?? SelectedMod;
        if (modToUpdate is not { IsMod: true })
        {
            return;
        }

        SelectedMod = modToUpdate;

        try
        {
            IsProcessingDownload = true;
            await modCatalogService.SetInstalledModEnabledAsync(SelectedProject, modToUpdate, isEnabled);
            await LoadModsFromProjectAsync(SelectedProject);
            await LoadPluginsFromProjectAsync(SelectedProject);
            ActivityMessage = isEnabled
                ? $"Мод включён: {modToUpdate.Name}"
                : $"Мод выключен: {modToUpdate.Name}";
        }
        catch (Exception exception)
        {
            ActivityMessage = isEnabled
                ? $"Не удалось включить мод: {exception.Message}"
                : $"Не удалось выключить мод: {exception.Message}";
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

        try
        {
            IsProcessingDownload = true;
            await modCatalogService.SetAllInstalledModsEnabledAsync(SelectedProject, isEnabled);
            await LoadModsFromProjectAsync(SelectedProject);
            await LoadPluginsFromProjectAsync(SelectedProject);
            ActivityMessage = isEnabled
                ? "Все моды включены."
                : "Все моды выключены.";
        }
        catch (Exception exception)
        {
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

        ModEntry? modToDelete = mod ?? SelectedMod;
        if (modToDelete is null)
        {
            return;
        }

        try
        {
            IsProcessingDownload = true;
            if (modToDelete.IsSeparator)
            {
                IReadOnlyList<ModEntry> mods = await modCatalogService.DeleteModSeparatorAsync(
                    SelectedProject,
                    SelectedProfile,
                    modToDelete);
                SyncMods(mods, null);
                OnPropertyChanged(nameof(HasMods));
                OnPropertyChanged(nameof(HasModOrderItems));
                OnPropertyChanged(nameof(ModCountText));
                ActivityMessage = $"Разделитель удалён: {modToDelete.DisplayName}";
            }
            else
            {
                await modCatalogService.DeleteInstalledModAsync(SelectedProject, modToDelete);
                await LoadModsFromProjectAsync(SelectedProject);
                await LoadPluginsFromProjectAsync(SelectedProject);
                ActivityMessage = $"Мод удалён: {modToDelete.Name}";
            }
        }
        catch (Exception exception)
        {
            ActivityMessage = modToDelete.IsSeparator
                ? $"Не удалось удалить разделитель: {exception.Message}"
                : $"Не удалось удалить мод: {exception.Message}";
        }
        finally
        {
            IsProcessingDownload = false;
        }
    }

    private async void InstallSelectedDownload(DownloadEntry? download)
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

        string? modName = modInstallDialogService.PickModName(downloadToInstall.Name);
        if (string.IsNullOrWhiteSpace(modName))
        {
            return;
        }

        try
        {
            IsProcessingDownload = true;
            ModEntry installedMod = await downloadCatalogService.InstallDownloadAsync(
                SelectedProject,
                downloadToInstall,
                modName);
            await LoadModsFromProjectAsync(SelectedProject);
            await LoadPluginsFromProjectAsync(SelectedProject);
            await LoadDownloadsFromProjectAsync(SelectedProject);
            SelectedMod = Mods.FirstOrDefault(mod => IsSamePath(mod.Id, installedMod.Id)) ?? Mods.FirstOrDefault();
            ActivityMessage = $"Мод установлен: {installedMod.Name}";
        }
        catch (Exception exception)
        {
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

        DownloadEntry? downloadToDelete = download ?? SelectedDownload;
        if (downloadToDelete is null)
        {
            return;
        }

        SelectedDownload = downloadToDelete;

        try
        {
            IsProcessingDownload = true;
            await downloadCatalogService.DeleteDownloadAsync(SelectedProject, downloadToDelete);
            await LoadDownloadsFromProjectAsync(SelectedProject);
            ActivityMessage = $"Загрузка удалена: {downloadToDelete.FileName}";
        }
        catch (Exception exception)
        {
            ActivityMessage = $"Не удалось удалить загрузку: {exception.Message}";
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

        try
        {
            await downloadCatalogService.CancelDownloadAsync(SelectedProject, downloadToCancel);
            await LoadDownloadsFromProjectAsync(SelectedProject);
            ActivityMessage = $"Отмена скачивания: {downloadToCancel.Name}";
        }
        catch (Exception exception)
        {
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

        try
        {
            IsProcessingDownload = true;
            Task<DownloadEntry> resumeTask = downloadCatalogService.ResumeDownloadAsync(SelectedProject, downloadToResume);
            await RunDownloadOperationWithLiveRefreshAsync(SelectedProject, resumeTask);
            DownloadEntry resumedDownload = await resumeTask;
            ActivityMessage = resumedDownload.CanInstall
                ? $"Скачивание завершено: {resumedDownload.Name}"
                : $"Скачивание приостановлено: {resumedDownload.Name}";
        }
        catch (Exception exception)
        {
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

        try
        {
            GameExecutableLaunchResult result = await coreBridgeService.LaunchGameExecutableAsync(
                SelectedProject.ConfigPath,
                executable.Id);
            ActivityMessage = $"Запуск: {result.DisplayName}";
        }
        catch (Exception exception)
        {
            ActivityMessage = $"Не удалось запустить executable: {exception.Message}";
        }
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
            IsProcessingPlugins)
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
        (OpenProjectCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (OpenProjectBuildCommand as RelayCommand<ModProject>)?.RaiseCanExecuteChanged();
        (RenameProjectCommand as RelayCommand<ModProject>)?.RaiseCanExecuteChanged();
        (DeleteProjectCommand as RelayCommand<ModProject>)?.RaiseCanExecuteChanged();
        RaiseWorkspaceCommandStateChanged();
    }

    private void RaiseWorkspaceStateChanged()
    {
        OnPropertyChanged(nameof(IsProjectWorkspaceOpen));
        OnPropertyChanged(nameof(IsHomeViewOpen));
        OnPropertyChanged(nameof(IsModlistViewOpen));
        OnPropertyChanged(nameof(WorkspaceTitle));
        OnPropertyChanged(nameof(WorkspaceSubtitle));
        RaiseWorkspaceCommandStateChanged();
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
        (OpenModsDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (OpenProfilesDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (OpenDownloadsDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (AddDownloadFileCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (CheckModUpdatesCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (CreateModSeparatorCommand as RelayCommand)?.RaiseCanExecuteChanged();
        RaiseModCommandStateChanged();
        RaisePluginCommandStateChanged();
        (InstallSelectedDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
        (DeleteSelectedDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
        (CancelDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
        (ResumeDownloadCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
        (OpenDownloadInExplorerCommand as RelayCommand<DownloadEntry>)?.RaiseCanExecuteChanged();
        (LaunchSelectedExecutableCommand as RelayCommand)?.RaiseCanExecuteChanged();
    }

    private void RaiseModCommandStateChanged()
    {
        (MoveSelectedModUpCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (MoveSelectedModDownCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (DeleteSelectedModCommand as RelayCommand<ModEntry>)?.RaiseCanExecuteChanged();
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
        (TogglePluginEnabledCommand as RelayCommand<PluginEntry>)?.RaiseCanExecuteChanged();
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
        Plugins.Clear();
        Downloads.Clear();
        SelectedModFileTree.Clear();
        AvailableProfiles.Clear();
        AvailableExecutables.Clear();
        SelectedMod = null;
        SelectedPlugin = null;
        SelectedDownload = null;
        SelectedExecutable = null;
        OnPropertyChanged(nameof(HasMods));
        OnPropertyChanged(nameof(HasModOrderItems));
        OnPropertyChanged(nameof(HasPlugins));
        OnPropertyChanged(nameof(HasDownloads));
        OnPropertyChanged(nameof(HasSelectedModFileTree));
        OnPropertyChanged(nameof(ModCountText));
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

    private static string ModListItemKey(ModEntry mod)
    {
        return string.IsNullOrWhiteSpace(mod.OrderId) ? mod.Id : mod.OrderId;
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
        OnPropertyChanged(nameof(PluginCountText));
        OnPropertyChanged(nameof(DownloadCountText));
        OnPropertyChanged(nameof(SelectedProjectProfileFilesText));
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
}
