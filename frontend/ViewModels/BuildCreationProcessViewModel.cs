using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Fluxora.App.ViewModels;

public enum BuildCreationProcessStage
{
    Data = 0,
    Template = 1,
    Files = 2,
    Catalog = 3
}

public sealed class BuildCreationProcessViewModel : INotifyPropertyChanged
{
    public const string PendingStepState = "Pending";
    public const string ActiveStepState = "Active";
    public const string CompleteStepState = "Complete";
    public const string CancelingStepState = "Canceling";

    private const int LastStepIndex = (int)BuildCreationProcessStage.Catalog;

    private bool isVisible;
    private bool isRunning;
    private bool isCompleted;
    private bool isCanceled;
    private bool hasError;
    private bool isCancellationRequested;
    private double overallPercent;
    private int activeStepIndex;
    private string currentStep = "Данные проекта";
    private string statusText = "Готовлю создание сборки";
    private string projectName = string.Empty;
    private string templateName = string.Empty;
    private string targetDirectory = string.Empty;
    private string errorMessage = string.Empty;
    private string dataStepState = ActiveStepState;
    private string templateStepState = PendingStepState;
    private string filesStepState = PendingStepState;
    private string catalogStepState = PendingStepState;

    public event PropertyChangedEventHandler? PropertyChanged;

    public bool IsVisible
    {
        get => isVisible;
        private set
        {
            if (SetField(ref isVisible, value))
            {
                OnPropertyChanged(nameof(CanCancel));
                OnPropertyChanged(nameof(CanClose));
            }
        }
    }

    public bool IsRunning
    {
        get => isRunning;
        private set
        {
            if (SetField(ref isRunning, value))
            {
                OnPropertyChanged(nameof(CanCancel));
                OnPropertyChanged(nameof(CanClose));
            }
        }
    }

    public bool IsCompleted
    {
        get => isCompleted;
        private set => SetField(ref isCompleted, value);
    }

    public bool IsCanceled
    {
        get => isCanceled;
        private set => SetField(ref isCanceled, value);
    }

    public bool HasError
    {
        get => hasError;
        private set => SetField(ref hasError, value);
    }

    public bool CanCancel => IsVisible && IsRunning && !IsCancellationRequested;

    public bool CanClose => IsVisible && !IsRunning;

    public double OverallPercent
    {
        get => overallPercent;
        private set => SetField(ref overallPercent, Math.Clamp(value, 0, 100));
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

    public string ProjectName
    {
        get => projectName;
        private set => SetField(ref projectName, value);
    }

    public string TemplateName
    {
        get => templateName;
        private set => SetField(ref templateName, value);
    }

    public string TargetDirectory
    {
        get => targetDirectory;
        private set => SetField(ref targetDirectory, value);
    }

    public string ErrorMessage
    {
        get => errorMessage;
        private set => SetField(ref errorMessage, value);
    }

    public bool IsCancellationRequested
    {
        get => isCancellationRequested;
        private set
        {
            if (SetField(ref isCancellationRequested, value))
            {
                OnPropertyChanged(nameof(CanCancel));
            }
        }
    }

    public string DataStepState
    {
        get => dataStepState;
        private set => SetField(ref dataStepState, value);
    }

    public string TemplateStepState
    {
        get => templateStepState;
        private set => SetField(ref templateStepState, value);
    }

    public string FilesStepState
    {
        get => filesStepState;
        private set => SetField(ref filesStepState, value);
    }

    public string CatalogStepState
    {
        get => catalogStepState;
        private set => SetField(ref catalogStepState, value);
    }

    public void Start(string projectName, string templateName, string targetDirectory)
    {
        ProjectName = projectName?.Trim() ?? string.Empty;
        TemplateName = templateName?.Trim() ?? string.Empty;
        TargetDirectory = targetDirectory?.Trim() ?? string.Empty;
        ErrorMessage = string.Empty;
        HasError = false;
        IsCompleted = false;
        IsCanceled = false;
        IsCancellationRequested = false;
        OverallPercent = 0;
        activeStepIndex = (int)BuildCreationProcessStage.Data;
        CurrentStep = "Данные проекта";
        StatusText = "Готовлю создание сборки";
        RefreshStepStates();
        IsRunning = true;
        IsVisible = true;
    }

    public void SetStage(BuildCreationProcessStage stage, double overallPercent, string currentStep = "", string statusText = "")
    {
        if (!IsVisible || IsCompleted || HasError)
        {
            return;
        }

        int requestedStepIndex = ClampStepIndex((int)stage);
        if (requestedStepIndex < activeStepIndex)
        {
            OverallPercent = Math.Max(OverallPercent, overallPercent);
            return;
        }

        activeStepIndex = requestedStepIndex;
        OverallPercent = Math.Max(OverallPercent, overallPercent);
        CurrentStep = string.IsNullOrWhiteSpace(currentStep) ? GetDefaultStepName(activeStepIndex) : currentStep.Trim();

        if (!string.IsNullOrWhiteSpace(statusText))
        {
            StatusText = statusText.Trim();
        }

        RefreshStepStates();
    }

    public void RequestCancel()
    {
        if (!CanCancel)
        {
            return;
        }

        IsCancellationRequested = true;
        CurrentStep = "Отмена создания сборки";
        StatusText = "Дожидаюсь безопасной остановки";
        RefreshStepStates();
    }

    public void Complete(string projectNameOrDirectory)
    {
        string completionTarget = string.IsNullOrWhiteSpace(projectNameOrDirectory)
            ? ProjectName
            : projectNameOrDirectory.Trim();

        if (!string.IsNullOrWhiteSpace(completionTarget))
        {
            StatusText = $"{completionTarget} готова";
        }
        else
        {
            StatusText = "Готово";
        }

        activeStepIndex = LastStepIndex;
        IsCancellationRequested = false;
        ErrorMessage = string.Empty;
        HasError = false;
        IsCanceled = false;
        IsRunning = false;
        IsCompleted = true;
        IsVisible = true;
        OverallPercent = 100;
        CurrentStep = "Сборка создана";
        RefreshStepStates();
    }

    public void Fail(string message)
    {
        ErrorMessage = string.IsNullOrWhiteSpace(message)
            ? "Не удалось создать сборку."
            : message.Trim();
        CurrentStep = "Не удалось создать сборку";
        StatusText = "Создание остановлено";
        HasError = true;
        IsCompleted = false;
        IsCanceled = false;
        IsRunning = false;
        IsVisible = true;
        RefreshStepStates();
    }

    public void Cancel()
    {
        CurrentStep = "Создание отменено";
        StatusText = "Операция остановлена";
        ErrorMessage = string.Empty;
        HasError = false;
        IsCompleted = false;
        IsCanceled = true;
        IsRunning = false;
        IsVisible = true;
        IsCancellationRequested = true;
        RefreshStepStates();
    }

    public void Close()
    {
        if (IsRunning)
        {
            return;
        }

        IsVisible = false;
        IsCompleted = false;
        IsCanceled = false;
        HasError = false;
        IsCancellationRequested = false;
        ErrorMessage = string.Empty;
        OverallPercent = 0;
        activeStepIndex = (int)BuildCreationProcessStage.Data;
        CurrentStep = "Данные проекта";
        StatusText = "Готовлю создание сборки";
        RefreshStepStates();
    }

    public static string ResolveStepState(
        BuildCreationProcessStage step,
        BuildCreationProcessStage activeStep,
        bool isCompleted,
        bool isCancellationRequested)
    {
        return ResolveStepState((int)step, (int)activeStep, isCompleted, isCancellationRequested);
    }

    public static string ResolveStepState(
        int stepIndex,
        int activeStepIndex,
        bool isCompleted,
        bool isCancellationRequested)
    {
        stepIndex = ClampStepIndex(stepIndex);
        activeStepIndex = ClampStepIndex(activeStepIndex);

        if (isCompleted)
        {
            return CompleteStepState;
        }

        if (stepIndex < activeStepIndex)
        {
            return CompleteStepState;
        }

        if (stepIndex == activeStepIndex)
        {
            return isCancellationRequested ? CancelingStepState : ActiveStepState;
        }

        return PendingStepState;
    }

    private void RefreshStepStates()
    {
        DataStepState = ResolveStepState(
            BuildCreationProcessStage.Data,
            (BuildCreationProcessStage)activeStepIndex,
            IsCompleted,
            IsCancellationRequested);
        TemplateStepState = ResolveStepState(
            BuildCreationProcessStage.Template,
            (BuildCreationProcessStage)activeStepIndex,
            IsCompleted,
            IsCancellationRequested);
        FilesStepState = ResolveStepState(
            BuildCreationProcessStage.Files,
            (BuildCreationProcessStage)activeStepIndex,
            IsCompleted,
            IsCancellationRequested);
        CatalogStepState = ResolveStepState(
            BuildCreationProcessStage.Catalog,
            (BuildCreationProcessStage)activeStepIndex,
            IsCompleted,
            IsCancellationRequested);
    }

    private static string GetDefaultStepName(int stepIndex)
    {
        return ClampStepIndex(stepIndex) switch
        {
            (int)BuildCreationProcessStage.Template => "Шаблон",
            (int)BuildCreationProcessStage.Files => "Файлы сборки",
            (int)BuildCreationProcessStage.Catalog => "Каталог сборок",
            _ => "Данные проекта"
        };
    }

    private static int ClampStepIndex(int stepIndex)
    {
        return Math.Clamp(stepIndex, (int)BuildCreationProcessStage.Data, LastStepIndex);
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
