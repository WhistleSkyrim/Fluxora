using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows.Input;
using Fluxora.App.Models;
using Fluxora.App.Services;

namespace Fluxora.App.ViewModels;

public sealed class BuildSettingsWindowViewModel : INotifyPropertyChanged
{
    private readonly CoreBridgeService coreBridgeService;
    private readonly IFolderPickerService folderPickerService;
    private readonly IExecutablePickerService executablePickerService;
    private readonly ModProject project;

    private bool isBusy;
    private string statusText = "Загружаю пути сборки...";
    private string errorText = string.Empty;
    private string gameDirectory = string.Empty;
    private string gameExecutablePath = string.Empty;
    private string modsDirectory = string.Empty;
    private string profilesDirectory = string.Empty;
    private string downloadsDirectory = string.Empty;
    private string overwriteDirectory = string.Empty;
    private List<GameExecutableEntry> executables;

    public BuildSettingsWindowViewModel(
        CoreBridgeService coreBridgeService,
        IFolderPickerService folderPickerService,
        IExecutablePickerService executablePickerService,
        ModProject project)
    {
        this.coreBridgeService = coreBridgeService;
        this.folderPickerService = folderPickerService;
        this.executablePickerService = executablePickerService;
        this.project = project;
        executables = project.Executables.Select(executable => executable.Clone()).ToList();

        BrowseGameExecutableCommand = new RelayCommand(
            BrowseGameExecutable,
            () => !IsBusy);
        BrowseModsDirectoryCommand = new RelayCommand(
            () => BrowsePath("Папка модов", ModsDirectory, value => ModsDirectory = value),
            () => !IsBusy);
        BrowseProfilesDirectoryCommand = new RelayCommand(
            () => BrowsePath("Папка профилей", ProfilesDirectory, value => ProfilesDirectory = value),
            () => !IsBusy);
        BrowseDownloadsDirectoryCommand = new RelayCommand(
            () => BrowsePath("Папка загрузок", DownloadsDirectory, value => DownloadsDirectory = value),
            () => !IsBusy);
        BrowseOverwriteDirectoryCommand = new RelayCommand(
            () => BrowsePath("Папка overwrite", OverwriteDirectory, value => OverwriteDirectory = value),
            () => !IsBusy);
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public ICommand BrowseGameExecutableCommand { get; }
    public ICommand BrowseModsDirectoryCommand { get; }
    public ICommand BrowseProfilesDirectoryCommand { get; }
    public ICommand BrowseDownloadsDirectoryCommand { get; }
    public ICommand BrowseOverwriteDirectoryCommand { get; }

    public string ProjectName => project.Name;

    public string ProjectDirectory => project.ProjectDirectory;

    public BuildSettingsResult? SavedResult { get; private set; }

    public bool IsBusy
    {
        get => isBusy;
        private set
        {
            if (SetField(ref isBusy, value))
            {
                RaiseCommandStateChanged();
            }
        }
    }

    public bool HasError => !string.IsNullOrWhiteSpace(ErrorText);

    public string StatusText
    {
        get => statusText;
        private set => SetField(ref statusText, value);
    }

    public string ErrorText
    {
        get => errorText;
        private set
        {
            if (SetField(ref errorText, value))
            {
                OnPropertyChanged(nameof(HasError));
            }
        }
    }

    public string GameDirectory
    {
        get => gameDirectory;
        set => SetField(ref gameDirectory, value);
    }

    public string GameExecutablePath
    {
        get => gameExecutablePath;
        set
        {
            if (SetField(ref gameExecutablePath, value))
            {
                if (TryGameDirectoryFromExecutable(value, out string directory))
                {
                    GameDirectory = directory;
                }
            }
        }
    }

    public string ModsDirectory
    {
        get => modsDirectory;
        set => SetField(ref modsDirectory, value);
    }

    public string ProfilesDirectory
    {
        get => profilesDirectory;
        set => SetField(ref profilesDirectory, value);
    }

    public string DownloadsDirectory
    {
        get => downloadsDirectory;
        set => SetField(ref downloadsDirectory, value);
    }

    public string OverwriteDirectory
    {
        get => overwriteDirectory;
        set => SetField(ref overwriteDirectory, value);
    }

    public async Task InitializeAsync()
    {
        try
        {
            IsBusy = true;
            ErrorText = string.Empty;
            BuildPathSettings settings = await coreBridgeService.GetBuildPathSettingsAsync(project.ConfigPath);
            IReadOnlyList<GameExecutableEntry> currentExecutables =
                await coreBridgeService.GetGameExecutablesAsync(project.ConfigPath);
            ApplySettings(settings, currentExecutables);
            StatusText = "Пути загружены. Изменения применяются только к этой сборке.";
        }
        catch (Exception exception)
        {
            ApplySettings(project.Paths, project.Executables);
            ErrorText = $"Не удалось загрузить пути: {exception.Message}";
            StatusText = "Показаны текущие локальные значения.";
        }
        finally
        {
            IsBusy = false;
        }
    }

    public async Task<bool> SaveAsync()
    {
        try
        {
            if (!TryValidateGameExecutable(out string validationMessage))
            {
                ErrorText = validationMessage;
                StatusText = "Выберите исполняемый файл игры.";
                return false;
            }

            IsBusy = true;
            ErrorText = string.Empty;
            StatusText = "Сохраняю пути сборки...";
            BuildPathSettings saved = await coreBridgeService.SaveBuildPathSettingsAsync(
                project.ConfigPath,
                CurrentSettings());
            IReadOnlyList<GameExecutableEntry> savedExecutables =
                await coreBridgeService.SaveGameExecutablesAsync(
                    project.ConfigPath,
                    BuildExecutableList(GameExecutablePath, saved.GameDirectory));

            SavedResult = new BuildSettingsResult
            {
                Paths = saved,
                Executables = savedExecutables
            };
            ApplySettings(saved, savedExecutables);
            StatusText = "Пути сборки сохранены.";
            return true;
        }
        catch (Exception exception)
        {
            ErrorText = $"Не удалось сохранить пути: {exception.Message}";
            StatusText = "Проверьте, что путь игры существует, а папки доступны для записи.";
            return false;
        }
        finally
        {
            IsBusy = false;
        }
    }

    private BuildPathSettings CurrentSettings()
    {
        string effectiveGameDirectory = GameDirectory;
        if (TryGameDirectoryFromExecutable(GameExecutablePath, out string executableDirectory))
        {
            effectiveGameDirectory = executableDirectory;
        }

        return new BuildPathSettings
        {
            GameDirectory = effectiveGameDirectory,
            ModsDirectory = ModsDirectory,
            ProfilesDirectory = ProfilesDirectory,
            DownloadsDirectory = DownloadsDirectory,
            OverwriteDirectory = OverwriteDirectory
        };
    }

    private void ApplySettings(
        BuildPathSettings settings,
        IReadOnlyList<GameExecutableEntry> currentExecutables)
    {
        BuildPathSettings copy = settings.Clone();
        copy.ApplyFallbacks(project.ProjectDirectory, project.GamePath);
        executables = currentExecutables.Select(executable => executable.Clone()).ToList();
        GameDirectory = copy.GameDirectory;
        GameExecutablePath = ResolveGameExecutablePath(executables, copy.GameDirectory);
        ModsDirectory = copy.ModsDirectory;
        ProfilesDirectory = copy.ProfilesDirectory;
        DownloadsDirectory = copy.DownloadsDirectory;
        OverwriteDirectory = copy.OverwriteDirectory;
    }

    private void BrowseGameExecutable()
    {
        string selectedRoot = File.Exists(GameExecutablePath)
            ? GameExecutablePath
            : Directory.Exists(GameDirectory)
                ? GameDirectory
                : project.ProjectDirectory;
        string? selected = executablePickerService.PickExecutable("Выберите .exe игры", selectedRoot);
        if (!string.IsNullOrWhiteSpace(selected))
        {
            GameExecutablePath = selected;
        }
    }

    private bool TryValidateGameExecutable(out string message)
    {
        if (string.IsNullOrWhiteSpace(GameExecutablePath))
        {
            message = "Укажите .exe игры.";
            return false;
        }

        if (!string.Equals(Path.GetExtension(GameExecutablePath), ".exe", StringComparison.OrdinalIgnoreCase))
        {
            message = "Путь игры должен вести к .exe файлу.";
            return false;
        }

        if (!File.Exists(GameExecutablePath))
        {
            message = "Выбранный .exe игры не найден.";
            return false;
        }

        message = string.Empty;
        return true;
    }

    private List<GameExecutableEntry> BuildExecutableList(string executablePath, string gameDirectory)
    {
        List<GameExecutableEntry> updated = executables.Select(executable => executable.Clone()).ToList();
        string normalizedExecutablePath = Path.GetFullPath(executablePath);
        string storedExecutablePath = ToStoredExecutablePath(normalizedExecutablePath, gameDirectory);
        int index = updated.FindIndex(executable => IsPrimaryGameExecutable(executable, gameDirectory));
        if (index < 0)
        {
            updated.Insert(0, new GameExecutableEntry());
            index = 0;
        }

        GameExecutableEntry primary = updated[index];
        ExecutableDisplayMetadata metadata = ResolveExecutableMetadata(normalizedExecutablePath);
        primary.Id = !string.IsNullOrWhiteSpace(metadata.Id)
            ? metadata.Id
            : string.IsNullOrWhiteSpace(primary.Id)
                ? "game"
                : primary.Id;
        primary.DisplayName = !string.IsNullOrWhiteSpace(metadata.DisplayName)
            ? metadata.DisplayName
            : DisplayNameForExecutable(normalizedExecutablePath);
        primary.ExecutablePath = storedExecutablePath;
        primary.WorkingDirectory = string.Empty;
        primary.IconPath = string.Empty;
        primary.ExecutableDisplayMetadata = metadata;
        return updated;
    }

    private string ResolveGameExecutablePath(
        IReadOnlyList<GameExecutableEntry> currentExecutables,
        string currentGameDirectory)
    {
        GameExecutableEntry? primary = currentExecutables.FirstOrDefault(
            executable => IsPrimaryGameExecutable(executable, currentGameDirectory));
        primary ??= currentExecutables.FirstOrDefault(
            executable => string.Equals(
                Path.GetExtension(executable.ExecutablePath),
                ".exe",
                StringComparison.OrdinalIgnoreCase));

        if (primary is not null)
        {
            return ResolveExecutablePath(primary.ExecutablePath, currentGameDirectory);
        }

        foreach (string executableName in KnownExecutableNames())
        {
            string candidate = Path.Combine(currentGameDirectory, executableName);
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        return string.Empty;
    }

    private bool IsPrimaryGameExecutable(GameExecutableEntry executable, string currentGameDirectory)
    {
        string resolvedPath = ResolveExecutablePath(executable.ExecutablePath, currentGameDirectory);
        string fileName = Path.GetFileName(resolvedPath);
        return executable.ExecutableDisplayMetadata.IsPrimary ||
            string.Equals(executable.ExecutableDisplayMetadata.Role, "primary", StringComparison.OrdinalIgnoreCase) ||
            KnownExecutableNames().Any(
                name => string.Equals(name, fileName, StringComparison.OrdinalIgnoreCase)) ||
            string.Equals(executable.Id, "game", StringComparison.OrdinalIgnoreCase);
    }

    private string ResolveExecutablePath(string executablePath, string currentGameDirectory)
    {
        if (string.IsNullOrWhiteSpace(executablePath))
        {
            return string.Empty;
        }

        if (Path.IsPathRooted(executablePath))
        {
            return Path.GetFullPath(executablePath);
        }

        string gameCandidate = Path.Combine(currentGameDirectory, executablePath);
        if (File.Exists(gameCandidate))
        {
            return Path.GetFullPath(gameCandidate);
        }

        string projectCandidate = Path.Combine(project.ProjectDirectory, executablePath);
        if (File.Exists(projectCandidate))
        {
            return Path.GetFullPath(projectCandidate);
        }

        return gameCandidate;
    }

    private static bool TryGameDirectoryFromExecutable(string executablePath, out string directory)
    {
        directory = string.Empty;
        if (!string.Equals(Path.GetExtension(executablePath), ".exe", StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        directory = Path.GetDirectoryName(executablePath) ?? string.Empty;
        return !string.IsNullOrWhiteSpace(directory);
    }

    private static string ToStoredExecutablePath(string executablePath, string gameDirectory)
    {
        string normalizedGameDirectory = Path.GetFullPath(gameDirectory)
            .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string normalizedExecutableDirectory = Path.GetDirectoryName(executablePath) ?? string.Empty;
        normalizedExecutableDirectory = Path.GetFullPath(normalizedExecutableDirectory)
            .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);

        return string.Equals(normalizedExecutableDirectory, normalizedGameDirectory, StringComparison.OrdinalIgnoreCase)
            ? Path.GetFileName(executablePath)
            : executablePath;
    }

    private string DisplayNameForExecutable(string executablePath)
    {
        ExecutableDisplayMetadata metadata = ResolveExecutableMetadata(executablePath);
        return string.IsNullOrWhiteSpace(metadata.DisplayName)
            ? Path.GetFileNameWithoutExtension(executablePath)
            : metadata.DisplayName;
    }

    private ExecutableDisplayMetadata ResolveExecutableMetadata(string executablePath)
    {
        string fileName = Path.GetFileName(executablePath);
        ExecutableDisplayMetadata? metadata = project.Template?.ExecutableDisplayMetadata.FirstOrDefault(candidate =>
            !string.IsNullOrWhiteSpace(candidate.ExecutableName) &&
            string.Equals(candidate.ExecutableName, fileName, StringComparison.OrdinalIgnoreCase));
        metadata ??= executables
            .Select(executable => executable.ExecutableDisplayMetadata)
            .FirstOrDefault(candidate =>
                !string.IsNullOrWhiteSpace(candidate.ExecutableName) &&
                string.Equals(candidate.ExecutableName, fileName, StringComparison.OrdinalIgnoreCase));

        if (metadata is null)
        {
            return new ExecutableDisplayMetadata
            {
                DisplayName = Path.GetFileNameWithoutExtension(executablePath),
                ExecutableName = fileName
            };
        }

        return new ExecutableDisplayMetadata
        {
            Id = metadata.Id,
            DisplayName = metadata.DisplayName,
            ExecutableName = metadata.ExecutableName,
            Role = metadata.Role,
            WorkingDirectoryKind = metadata.WorkingDirectoryKind,
            IsPrimary = metadata.IsPrimary,
            IsLauncher = metadata.IsLauncher,
            IsScriptExtender = metadata.IsScriptExtender
        };
    }

    private IReadOnlyList<string> KnownExecutableNames()
    {
        List<string> names = new();
        if (project.Template is not null)
        {
            names.AddRange(project.Template.ExecutableDisplayMetadata.Select(metadata => metadata.ExecutableName));
            names.AddRange(project.Template.Executables
                .Select(Path.GetFileName)
                .Where(name => !string.IsNullOrWhiteSpace(name))
                .Select(name => name!));
        }

        names.AddRange(executables
            .Select(executable => Path.GetFileName(executable.ExecutablePath))
            .Where(name => !string.IsNullOrWhiteSpace(name))
            .Select(name => name!));
        names.Add(project.ProjectFingerprint?.SelectedExecutable ?? string.Empty);

        return names
            .Where(name => !string.IsNullOrWhiteSpace(name))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    private void BrowsePath(string title, string currentPath, Action<string> apply)
    {
        string selectedRoot = Directory.Exists(currentPath) ? currentPath : project.ProjectDirectory;
        string? selected = folderPickerService.PickFolder(title, selectedRoot);
        if (!string.IsNullOrWhiteSpace(selected))
        {
            apply(selected);
        }
    }

    private void RaiseCommandStateChanged()
    {
        (BrowseGameExecutableCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (BrowseModsDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (BrowseProfilesDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (BrowseDownloadsDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
        (BrowseOverwriteDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
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
}
