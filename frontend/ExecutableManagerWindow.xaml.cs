using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Input;
using Fluxora.App.Services;
using Fluxora.App.Models;
using Forms = System.Windows.Forms;

namespace Fluxora.App;

public partial class ExecutableManagerWindow : Window, INotifyPropertyChanged
{
    private readonly WindowChromeService windowChromeService;
    private readonly string gamePath;
    private readonly string projectDirectory;
    private readonly Func<string, string> resolveExecutableIconPath;
    private GameExecutableEntry? selectedExecutable;
    private string validationMessage = string.Empty;

    public ExecutableManagerWindow(
        IReadOnlyList<GameExecutableEntry> executables,
        string gamePath,
        string projectDirectory,
        Func<string, string> resolveExecutableIconPath)
    {
        InitializeComponent();
        windowChromeService = new WindowChromeService(this);
        windowChromeService.Attach();
        this.gamePath = gamePath;
        this.projectDirectory = projectDirectory;
        this.resolveExecutableIconPath = resolveExecutableIconPath;

        foreach (GameExecutableEntry executable in executables)
        {
            Executables.Add(executable.Clone());
        }

        SelectedExecutable = Executables.FirstOrDefault();
        DataContext = this;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public ObservableCollection<GameExecutableEntry> Executables { get; } = new();

    public IReadOnlyList<GameExecutableEntry> ResultExecutables => Executables
        .Select(executable => executable.Clone())
        .ToList();

    public GameExecutableEntry? SelectedExecutable
    {
        get => selectedExecutable;
        set
        {
            if (SetField(ref selectedExecutable, value))
            {
                ValidationMessage = string.Empty;
                OnPropertyChanged(nameof(HasSelectedExecutable));
            }
        }
    }

    public bool HasSelectedExecutable => SelectedExecutable is not null;

    public string ValidationMessage
    {
        get => validationMessage;
        private set => SetField(ref validationMessage, value);
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        ExecutablesListBox.Focus();
        Keyboard.Focus(ExecutablesListBox);
    }

    private void OnAddExecutableClick(object sender, RoutedEventArgs e)
    {
        GameExecutableEntry executable = new()
        {
            DisplayName = "Новый запуск"
        };

        Executables.Add(executable);
        SelectedExecutable = executable;
        ExecutablesListBox.ScrollIntoView(executable);
    }

    private void OnDeleteExecutableClick(object sender, RoutedEventArgs e)
    {
        if (SelectedExecutable is null)
        {
            return;
        }

        int index = Executables.IndexOf(SelectedExecutable);
        Executables.Remove(SelectedExecutable);
        SelectedExecutable = Executables.Count == 0
            ? null
            : Executables[Math.Clamp(index, 0, Executables.Count - 1)];
        ValidationMessage = string.Empty;
    }

    private void OnBrowseExecutableClick(object sender, RoutedEventArgs e)
    {
        if (SelectedExecutable is null)
        {
            return;
        }

        Microsoft.Win32.OpenFileDialog dialog = new()
        {
            Title = "Выберите исполняемый файл",
            Filter = "Executable (*.exe)|*.exe",
            CheckFileExists = true,
            Multiselect = false
        };

        string? initialDirectory = ResolveInitialDirectory(SelectedExecutable.ExecutablePath);
        if (!string.IsNullOrWhiteSpace(initialDirectory))
        {
            dialog.InitialDirectory = initialDirectory;
        }

        if (dialog.ShowDialog(this) != true)
        {
            return;
        }

        SelectedExecutable.ExecutablePath = dialog.FileName;
        SelectedExecutable.IconPath = resolveExecutableIconPath(dialog.FileName);
        if (string.IsNullOrWhiteSpace(SelectedExecutable.DisplayName))
        {
            SelectedExecutable.DisplayName = Path.GetFileNameWithoutExtension(dialog.FileName);
        }

        ValidationMessage = string.Empty;
    }

    private void OnBrowseWorkingDirectoryClick(object sender, RoutedEventArgs e)
    {
        if (SelectedExecutable is null)
        {
            return;
        }

        using Forms.FolderBrowserDialog dialog = new()
        {
            Description = "Выберите рабочую папку",
            UseDescriptionForTitle = true,
            SelectedPath = ResolveInitialDirectory(SelectedExecutable.WorkingDirectory) ?? gamePath
        };

        if (dialog.ShowDialog() == Forms.DialogResult.OK)
        {
            SelectedExecutable.WorkingDirectory = dialog.SelectedPath;
            ValidationMessage = string.Empty;
        }
    }

    private void OnSaveClick(object sender, RoutedEventArgs e)
    {
        if (!TryValidateExecutables(out string message))
        {
            ValidationMessage = message;
            return;
        }

        DialogResult = true;
        Close();
    }

    private void OnCancelClick(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
        Close();
    }

    private void OnWindowDragMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton != MouseButton.Left)
        {
            return;
        }

        try
        {
            DragMove();
        }
        catch (InvalidOperationException)
        {
        }
    }

    private bool TryValidateExecutables(out string message)
    {
        for (int index = 0; index < Executables.Count; index++)
        {
            GameExecutableEntry executable = Executables[index];
            executable.DisplayName = executable.DisplayName.Trim();
            executable.ExecutablePath = executable.ExecutablePath.Trim();
            executable.Arguments = executable.Arguments.Trim();
            executable.WorkingDirectory = executable.WorkingDirectory.Trim();

            if (string.IsNullOrWhiteSpace(executable.ExecutablePath))
            {
                message = $"Укажите путь к .exe для пункта {index + 1}.";
                SelectedExecutable = executable;
                return false;
            }

            if (!string.Equals(
                    Path.GetExtension(executable.ExecutablePath),
                    ".exe",
                    StringComparison.OrdinalIgnoreCase))
            {
                message = $"Путь должен вести к .exe: {executable.DisplayTitle}.";
                SelectedExecutable = executable;
                return false;
            }
        }

        message = string.Empty;
        return true;
    }

    private string? ResolveInitialDirectory(string path)
    {
        if (!string.IsNullOrWhiteSpace(path))
        {
            if (Directory.Exists(path))
            {
                return path;
            }

            string? directory = Path.GetDirectoryName(path);
            if (!string.IsNullOrWhiteSpace(directory) && Directory.Exists(directory))
            {
                return directory;
            }
        }

        if (!string.IsNullOrWhiteSpace(gamePath) && Directory.Exists(gamePath))
        {
            return gamePath;
        }

        return !string.IsNullOrWhiteSpace(projectDirectory) && Directory.Exists(projectDirectory)
            ? projectDirectory
            : null;
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
