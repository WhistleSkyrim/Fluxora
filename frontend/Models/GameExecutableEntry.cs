using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.Json.Serialization;

namespace Fluxora.App.Models;

public sealed class GameExecutableEntry : INotifyPropertyChanged
{
    private string id = string.Empty;
    private string displayName = string.Empty;
    private string executablePath = string.Empty;
    private string arguments = string.Empty;
    private string workingDirectory = string.Empty;
    private string iconPath = string.Empty;
    private ExecutableDisplayMetadata executableDisplayMetadata = new();

    public event PropertyChangedEventHandler? PropertyChanged;

    [JsonPropertyName("id")]
    public string Id
    {
        get => id;
        set => SetField(ref id, value);
    }

    [JsonPropertyName("displayName")]
    public string DisplayName
    {
        get => displayName;
        set
        {
            if (SetField(ref displayName, value))
            {
                OnPropertyChanged(nameof(DisplayTitle));
                OnPropertyChanged(nameof(SummaryText));
            }
        }
    }

    [JsonPropertyName("executablePath")]
    public string ExecutablePath
    {
        get => executablePath;
        set
        {
            if (SetField(ref executablePath, value))
            {
                OnPropertyChanged(nameof(DisplayTitle));
                OnPropertyChanged(nameof(SummaryText));
            }
        }
    }

    [JsonPropertyName("arguments")]
    public string Arguments
    {
        get => arguments;
        set
        {
            if (SetField(ref arguments, value))
            {
                OnPropertyChanged(nameof(SummaryText));
            }
        }
    }

    [JsonPropertyName("workingDirectory")]
    public string WorkingDirectory
    {
        get => workingDirectory;
        set
        {
            if (SetField(ref workingDirectory, value))
            {
                OnPropertyChanged(nameof(SummaryText));
            }
        }
    }

    [JsonPropertyName("iconPath")]
    public string IconPath
    {
        get => iconPath;
        set => SetField(ref iconPath, value);
    }

    [JsonPropertyName("executableDisplayMetadata")]
    public ExecutableDisplayMetadata ExecutableDisplayMetadata
    {
        get => executableDisplayMetadata;
        set => SetField(ref executableDisplayMetadata, value ?? new ExecutableDisplayMetadata());
    }

    [JsonIgnore]
    public string DisplayTitle
    {
        get
        {
            if (!string.IsNullOrWhiteSpace(DisplayName))
            {
                return DisplayName;
            }

            string fileName = Path.GetFileNameWithoutExtension(ExecutablePath);
            return string.IsNullOrWhiteSpace(fileName) ? "Новый executable" : fileName;
        }
    }

    [JsonIgnore]
    public string SummaryText
    {
        get
        {
            string path = string.IsNullOrWhiteSpace(ExecutablePath) ? "Путь не задан" : ExecutablePath;
            return string.IsNullOrWhiteSpace(Arguments) ? path : $"{path} {Arguments}";
        }
    }

    public GameExecutableEntry Clone()
    {
        return new GameExecutableEntry
        {
            Id = Id,
            DisplayName = DisplayName,
            ExecutablePath = ExecutablePath,
            Arguments = Arguments,
            WorkingDirectory = WorkingDirectory,
            IconPath = IconPath,
            ExecutableDisplayMetadata = new ExecutableDisplayMetadata
            {
                Id = ExecutableDisplayMetadata.Id,
                DisplayName = ExecutableDisplayMetadata.DisplayName,
                ExecutableName = ExecutableDisplayMetadata.ExecutableName,
                Role = ExecutableDisplayMetadata.Role,
                WorkingDirectoryKind = ExecutableDisplayMetadata.WorkingDirectoryKind,
                IsPrimary = ExecutableDisplayMetadata.IsPrimary,
                IsLauncher = ExecutableDisplayMetadata.IsLauncher,
                IsScriptExtender = ExecutableDisplayMetadata.IsScriptExtender
            }
        };
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
