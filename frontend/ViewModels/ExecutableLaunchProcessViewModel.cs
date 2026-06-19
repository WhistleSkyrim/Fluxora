using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using Fluxora.App.Models;

namespace Fluxora.App.ViewModels;

public sealed class ExecutableLaunchProcessViewModel : INotifyPropertyChanged
{
    private bool isVisible;
    private bool isStarting;
    private bool isProcessRunning;
    private bool isCompleted;
    private bool hasError;
    private int processId;
    private double overallPercent;
    private string title = "Запуск процесса";
    private string currentStep = "Подготовка";
    private string statusText = string.Empty;
    private string processName = string.Empty;
    private string projectName = string.Empty;
    private string errorMessage = string.Empty;

    public event PropertyChangedEventHandler? PropertyChanged;

    public bool IsVisible
    {
        get => isVisible;
        private set
        {
            if (SetField(ref isVisible, value))
            {
                OnPropertyChanged(nameof(CanClose));
                OnPropertyChanged(nameof(IsSpinnerActive));
            }
        }
    }

    public bool IsStarting
    {
        get => isStarting;
        private set
        {
            if (SetField(ref isStarting, value))
            {
                OnPropertyChanged(nameof(CanClose));
                OnPropertyChanged(nameof(IsSpinnerActive));
            }
        }
    }

    public bool IsProcessRunning
    {
        get => isProcessRunning;
        private set
        {
            if (SetField(ref isProcessRunning, value))
            {
                OnPropertyChanged(nameof(CanClose));
                OnPropertyChanged(nameof(IsSpinnerActive));
            }
        }
    }

    public bool IsCompleted
    {
        get => isCompleted;
        private set => SetField(ref isCompleted, value);
    }

    public bool HasError
    {
        get => hasError;
        private set => SetField(ref hasError, value);
    }

    public bool CanClose => IsVisible && !IsStarting;

    public bool IsSpinnerActive => IsVisible && IsStarting;

    public int ProcessId
    {
        get => processId;
        private set => SetField(ref processId, value);
    }

    public double OverallPercent
    {
        get => overallPercent;
        private set => SetField(ref overallPercent, Math.Clamp(value, 0, 100));
    }

    public string Title
    {
        get => title;
        private set => SetField(ref title, value);
    }

    public string CurrentStep
    {
        get => currentStep;
        private set => SetField(ref currentStep, value);
    }

    public string StatusText
    {
        get => statusText;
        private set => SetField(ref statusText, value);
    }

    public string ProcessName
    {
        get => processName;
        private set => SetField(ref processName, value);
    }

    public string ProjectName
    {
        get => projectName;
        private set => SetField(ref projectName, value);
    }

    public string ErrorMessage
    {
        get => errorMessage;
        private set => SetField(ref errorMessage, value);
    }

    public string CloseButtonText => "Закрыть приложение";

    public void Start(
        string executablePathOrName,
        string displayName,
        string buildName,
        bool usesExpectedChildProcessTracking,
        string handoffDisplayName = "")
    {
        string name = ResolveProcessName(executablePathOrName, displayName);
        string handoffName = ResolveHandoffDisplayName(handoffDisplayName, displayName, name);
        ProcessName = name;
        ProjectName = buildName.Trim();
        Title = "Запуск процесса";
        CurrentStep = usesExpectedChildProcessTracking ? "Запуск передаётся приложению" : $"Запускается: {name}";
        StatusText = usesExpectedChildProcessTracking && !string.IsNullOrWhiteSpace(ProjectName)
            ? $"{ProjectName} · {handoffName}"
            : name;
        ErrorMessage = string.Empty;
        ProcessId = 0;
        OverallPercent = 36;
        HasError = false;
        IsCompleted = false;
        IsProcessRunning = false;
        IsStarting = true;
        IsVisible = true;
    }

    public void MarkLaunched(ExecutableLaunchSession session, bool recovered)
    {
        ProcessName = ResolveProcessName(session.ResolvedExecutablePath, session.DisplayName);
        ProjectName = session.ProjectName;
        ProcessId = session.ProcessId;
        Title = recovered ? "Активный процесс восстановлен" : "Запуск процесса";
        CurrentStep = "Процесс запущен";
        StatusText = FormatRunningStatus(session);
        ErrorMessage = string.Empty;
        OverallPercent = 100;
        HasError = false;
        IsCompleted = false;
        IsStarting = false;
        IsProcessRunning = true;
        IsVisible = true;
    }

    public void MarkWaitingForChildProcess(ExecutableLaunchSession session)
    {
        ProcessName = ResolveProcessName(session.ResolvedExecutablePath, session.DisplayName);
        ProjectName = session.ProjectName;
        ProcessId = session.ProcessId;
        Title = "Запуск процесса";
        CurrentStep = "Процесс запущен";
        string handoffName = ResolveHandoffDisplayName(session.HandoffDisplayName, session.DisplayName, ProcessName);
        StatusText = !string.IsNullOrWhiteSpace(session.ProjectName)
            ? $"{session.ProjectName} · ожидаю {handoffName}"
            : $"Ожидаю {handoffName}";
        ErrorMessage = string.Empty;
        OverallPercent = 100;
        HasError = false;
        IsCompleted = false;
        IsStarting = false;
        IsProcessRunning = true;
        IsVisible = true;
    }

    public void MarkExited(string exitText)
    {
        CurrentStep = "Процесс закрыт";
        StatusText = string.IsNullOrWhiteSpace(exitText) ? ProcessName : exitText;
        ErrorMessage = string.Empty;
        OverallPercent = 100;
        HasError = false;
        IsStarting = false;
        IsProcessRunning = false;
        IsCompleted = true;
        IsVisible = true;
    }

    public void Fail(string message)
    {
        CurrentStep = "Ошибка запуска";
        StatusText = "Процесс не был запущен";
        ErrorMessage = string.IsNullOrWhiteSpace(message)
            ? "Не удалось запустить процесс."
            : message;
        OverallPercent = 0;
        HasError = true;
        IsCompleted = false;
        IsProcessRunning = false;
        IsStarting = false;
        IsVisible = true;
    }

    public void Close()
    {
        if (IsStarting)
        {
            return;
        }

        Reset();
    }

    public void Reset()
    {
        IsVisible = false;
        IsStarting = false;
        IsProcessRunning = false;
        IsCompleted = false;
        HasError = false;
        ProcessId = 0;
        OverallPercent = 0;
        Title = "Запуск процесса";
        CurrentStep = "Подготовка";
        StatusText = string.Empty;
        ErrorMessage = string.Empty;
        ProcessName = string.Empty;
        ProjectName = string.Empty;
    }

    public static string ResolveProcessName(string executablePathOrName, string displayName = "")
    {
        if (!string.IsNullOrWhiteSpace(executablePathOrName))
        {
            string fileName = Path.GetFileName(executablePathOrName.Trim());
            if (!string.IsNullOrWhiteSpace(fileName))
            {
                return fileName;
            }
        }

        return string.IsNullOrWhiteSpace(displayName) ? "process.exe" : displayName.Trim();
    }

    private static string FormatRunningStatus(ExecutableLaunchSession session)
    {
        string processName = ResolveProcessName(session.ResolvedExecutablePath, session.DisplayName);
        if (session.TracksExpectedChildProcess && !string.IsNullOrWhiteSpace(session.ProjectName))
        {
            string handoffName = ResolveHandoffDisplayName(session.HandoffDisplayName, session.DisplayName, processName);
            return $"{session.ProjectName} · {handoffName}";
        }

        return session.ProcessId > 0
            ? $"{processName} · PID {session.ProcessId}"
            : processName;
    }

    private static string ResolveHandoffDisplayName(
        string handoffDisplayName,
        string displayName,
        string processName)
    {
        if (!string.IsNullOrWhiteSpace(handoffDisplayName))
        {
            return handoffDisplayName.Trim();
        }

        if (!string.IsNullOrWhiteSpace(displayName))
        {
            return displayName.Trim();
        }

        return string.IsNullOrWhiteSpace(processName) ? "приложение" : processName;
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
