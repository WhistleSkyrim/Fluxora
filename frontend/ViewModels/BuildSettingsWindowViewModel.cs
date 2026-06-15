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
    private readonly ModProject project;

    private bool isBusy;
    private string statusText = "Загружаю пути сборки...";
    private string errorText = string.Empty;
    private string gameDirectory = string.Empty;
    private string modsDirectory = string.Empty;
    private string profilesDirectory = string.Empty;
    private string downloadsDirectory = string.Empty;
    private string overwriteDirectory = string.Empty;

    public BuildSettingsWindowViewModel(
        CoreBridgeService coreBridgeService,
        IFolderPickerService folderPickerService,
        ModProject project)
    {
        this.coreBridgeService = coreBridgeService;
        this.folderPickerService = folderPickerService;
        this.project = project;

        BrowseGameDirectoryCommand = new RelayCommand(
            () => BrowsePath("Путь к игре", GameDirectory, value => GameDirectory = value),
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

    public ICommand BrowseGameDirectoryCommand { get; }
    public ICommand BrowseModsDirectoryCommand { get; }
    public ICommand BrowseProfilesDirectoryCommand { get; }
    public ICommand BrowseDownloadsDirectoryCommand { get; }
    public ICommand BrowseOverwriteDirectoryCommand { get; }

    public string ProjectName => project.Name;

    public string ProjectDirectory => project.ProjectDirectory;

    public BuildPathSettings? SavedSettings { get; private set; }

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
            ApplySettings(settings);
            StatusText = "Пути загружены. Изменения применяются только к этой сборке.";
        }
        catch (Exception exception)
        {
            ApplySettings(project.Paths);
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
            IsBusy = true;
            ErrorText = string.Empty;
            StatusText = "Сохраняю пути сборки...";
            BuildPathSettings saved = await coreBridgeService.SaveBuildPathSettingsAsync(
                project.ConfigPath,
                CurrentSettings());
            SavedSettings = saved;
            ApplySettings(saved);
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
        return new BuildPathSettings
        {
            GameDirectory = GameDirectory,
            ModsDirectory = ModsDirectory,
            ProfilesDirectory = ProfilesDirectory,
            DownloadsDirectory = DownloadsDirectory,
            OverwriteDirectory = OverwriteDirectory
        };
    }

    private void ApplySettings(BuildPathSettings settings)
    {
        BuildPathSettings copy = settings.Clone();
        copy.ApplyFallbacks(project.ProjectDirectory, project.GamePath);
        GameDirectory = copy.GameDirectory;
        ModsDirectory = copy.ModsDirectory;
        ProfilesDirectory = copy.ProfilesDirectory;
        DownloadsDirectory = copy.DownloadsDirectory;
        OverwriteDirectory = copy.OverwriteDirectory;
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
        (BrowseGameDirectoryCommand as RelayCommand)?.RaiseCanExecuteChanged();
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
