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

public sealed class SettingsWindowViewModel : INotifyPropertyChanged, IDisposable
{
    private const string ConnectionsSection = "connections";
    private const string LanguagesSection = "languages";
    private const string TransferSection = "transfer";
    private static readonly TimeSpan LanguageSwitchMinimumDuration = TimeSpan.FromMilliseconds(520);

    private readonly CoreBridgeService coreBridgeService;
    private readonly SettingsService settingsService;
    private readonly LanguageCatalogService languageCatalogService;
    private readonly IFolderPickerService folderPickerService;
    private readonly ModProject? currentProject;
    private readonly bool replaceCurrentProject;

    private string selectedSection = ConnectionsSection;
    private bool isNexusBusy;
    private bool isNexusModsConnectionPending;
    private bool isNexusModsLinked;
    private string nexusModsTitle = "Nexus Mods";
    private string nexusModsMessage = "Проверяю привязку...";
    private string nexusModsDetails = string.Empty;
    private LanguageOption? selectedLanguage;
    private bool isLoadingLanguages;
    private bool isLanguageSwitching;
    private string languageStatus = "Выберите язык интерфейса. Будет загружен только выбранный конфиг.";
    private string languageError = string.Empty;

    private TransferDriveOption? selectedDrive;
    private string sourceDirectory = string.Empty;
    private string validatedSourceDirectory = string.Empty;
    private string destinationRootDirectory = string.Empty;
    private ModOrganizerImportAnalysis? transferAnalysis;
    private bool isTransferStepperOpen;
    private int transferStepIndex;
    private bool isAnalyzingTransfer;
    private bool isTransferRunning;
    private bool isTransferCompleted;
    private string transferMessage = "Выберите источник переноса сборки.";
    private string transferError = string.Empty;
    private int transferOverallPercent;
    private int transferCopyPercent;
    private int transferDatabasePercent;
    private string transferCurrentStep = "Ожидание";
    private string transferCurrentItem = string.Empty;
    private string transferBytesText = string.Empty;
    private int transferAnalysisRequestId;
    private CancellationTokenSource? transferAnalysisCancellation;
    private ulong requiredTransferBytes;
    private ModProject? importedProject;

    public SettingsWindowViewModel(
        CoreBridgeService coreBridgeService,
        SettingsService settingsService,
        LanguageCatalogService languageCatalogService,
        IFolderPickerService folderPickerService,
        ModProject? currentProject,
        bool replaceCurrentProject)
    {
        this.coreBridgeService = coreBridgeService;
        this.settingsService = settingsService;
        this.languageCatalogService = languageCatalogService;
        this.folderPickerService = folderPickerService;
        this.currentProject = currentProject;
        this.replaceCurrentProject = replaceCurrentProject && currentProject is not null;

        SelectConnectionsCommand = new RelayCommand(() => SelectSection(ConnectionsSection), () => !IsTransferProcessVisible);
        SelectLanguagesCommand = new RelayCommand(() => SelectSection(LanguagesSection), () => !IsTransferProcessVisible);
        SelectTransferCommand = new RelayCommand(() => SelectSection(TransferSection), () => !IsTransferProcessVisible);
        ToggleNexusModsCommand = new AsyncRelayCommand(ToggleNexusModsAsync, () => !IsBusy);
        OpenModOrganizerTransferCommand = new RelayCommand(OpenModOrganizerTransfer, () => !IsBusy);
        CancelTransferFlowCommand = new RelayCommand(CancelTransferFlow, () => !IsBusy && IsTransferStepperOpen);
        SelectTransferStepCommand = new RelayCommand<string>(SelectTransferStep, _ => !IsBusy && IsTransferStepperOpen);
        PreviousTransferStepCommand = new RelayCommand(MoveToPreviousTransferStep, () => CanMoveToPreviousTransferStep);
        NextTransferStepCommand = new AsyncRelayCommand(MoveToNextTransferStepAsync, () => CanMoveToNextTransferStep);
        BrowseSourceCommand = new AsyncRelayCommand(BrowseSourceAsync, () => !IsBusy);
        BrowseDestinationCommand = new AsyncRelayCommand(BrowseDestinationAsync, () => CanChangeDestination);
        StartTransferCommand = new AsyncRelayCommand(StartTransferAsync, () => CanStartTransfer);

        LocalizationManager.Current.LanguageChanged += OnLocalizationChanged;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public ObservableCollection<TransferDriveOption> AvailableDrives { get; } = new();

    public ObservableCollection<LanguageOption> AvailableLanguages { get; } = new();

    public ICommand SelectConnectionsCommand { get; }
    public ICommand SelectLanguagesCommand { get; }
    public ICommand SelectTransferCommand { get; }
    public ICommand ToggleNexusModsCommand { get; }
    public ICommand OpenModOrganizerTransferCommand { get; }
    public ICommand CancelTransferFlowCommand { get; }
    public ICommand SelectTransferStepCommand { get; }
    public ICommand PreviousTransferStepCommand { get; }
    public ICommand NextTransferStepCommand { get; }
    public ICommand BrowseSourceCommand { get; }
    public ICommand BrowseDestinationCommand { get; }
    public ICommand StartTransferCommand { get; }

    public ModProject? ImportedProject
    {
        get => importedProject;
        private set => SetField(ref importedProject, value);
    }

    public bool IsBusy => IsNexusBusy || IsAnalyzingTransfer || IsTransferRunning || IsLanguageSwitching;

    public bool IsNormalSettingsVisible => !IsTransferProcessVisible;

    public bool IsTransferProcessVisible => IsTransferRunning || IsTransferCompleted;

    public bool IsConnectionsSelected => selectedSection == ConnectionsSection;

    public bool IsLanguagesSelected => selectedSection == LanguagesSection;

    public bool IsTransferSelected => selectedSection == TransferSection;

    public bool IsTransferMenuVisible => IsTransferSelected && !IsTransferStepperOpen;

    public bool IsTransferStepperVisible => IsTransferSelected && IsTransferStepperOpen;

    public bool IsNexusBusy
    {
        get => isNexusBusy;
        private set
        {
            if (SetField(ref isNexusBusy, value))
            {
                RaiseBusyStateChanged();
                OnPropertyChanged(nameof(NexusModsToggleText));
                OnPropertyChanged(nameof(IsNexusModsToggleEnabled));
                (ToggleNexusModsCommand as AsyncRelayCommand)?.RaiseCanExecuteChanged();
            }
        }
    }

    public bool IsNexusModsToggleEnabled => !IsBusy;

    public bool IsNexusModsToggleChecked => IsNexusModsLinked || isNexusModsConnectionPending;

    public bool IsNexusModsLinked
    {
        get => isNexusModsLinked;
        set
        {
            if (SetField(ref isNexusModsLinked, value))
            {
                OnPropertyChanged(nameof(NexusModsToggleText));
                OnPropertyChanged(nameof(IsNexusModsToggleChecked));
            }
        }
    }

    public string NexusModsTitle
    {
        get => nexusModsTitle;
        private set => SetField(ref nexusModsTitle, value);
    }

    public string NexusModsMessage
    {
        get => T(nexusModsMessage);
        private set => SetField(ref nexusModsMessage, value);
    }

    public string NexusModsDetails
    {
        get => nexusModsDetails;
        private set => SetField(ref nexusModsDetails, value);
    }

    public string NexusModsToggleText => IsNexusBusy ? T("Ожидание") : IsNexusModsLinked ? T("Вкл") : T("Выкл");

    public LanguageOption? SelectedLanguage
    {
        get => selectedLanguage;
        set
        {
            if (SetField(ref selectedLanguage, value) &&
                value is not null &&
                !isLoadingLanguages)
            {
                OnPropertyChanged(nameof(SelectedLanguageText));
                _ = ChangeLanguageAsync(value);
            }
        }
    }

    public bool IsLanguageSwitching
    {
        get => isLanguageSwitching;
        private set
        {
            if (SetField(ref isLanguageSwitching, value))
            {
                RaiseBusyStateChanged();
                OnPropertyChanged(nameof(LanguageSwitchSubtitle));
            }
        }
    }

    public bool HasLanguageError => !string.IsNullOrWhiteSpace(LanguageError);

    public string LanguageStatus
    {
        get => T(languageStatus);
        private set => SetField(ref languageStatus, value);
    }

    public string LanguageError
    {
        get => T(languageError);
        private set
        {
            if (SetField(ref languageError, value))
            {
                OnPropertyChanged(nameof(HasLanguageError));
            }
        }
    }

    public string LanguageDirectoryText => languageCatalogService.LanguageDirectory;

    public string SelectedLanguageText => SelectedLanguage is null
        ? T("Язык не выбран")
        : F("Текущий язык: {0}", SelectedLanguage.DisplayName);

    public string LanguageSwitchTitle => T("Меняем язык");

    public string LanguageSwitchSubtitle => SelectedLanguage is null
        ? T("Загружаю конфиг интерфейса...")
        : F("Загружаю {0}", SelectedLanguage.DisplayName);

    public TransferDriveOption? SelectedDrive
    {
        get => selectedDrive;
        set
        {
            if (SetField(ref selectedDrive, value))
            {
                string nextDestinationRoot = value is null
                    ? string.Empty
                    : Path.Combine(value.RootDirectory, "Fluxora Builds");
                if (IsSamePath(DestinationRootDirectory, nextDestinationRoot))
                {
                    InvalidateTransferAnalysis();
                }
                else
                {
                    DestinationRootDirectory = nextDestinationRoot;
                }

                RaiseTransferStepCompletionStateChanged();
                RaiseTransferCommandStateChanged();
            }
        }
    }

    public string SourceDirectory
    {
        get => sourceDirectory;
        set
        {
            if (SetField(ref sourceDirectory, value))
            {
                validatedSourceDirectory = string.Empty;
                UpdateDriveRequirements(0);
                InvalidateTransferAnalysis();
                RaiseTransferStepCompletionStateChanged();
                RaiseTransferCommandStateChanged();
            }
        }
    }

    public string DestinationRootDirectory
    {
        get => destinationRootDirectory;
        set
        {
            if (SetField(ref destinationRootDirectory, value))
            {
                OnPropertyChanged(nameof(TransferTargetText));
                InvalidateTransferAnalysis();
                RaiseTransferStepCompletionStateChanged();
                RaiseTransferCommandStateChanged();
            }
        }
    }

    public ModOrganizerImportAnalysis? TransferAnalysis
    {
        get => transferAnalysis;
        private set
        {
            if (SetField(ref transferAnalysis, value))
            {
                if (value is not null)
                {
                    UpdateDriveRequirements(value.TotalBytes);
                }

                OnPropertyChanged(nameof(HasTransferAnalysis));
                OnPropertyChanged(nameof(TransferProjectName));
                OnPropertyChanged(nameof(TransferTargetText));
                OnPropertyChanged(nameof(TransferGameText));
                OnPropertyChanged(nameof(TransferProfileText));
                OnPropertyChanged(nameof(TransferModCountText));
                OnPropertyChanged(nameof(TransferSizeText));
                OnPropertyChanged(nameof(TransferAvailableText));
                OnPropertyChanged(nameof(TransferDiskSpaceText));
                OnPropertyChanged(nameof(TransferWarningText));
                OnPropertyChanged(nameof(HasTransferWarning));
                OnPropertyChanged(nameof(HasEnoughSpace));
                RaiseTransferStepCompletionStateChanged();
                RaiseTransferCommandStateChanged();
            }
        }
    }

    public bool IsTransferStepperOpen
    {
        get => isTransferStepperOpen;
        private set
        {
            if (SetField(ref isTransferStepperOpen, value))
            {
                RaiseTransferSurfaceStateChanged();
                RaiseTransferFlowCommandStateChanged();
            }
        }
    }

    public int TransferStepIndex
    {
        get => transferStepIndex;
        private set
        {
            int nextValue = Math.Clamp(value, 0, 2);
            if (SetField(ref transferStepIndex, nextValue))
            {
                RaiseTransferStepStateChanged();
                RaiseTransferFlowCommandStateChanged();
            }
        }
    }

    public bool IsAnalyzingTransfer
    {
        get => isAnalyzingTransfer;
        private set
        {
            if (SetField(ref isAnalyzingTransfer, value))
            {
                OnPropertyChanged(nameof(TransferReviewStepStatus));
                OnPropertyChanged(nameof(TransferSizeText));
                RaiseBusyStateChanged();
                RaiseTransferCommandStateChanged();
            }
        }
    }

    public bool IsTransferRunning
    {
        get => isTransferRunning;
        private set
        {
            if (SetField(ref isTransferRunning, value))
            {
                RaiseBusyStateChanged();
                RaiseProcessVisibilityChanged();
                RaiseTransferCommandStateChanged();
            }
        }
    }

    public bool IsTransferCompleted
    {
        get => isTransferCompleted;
        private set
        {
            if (SetField(ref isTransferCompleted, value))
            {
                RaiseProcessVisibilityChanged();
                OnPropertyChanged(nameof(TransferProcessTitle));
                OnPropertyChanged(nameof(TransferProcessSubtitle));
            }
        }
    }

    public string TransferMessage
    {
        get => T(transferMessage);
        private set => SetField(ref transferMessage, value);
    }

    public string TransferError
    {
        get => T(transferError);
        private set
        {
            if (SetField(ref transferError, value))
            {
                OnPropertyChanged(nameof(HasTransferError));
                RaiseTransferStepCompletionStateChanged();
                RaiseTransferCommandStateChanged();
            }
        }
    }

    public int TransferOverallPercent
    {
        get => transferOverallPercent;
        private set => SetField(ref transferOverallPercent, Math.Clamp(value, 0, 100));
    }

    public int TransferCopyPercent
    {
        get => transferCopyPercent;
        private set => SetField(ref transferCopyPercent, Math.Clamp(value, 0, 100));
    }

    public int TransferDatabasePercent
    {
        get => transferDatabasePercent;
        private set => SetField(ref transferDatabasePercent, Math.Clamp(value, 0, 100));
    }

    public string TransferCurrentStep
    {
        get => T(transferCurrentStep);
        private set => SetField(ref transferCurrentStep, value);
    }

    public string TransferCurrentItem
    {
        get => transferCurrentItem;
        private set => SetField(ref transferCurrentItem, value);
    }

    public string TransferBytesText
    {
        get => transferBytesText;
        private set => SetField(ref transferBytesText, value);
    }

    public bool HasTransferAnalysis => TransferAnalysis is not null;

    public bool HasTransferError => !string.IsNullOrWhiteSpace(TransferError);

    public bool HasTransferWarning => !string.IsNullOrWhiteSpace(TransferWarningText);

    public bool HasEnoughSpace => TransferAnalysis?.HasEnoughSpace == true;

    public bool IsReplaceMode => replaceCurrentProject;

    public bool IsTransferSourceComplete =>
        !string.IsNullOrWhiteSpace(SourceDirectory);

    public bool IsTransferDestinationComplete =>
        !string.IsNullOrWhiteSpace(DestinationRootDirectory) &&
        SelectedDrive?.IsSelectable == true;

    public bool IsTransferReviewComplete =>
        TransferAnalysis?.CanImport == true &&
        TransferAnalysis.HasEnoughSpace &&
        string.IsNullOrWhiteSpace(TransferError);

    public bool IsTransferReady =>
        IsTransferSourceComplete &&
        IsTransferDestinationComplete &&
        IsTransferReviewComplete;

    public string TransferSourceStepIcon => IsTransferSourceComplete ? "\uE73E" : "\uE711";

    public string TransferDestinationStepIcon => IsTransferDestinationComplete ? "\uE73E" : "\uE711";

    public string TransferReviewStepIcon => IsTransferReviewComplete ? "\uE73E" : "\uE711";

    public string TransferSourceStepStatus => IsTransferSourceComplete ? T("Готово") : T("Не заполнено");

    public string TransferDestinationStepStatus => IsTransferDestinationComplete ? T("Готово") : T("Не заполнено");

    public string TransferReviewStepStatus => TransferAnalysis switch
    {
        _ when IsAnalyzingTransfer => T("Проверка..."),
        { HasEnoughSpace: false } => T("Не хватает места"),
        { CanImport: false } => T("Нельзя перенести"),
        _ => IsTransferReviewComplete ? T("Готово") : T("Не заполнено")
    };

    public bool IsTransferSourceStep => TransferStepIndex == 0;

    public bool IsTransferDestinationStep => TransferStepIndex == 1;

    public bool IsTransferReviewStep => TransferStepIndex == 2;

    public bool CanMoveToPreviousTransferStep => !IsBusy && IsTransferStepperOpen && TransferStepIndex > 0;

    public bool CanMoveToNextTransferStep => !IsBusy && IsTransferStepperOpen && TransferStepIndex < 2;

    public bool CanChangeDestination => !IsBusy;

    public bool CanStartTransfer =>
        !IsBusy &&
        IsTransferReady;

    public string TransferModeTitle => IsReplaceMode ? T("Замена текущей сборки") : T("Новая сборка на Home");

    public string TransferModeSubtitle => IsReplaceMode
        ? T("Импорт заменит все данные открытой сборки. Текущая MO2-папка останется на месте.")
        : T("Импорт добавит отдельную сборку в список Fluxora. Исходная MO2-папка останется на месте.");

    public string TransferStepTitle => TransferStepIndex switch
    {
        0 => T("Источник Mod Organizer 2"),
        1 => T("Куда перенести сборку"),
        _ => T("Проверка и запуск")
    };

    public string TransferStepSubtitle => TransferStepIndex switch
    {
        0 => T("Укажите папку инстанса MO2. Проверка структуры останется на стороне C++ core."),
        1 => IsReplaceMode
            ? T("Перенос заменит открытую сборку Fluxora, исходная папка MO2 останется без изменений.")
            : T("Выберите диск и корневую папку, куда Fluxora положит новую сборку."),
        _ => T("Автопроверка подготовит размер, профиль и путь назначения перед запуском переноса.")
    };

    public string TransferStepCounter => F("{0} из 3", TransferStepIndex + 1);

    public string NextTransferStepText => TransferStepIndex == 1 ? T("К обзору") : T("Далее");

    public string DestinationLabel => IsReplaceMode ? T("Диск для текущей сборки Fluxora") : T("Диск и папка переноса");

    public string TransferProjectName => TransferAnalysis?.ProjectName ?? T("Сборка не выбрана");

    public string TransferTargetText => TransferAnalysis?.TargetProjectDirectory ?? DestinationRootDirectory;

    public string TransferGameText => TransferAnalysis is null
        ? T("Игра определится после анализа")
        : string.IsNullOrWhiteSpace(TransferAnalysis.GameName)
            ? TransferAnalysis.TemplateId
            : TransferAnalysis.GameName;

    public string TransferProfileText => TransferAnalysis?.ProfileName ?? T("Профиль определится после анализа");

    public string TransferModCountText => TransferAnalysis is null
        ? T("0 модов")
        : F("{0} модов, разделов MO2 проигнорировано: {1}", TransferAnalysis.ModCount, TransferAnalysis.SeparatorCount);

    public string TransferSizeText => TransferAnalysis is null
        ? (IsAnalyzingTransfer ? T("Проверяю...") : T("Проверка запустится автоматически"))
        : TransferDriveOption.FormatBytes(TransferAnalysis.TotalBytes);

    public string TransferAvailableText => TransferAnalysis is null
        ? SelectedDrive is null
            ? T("Выберите диск")
            : SelectedDrive.IsSelectable
                ? SelectedDrive.SpaceText
                : F("Недостаточно места: {0} доступно", TransferDriveOption.FormatBytes(SelectedDrive.AvailableBytes))
        : TransferAnalysis.HasEnoughSpace
            ? F("Места хватает: {0} доступно", TransferDriveOption.FormatBytes(TransferAnalysis.AvailableBytes))
            : F("Не хватает места: {0} доступно", TransferDriveOption.FormatBytes(TransferAnalysis.AvailableBytes));

    public string TransferDiskSpaceText => TransferAnalysis is null
        ? requiredTransferBytes > 0
            ? F("Выберите диск с минимум {0} свободного места.", TransferDriveOption.FormatBytes(requiredTransferBytes))
            : T("Автопроверка сравнит вес сборки со свободным местом на диске.")
        : TransferAnalysis.HasEnoughSpace
            ? F("Можно продолжить: нужно {0}, доступно {1}.", TransferDriveOption.FormatBytes(TransferAnalysis.TotalBytes), TransferDriveOption.FormatBytes(TransferAnalysis.AvailableBytes))
            : F("Нельзя продолжить: нужно {0}, доступно {1}.", TransferDriveOption.FormatBytes(TransferAnalysis.TotalBytes), TransferDriveOption.FormatBytes(TransferAnalysis.AvailableBytes));

    public string TransferWarningText => TransferAnalysis?.WarningMessage ?? string.Empty;

    public string StartTransferText => IsReplaceMode ? T("Заменить сборку") : T("Начать перенос");

    public string TransferProcessTitle => IsTransferCompleted ? T("Перенос завершен") : T("Перенос сборки");

    public string TransferProcessSubtitle => IsTransferCompleted
        ? T("Fluxora создала структуру сборки и перенесла metadata из MO2 в базу.")
        : T("Идет копирование файлов, перенос порядка модов и запись базы данных.");

    public Task InitializeAsync()
    {
        LoadLanguages();
        RefreshDrives();
        ApplyInitialDestination();
        ApplyStatus(coreBridgeService.GetNexusModsAuthStatus());
        TransferMessage = "Выберите источник переноса сборки.";
        return Task.CompletedTask;
    }

    public void Dispose()
    {
        LocalizationManager.Current.LanguageChanged -= OnLocalizationChanged;
    }

    private void SelectSection(string section)
    {
        if (selectedSection == section)
        {
            return;
        }

        selectedSection = section;
        OnPropertyChanged(nameof(IsConnectionsSelected));
        OnPropertyChanged(nameof(IsLanguagesSelected));
        OnPropertyChanged(nameof(IsTransferSelected));
        RaiseTransferSurfaceStateChanged();
    }

    private void LoadLanguages()
    {
        isLoadingLanguages = true;
        try
        {
            languageCatalogService.RefreshAvailableLanguages();
            AvailableLanguages.Clear();
            foreach (LanguageOption language in languageCatalogService.AvailableLanguages)
            {
                AvailableLanguages.Add(language);
            }

            SelectedLanguage = languageCatalogService.ResolveLanguage(LocalizationManager.Current.CurrentLanguageCode);
            LanguageStatus = "Выберите язык интерфейса. Будет загружен только выбранный конфиг.";
            LanguageError = string.Empty;
        }
        finally
        {
            isLoadingLanguages = false;
        }

        OnPropertyChanged(nameof(SelectedLanguageText));
        OnPropertyChanged(nameof(LanguageDirectoryText));
    }

    private async Task ChangeLanguageAsync(LanguageOption language)
    {
        if (IsLanguageSwitching)
        {
            return;
        }

        Stopwatch stopwatch = Stopwatch.StartNew();
        try
        {
            IsLanguageSwitching = true;
            LanguageError = string.Empty;
            LanguageStatus = "Загружаю выбранный язык...";

            await languageCatalogService.ChangeLanguageAsync(language);

            TimeSpan remaining = LanguageSwitchMinimumDuration - stopwatch.Elapsed;
            if (remaining > TimeSpan.Zero)
            {
                await Task.Delay(remaining);
            }

            LanguageStatus = "Язык интерфейса обновлен.";
        }
        catch (Exception exception)
        {
            LanguageError = F("Не удалось сменить язык: {0}", exception.Message);
            LanguageStatus = "Оставляю предыдущий язык интерфейса.";
        }
        finally
        {
            IsLanguageSwitching = false;
            OnPropertyChanged(nameof(SelectedLanguageText));
        }
    }

    private void OpenModOrganizerTransfer()
    {
        TransferError = string.Empty;
        TransferMessage = string.IsNullOrWhiteSpace(SourceDirectory)
            ? "Выберите папку Mod Organizer 2, затем перейдите к папке назначения."
            : "Продолжите настройку переноса из Mod Organizer 2.";
        TransferStepIndex = 0;
        IsTransferStepperOpen = true;
    }

    private void CancelTransferFlow()
    {
        if (IsBusy)
        {
            return;
        }

        TransferAnalysis = null;
        TransferError = string.Empty;
        TransferMessage = "Перенос отменен. Выберите источник, чтобы начать заново.";
        ResetProgress();
        TransferStepIndex = 0;
        IsTransferStepperOpen = false;
    }

    private void SelectTransferStep(string? step)
    {
        if (IsBusy || !IsTransferStepperOpen || !int.TryParse(step, out int stepIndex))
        {
            return;
        }

        TransferError = string.Empty;
        TransferStepIndex = stepIndex;
    }

    private void MoveToPreviousTransferStep()
    {
        if (!CanMoveToPreviousTransferStep)
        {
            return;
        }

        TransferError = string.Empty;
        TransferStepIndex -= 1;
    }

    private async Task MoveToNextTransferStepAsync()
    {
        if (!CanMoveToNextTransferStep)
        {
            return;
        }

        if (TransferStepIndex == 0)
        {
            if (!IsTransferSourceComplete)
            {
                if (string.IsNullOrWhiteSpace(SourceDirectory))
                {
                    TransferError = "Выберите папку Mod Organizer 2.";
                    return;
                }

                await EnsureFreshTransferAnalysisAsync();
                if (!IsTransferSourceComplete)
                {
                    return;
                }
            }

            TransferError = string.Empty;
            TransferMessage = "Источник выбран. Укажите, куда перенести сборку.";
            TransferStepIndex = 1;
            return;
        }

        if (TransferStepIndex == 1)
        {
            if (!IsTransferDestinationComplete)
            {
                TransferError = "Выберите папку назначения.";
                return;
            }

            await EnsureFreshTransferAnalysisAsync();
            if (TransferAnalysis is not null)
            {
                TransferStepIndex = 2;
            }
        }
    }

    private async Task ToggleNexusModsAsync()
    {
        bool wasLinked = IsNexusModsLinked;

        try
        {
            IsNexusBusy = true;
            if (!wasLinked)
            {
                IsNexusModsConnectionPending = true;
            }

            NexusModsMessage = wasLinked
                ? "Отвязываю NexusMods..."
                : "Открываю OAuth NexusMods. Завершите авторизацию в браузере.";

            NexusModsAuthStatus status = wasLinked
                ? await coreBridgeService.DisconnectNexusModsAsync()
                : await coreBridgeService.ConnectNexusModsAsync();

            ApplyStatus(status);
        }
        catch (Exception exception)
        {
            ApplyStatus(coreBridgeService.GetNexusModsAuthStatus());
            NexusModsMessage = exception.Message;
        }
        finally
        {
            IsNexusModsConnectionPending = false;
            IsNexusBusy = false;
        }
    }

    private async Task BrowseSourceAsync()
    {
        string? selectedPath = folderPickerService.PickFolder("Выберите папку Mod Organizer 2", SourceDirectory);
        if (string.IsNullOrWhiteSpace(selectedPath))
        {
            return;
        }

        SourceDirectory = selectedPath;
        if (IsTransferDestinationComplete)
        {
            await EnsureFreshTransferAnalysisAsync();
        }
    }

    private async Task BrowseDestinationAsync()
    {
        string? selectedPath = folderPickerService.PickFolder("Выберите папку для сборок Fluxora", DestinationRootDirectory);
        if (string.IsNullOrWhiteSpace(selectedPath))
        {
            return;
        }

        DestinationRootDirectory = selectedPath;
        SelectDriveForPath(selectedPath);
        if (!string.IsNullOrWhiteSpace(SourceDirectory))
        {
            await EnsureFreshTransferAnalysisAsync();
        }
    }

    private async Task EnsureFreshTransferAnalysisAsync()
    {
        if (TransferAnalysis is not null && IsTransferSourceComplete)
        {
            return;
        }

        transferAnalysisCancellation?.Cancel();
        int requestId = ++transferAnalysisRequestId;
        await RunTransferAnalysisAsync(requestId, CancellationToken.None);
    }

    private async Task RunAutomaticTransferAnalysisAsync(int requestId, CancellationToken cancellationToken)
    {
        try
        {
            await Task.Delay(350, cancellationToken);
            await RunTransferAnalysisAsync(requestId, cancellationToken);
        }
        catch (OperationCanceledException)
        {
        }
    }

    private async Task RunTransferAnalysisAsync(int requestId, CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(SourceDirectory) || !IsTransferDestinationComplete)
        {
            TransferError = "Выберите папку MO2 и диск назначения.";
            return;
        }

        string source = SourceDirectory;
        string destination = DestinationRootDirectory;
        string existingConfig = ExistingConfigPath;

        try
        {
            IsAnalyzingTransfer = true;
            TransferError = string.Empty;
            TransferMessage = "Автоматически проверяю папку MO2, размер и свободное место...";

            ModOrganizerImportAnalysis analysis = await coreBridgeService.AnalyzeModOrganizerInstanceAsync(
                source,
                destination,
                existingConfig,
                cancellationToken);

            cancellationToken.ThrowIfCancellationRequested();
            if (requestId != transferAnalysisRequestId)
            {
                return;
            }

            TransferAnalysis = analysis;
            validatedSourceDirectory = source;
            TransferMessage = TransferAnalysis.StatusMessage;
            if (!TransferAnalysis.CanImport)
            {
                TransferError = string.IsNullOrWhiteSpace(TransferAnalysis.StatusMessage)
                    ? "Сборку невозможно перенести с текущими путями."
                    : TransferAnalysis.StatusMessage;
            }
            RaiseTransferStepCompletionStateChanged();
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception exception)
        {
            if (requestId == transferAnalysisRequestId)
            {
                TransferAnalysis = null;
                TransferError = LocalizeTransferError(exception.Message);
                TransferMessage = "Не удалось подготовить перенос.";
            }
        }
        finally
        {
            if (requestId == transferAnalysisRequestId)
            {
                IsAnalyzingTransfer = false;
            }
        }
    }

    private async Task StartTransferAsync()
    {
        if (TransferAnalysis is null)
        {
            await EnsureFreshTransferAnalysisAsync();
        }

        if (!IsTransferReady)
        {
            TransferError = "Заполните все шаги и дождитесь успешной проверки.";
            return;
        }

        TransferStepIndex = 2;

        if (IsReplaceMode)
        {
            MessageBoxResult result = System.Windows.MessageBox.Show(
                "Текущая сборка Fluxora будет заменена копией из Mod Organizer 2. Исходная MO2-сборка не удалится. Продолжить?",
                "Подтвердите замену сборки",
                MessageBoxButton.YesNo,
                MessageBoxImage.Warning);
            if (result != MessageBoxResult.Yes)
            {
                return;
            }
        }

        try
        {
            ResetProgress();
            IsTransferCompleted = false;
            IsTransferRunning = true;
            TransferError = string.Empty;
            TransferMessage = "Перенос запущен. Исходная сборка MO2 останется без изменений.";

            ModProject project = await coreBridgeService.ImportModOrganizerInstanceAsync(
                SourceDirectory,
                DestinationRootDirectory,
                ExistingConfigPath,
                IsReplaceMode,
                DispatchProgress);

            ImportedProject = project;
            TransferOverallPercent = 100;
            TransferCopyPercent = 100;
            TransferDatabasePercent = 100;
            TransferCurrentStep = "Готово";
            TransferCurrentItem = project.Name;
            TransferMessage = IsReplaceMode
                ? "Текущая сборка заменена копией из MO2."
                : "Новая сборка добавлена в Fluxora.";
            IsTransferCompleted = true;
        }
        catch (Exception exception)
        {
            TransferError = LocalizeTransferError(exception.Message);
            TransferMessage = "Перенос остановлен.";
            IsTransferCompleted = false;
        }
        finally
        {
            IsTransferRunning = false;
        }
    }

    private void DispatchProgress(ModOrganizerImportProgress progress)
    {
        System.Windows.Application.Current.Dispatcher.BeginInvoke(() => ApplyProgress(progress));
    }

    private void ApplyProgress(ModOrganizerImportProgress progress)
    {
        TransferOverallPercent = progress.OverallPercent;
        TransferCopyPercent = progress.CopyPercent;
        TransferDatabasePercent = progress.DatabasePercent;
        TransferCurrentStep = string.IsNullOrWhiteSpace(progress.CurrentStep)
            ? "Перенос"
            : progress.CurrentStep;
        TransferCurrentItem = progress.CurrentItem;
        TransferBytesText = progress.TotalBytes == 0
            ? string.Empty
            : $"{TransferDriveOption.FormatBytes(progress.CopiedBytes)} из {TransferDriveOption.FormatBytes(progress.TotalBytes)}";
    }

    private void RefreshDrives()
    {
        AvailableDrives.Clear();
        foreach (DriveInfo drive in DriveInfo
            .GetDrives()
            .Where(drive => drive.IsReady && drive.DriveType is DriveType.Fixed or DriveType.Removable)
            .OrderByDescending(drive => drive.AvailableFreeSpace))
        {
            AvailableDrives.Add(TransferDriveOption.FromDrive(drive));
        }
    }

    private void ApplyInitialDestination()
    {
        SelectedDrive = null;
        DestinationRootDirectory = string.Empty;

        if (IsReplaceMode && currentProject is not null)
        {
            SelectSection(TransferSection);
        }
    }

    private void SelectDriveForPath(string path)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            return;
        }

        string root = Path.GetPathRoot(Path.GetFullPath(path)) ?? string.Empty;
        TransferDriveOption? drive = AvailableDrives.FirstOrDefault(candidate =>
            string.Equals(candidate.RootDirectory, root, StringComparison.OrdinalIgnoreCase));
        if (drive is not null && !ReferenceEquals(SelectedDrive, drive))
        {
            selectedDrive = drive;
            OnPropertyChanged(nameof(SelectedDrive));
            RaiseTransferStepCompletionStateChanged();
            RaiseTransferCommandStateChanged();
        }
    }

    private void UpdateDriveRequirements(ulong requiredBytes)
    {
        if (requiredTransferBytes == requiredBytes)
        {
            return;
        }

        requiredTransferBytes = requiredBytes;
        foreach (TransferDriveOption drive in AvailableDrives)
        {
            drive.RequiredBytes = requiredBytes;
        }

        RaiseTransferStepCompletionStateChanged();
        RaiseTransferCommandStateChanged();
    }

    private bool CanQueueAutomaticTransferAnalysis =>
        !IsTransferRunning &&
        !IsTransferCompleted &&
        !string.IsNullOrWhiteSpace(SourceDirectory) &&
        IsTransferDestinationComplete;

    private void QueueAutomaticTransferAnalysis()
    {
        transferAnalysisCancellation?.Cancel();

        if (!CanQueueAutomaticTransferAnalysis)
        {
            ++transferAnalysisRequestId;
            if (IsAnalyzingTransfer)
            {
                IsAnalyzingTransfer = false;
            }
            return;
        }

        int requestId = ++transferAnalysisRequestId;
        transferAnalysisCancellation = new CancellationTokenSource();
        _ = RunAutomaticTransferAnalysisAsync(requestId, transferAnalysisCancellation.Token);
    }

    private void InvalidateTransferAnalysis()
    {
        TransferAnalysis = null;
        TransferError = string.Empty;
        TransferMessage = "Параметры изменились. Автопроверка запустится после заполнения путей.";
        if (TransferStepIndex == 2)
        {
            TransferStepIndex = string.IsNullOrWhiteSpace(SourceDirectory) ? 0 : 1;
        }

        QueueAutomaticTransferAnalysis();
    }

    private void ResetProgress()
    {
        TransferOverallPercent = 0;
        TransferCopyPercent = 0;
        TransferDatabasePercent = 0;
        TransferCurrentStep = "Подготовка";
        TransferCurrentItem = string.Empty;
        TransferBytesText = string.Empty;
    }

    private string ExistingConfigPath => IsReplaceMode ? currentProject?.ConfigPath ?? string.Empty : string.Empty;

    private static string LocalizeTransferError(string message)
    {
        if (message.Contains("mods folder is missing", StringComparison.OrdinalIgnoreCase))
        {
            return "Выбранная папка не похожа на Mod Organizer 2: не найдена папка mods.";
        }

        if (message.Contains("profiles folder is missing", StringComparison.OrdinalIgnoreCase))
        {
            return "Выбранная папка не похожа на Mod Organizer 2: не найдена папка profiles.";
        }

        if (message.Contains("no profile contains", StringComparison.OrdinalIgnoreCase))
        {
            return "Выбранная папка не похожа на Mod Organizer 2: в profiles нет профиля с modlist.txt, plugins.txt или loadorder.txt.";
        }

        if (message.Contains("ModOrganizer.ini or MO2 files are missing", StringComparison.OrdinalIgnoreCase))
        {
            return "Выбранная папка не похожа на Mod Organizer 2: не найдены ModOrganizer.ini или файлы MO2.";
        }

        if (message.Contains("Mod Organizer 2 directory does not exist", StringComparison.OrdinalIgnoreCase))
        {
            return "Папка Mod Organizer 2 не найдена.";
        }

        if (message.Contains("Source and destination directories must be separate", StringComparison.OrdinalIgnoreCase))
        {
            return "Невозможно перенести сборку: источник и папка назначения совпадают или вложены друг в друга.";
        }

        if (message.Contains("cannot be imported", StringComparison.OrdinalIgnoreCase))
        {
            return "Сборку Mod Organizer 2 нельзя перенести с текущими настройками.";
        }

        if (message.Contains("locked by another process", StringComparison.OrdinalIgnoreCase) ||
            message.Contains("being used by another process", StringComparison.OrdinalIgnoreCase))
        {
            return "Файл занят другим процессом. Fluxora попыталась закрыть приложение, но Windows все еще держит файл. Подробности записаны в папку logs рядом с FluxoraModding.exe.";
        }

        if (message.Contains("Failed to write target file during import", StringComparison.OrdinalIgnoreCase))
        {
            return "Не удалось записать файл при переносе. Подробности записаны в папку logs рядом с FluxoraModding.exe.";
        }

        return message;
    }

    private static bool IsSamePath(string left, string right)
    {
        if (string.IsNullOrWhiteSpace(left) || string.IsNullOrWhiteSpace(right))
        {
            return false;
        }

        try
        {
            return string.Equals(
                Path.GetFullPath(left).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                Path.GetFullPath(right).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception)
        {
            return string.Equals(left.Trim(), right.Trim(), StringComparison.OrdinalIgnoreCase);
        }
    }

    private bool IsNexusModsConnectionPending
    {
        get => isNexusModsConnectionPending;
        set
        {
            if (SetField(ref isNexusModsConnectionPending, value))
            {
                OnPropertyChanged(nameof(IsNexusModsToggleChecked));
                OnPropertyChanged(nameof(NexusModsToggleText));
            }
        }
    }

    private void ApplyStatus(NexusModsAuthStatus status)
    {
        IsNexusModsLinked = status.IsLinked;
        NexusModsTitle = string.IsNullOrWhiteSpace(status.DisplayName)
            ? "Nexus Mods"
            : $"Nexus Mods · {status.DisplayName}";
        NexusModsMessage = string.IsNullOrWhiteSpace(status.Message)
            ? (status.IsLinked ? "Nexus Mods привязан." : "Nexus Mods не привязан.")
            : status.Message;
        NexusModsDetails = string.Empty;
        OnPropertyChanged(nameof(IsNexusModsToggleChecked));
    }

    private void OnLocalizationChanged(object? sender, EventArgs e)
    {
        OnPropertyChanged(nameof(NexusModsMessage));
        OnPropertyChanged(nameof(NexusModsToggleText));
        OnPropertyChanged(nameof(LanguageStatus));
        OnPropertyChanged(nameof(LanguageError));
        OnPropertyChanged(nameof(SelectedLanguageText));
        OnPropertyChanged(nameof(LanguageSwitchTitle));
        OnPropertyChanged(nameof(LanguageSwitchSubtitle));
        OnPropertyChanged(nameof(TransferMessage));
        OnPropertyChanged(nameof(TransferError));
        OnPropertyChanged(nameof(TransferCurrentStep));
        OnPropertyChanged(nameof(TransferSourceStepStatus));
        OnPropertyChanged(nameof(TransferDestinationStepStatus));
        OnPropertyChanged(nameof(TransferReviewStepStatus));
        OnPropertyChanged(nameof(TransferModeTitle));
        OnPropertyChanged(nameof(TransferModeSubtitle));
        OnPropertyChanged(nameof(TransferStepTitle));
        OnPropertyChanged(nameof(TransferStepSubtitle));
        OnPropertyChanged(nameof(TransferStepCounter));
        OnPropertyChanged(nameof(NextTransferStepText));
        OnPropertyChanged(nameof(DestinationLabel));
        OnPropertyChanged(nameof(TransferProjectName));
        OnPropertyChanged(nameof(TransferGameText));
        OnPropertyChanged(nameof(TransferProfileText));
        OnPropertyChanged(nameof(TransferModCountText));
        OnPropertyChanged(nameof(TransferSizeText));
        OnPropertyChanged(nameof(TransferAvailableText));
        OnPropertyChanged(nameof(TransferDiskSpaceText));
        OnPropertyChanged(nameof(StartTransferText));
        OnPropertyChanged(nameof(TransferProcessTitle));
        OnPropertyChanged(nameof(TransferProcessSubtitle));
    }

    private void RaiseBusyStateChanged()
    {
        OnPropertyChanged(nameof(IsBusy));
        OnPropertyChanged(nameof(CanStartTransfer));
        OnPropertyChanged(nameof(CanChangeDestination));
        OnPropertyChanged(nameof(CanMoveToPreviousTransferStep));
        OnPropertyChanged(nameof(CanMoveToNextTransferStep));
        (OpenModOrganizerTransferCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (CancelTransferFlowCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (SelectTransferStepCommand as RelayCommand<string>)?.RaiseCanExecuteChanged();
        (PreviousTransferStepCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (NextTransferStepCommand as AsyncRelayCommand)?.RaiseCanExecuteChanged();
        (BrowseSourceCommand as AsyncRelayCommand)?.RaiseCanExecuteChanged();
        (BrowseDestinationCommand as AsyncRelayCommand)?.RaiseCanExecuteChanged();
        (StartTransferCommand as AsyncRelayCommand)?.RaiseCanExecuteChanged();
        (SelectConnectionsCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (SelectLanguagesCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (SelectTransferCommand as RelayCommand)?.RaiseCanExecuteChanged();
    }

    private void RaiseProcessVisibilityChanged()
    {
        OnPropertyChanged(nameof(IsNormalSettingsVisible));
        OnPropertyChanged(nameof(IsTransferProcessVisible));
        OnPropertyChanged(nameof(TransferProcessTitle));
        OnPropertyChanged(nameof(TransferProcessSubtitle));
    }

    private void RaiseTransferSurfaceStateChanged()
    {
        OnPropertyChanged(nameof(IsTransferMenuVisible));
        OnPropertyChanged(nameof(IsTransferStepperVisible));
    }

    private void RaiseTransferStepStateChanged()
    {
        OnPropertyChanged(nameof(IsTransferSourceStep));
        OnPropertyChanged(nameof(IsTransferDestinationStep));
        OnPropertyChanged(nameof(IsTransferReviewStep));
        OnPropertyChanged(nameof(TransferStepTitle));
        OnPropertyChanged(nameof(TransferStepSubtitle));
        OnPropertyChanged(nameof(TransferStepCounter));
        OnPropertyChanged(nameof(NextTransferStepText));
    }

    private void RaiseTransferStepCompletionStateChanged()
    {
        OnPropertyChanged(nameof(IsTransferSourceComplete));
        OnPropertyChanged(nameof(IsTransferDestinationComplete));
        OnPropertyChanged(nameof(IsTransferReviewComplete));
        OnPropertyChanged(nameof(IsTransferReady));
        OnPropertyChanged(nameof(TransferSourceStepIcon));
        OnPropertyChanged(nameof(TransferDestinationStepIcon));
        OnPropertyChanged(nameof(TransferReviewStepIcon));
        OnPropertyChanged(nameof(TransferSourceStepStatus));
        OnPropertyChanged(nameof(TransferDestinationStepStatus));
        OnPropertyChanged(nameof(TransferReviewStepStatus));
    }

    private void RaiseTransferFlowCommandStateChanged()
    {
        OnPropertyChanged(nameof(CanMoveToPreviousTransferStep));
        OnPropertyChanged(nameof(CanMoveToNextTransferStep));
        (OpenModOrganizerTransferCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (CancelTransferFlowCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (SelectTransferStepCommand as RelayCommand<string>)?.RaiseCanExecuteChanged();
        (PreviousTransferStepCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (NextTransferStepCommand as AsyncRelayCommand)?.RaiseCanExecuteChanged();
    }

    private void RaiseTransferCommandStateChanged()
    {
        OnPropertyChanged(nameof(CanStartTransfer));
        OnPropertyChanged(nameof(CanChangeDestination));
        OnPropertyChanged(nameof(TransferAvailableText));
        OnPropertyChanged(nameof(TransferDiskSpaceText));
        RaiseTransferFlowCommandStateChanged();
        (BrowseSourceCommand as AsyncRelayCommand)?.RaiseCanExecuteChanged();
        (BrowseDestinationCommand as AsyncRelayCommand)?.RaiseCanExecuteChanged();
        (StartTransferCommand as AsyncRelayCommand)?.RaiseCanExecuteChanged();
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
