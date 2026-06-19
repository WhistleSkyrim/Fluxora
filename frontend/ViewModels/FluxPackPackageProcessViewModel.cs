using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using Fluxora.App.Models;

namespace Fluxora.App.ViewModels;

public enum FluxPackPackageProcessStage
{
    Prepare = 0,
    Composition = 1,
    Export = 2,
    Summary = 3
}

public sealed class FluxPackPackageProcessViewModel : INotifyPropertyChanged
{
    public const string PendingStepState = "Pending";
    public const string ActiveStepState = "Active";
    public const string CompleteStepState = "Complete";

    private const int LastStepIndex = (int)FluxPackPackageProcessStage.Summary;

    private bool isVisible;
    private bool isRunning;
    private bool isCompleted;
    private bool hasError;
    private bool includeGeneratedAssets;
    private double overallPercent;
    private int activeStepIndex;
    private string currentStep = "Подготовка";
    private string statusText = "Готовлю упаковку сборки";
    private string buildName = string.Empty;
    private string outputPath = string.Empty;
    private string compositionText = "Состав FluxPack уточняется";
    private string summaryText = "Манифест ещё не записан";
    private string errorMessage = string.Empty;
    private string prepareStepState = ActiveStepState;
    private string compositionStepState = PendingStepState;
    private string exportStepState = PendingStepState;
    private string summaryStepState = PendingStepState;

    public event PropertyChangedEventHandler? PropertyChanged;

    public bool IsVisible
    {
        get => isVisible;
        private set
        {
            if (SetField(ref isVisible, value))
            {
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
                OnPropertyChanged(nameof(CanClose));
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

    public bool CanClose => IsVisible && !IsRunning;

    public bool IncludeGeneratedAssets
    {
        get => includeGeneratedAssets;
        private set => SetField(ref includeGeneratedAssets, value);
    }

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

    public string BuildName
    {
        get => buildName;
        private set => SetField(ref buildName, value);
    }

    public string OutputPath
    {
        get => outputPath;
        private set => SetField(ref outputPath, value);
    }

    public string CompositionText
    {
        get => compositionText;
        private set => SetField(ref compositionText, value);
    }

    public string SummaryText
    {
        get => summaryText;
        private set => SetField(ref summaryText, value);
    }

    public string ErrorMessage
    {
        get => errorMessage;
        private set => SetField(ref errorMessage, value);
    }

    public string PrepareStepState
    {
        get => prepareStepState;
        private set => SetField(ref prepareStepState, value);
    }

    public string CompositionStepState
    {
        get => compositionStepState;
        private set => SetField(ref compositionStepState, value);
    }

    public string ExportStepState
    {
        get => exportStepState;
        private set => SetField(ref exportStepState, value);
    }

    public string SummaryStepState
    {
        get => summaryStepState;
        private set => SetField(ref summaryStepState, value);
    }

    public void Start(string buildName, string outputPath, bool includeGeneratedAssets)
    {
        BuildName = string.IsNullOrWhiteSpace(buildName) ? "Сборка" : buildName.Trim();
        OutputPath = outputPath?.Trim() ?? string.Empty;
        IncludeGeneratedAssets = includeGeneratedAssets;
        ErrorMessage = string.Empty;
        HasError = false;
        IsCompleted = false;
        OverallPercent = 0;
        activeStepIndex = (int)FluxPackPackageProcessStage.Prepare;
        CurrentStep = "Подготовка FluxPack";
        StatusText = "Проверяем сборку и путь сохранения";
        CompositionText = includeGeneratedAssets
            ? "Generated assets войдут в FluxPack"
            : "Generated assets останутся вне FluxPack";
        SummaryText = "Манифест ещё не записан";
        RefreshStepStates();
        IsRunning = true;
        IsVisible = true;
    }

    public void SetStage(
        FluxPackPackageProcessStage stage,
        double overallPercent,
        string currentStep = "",
        string statusText = "",
        string summaryText = "")
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
        CurrentStep = string.IsNullOrWhiteSpace(currentStep)
            ? GetDefaultStepName(activeStepIndex)
            : currentStep.Trim();

        if (!string.IsNullOrWhiteSpace(statusText))
        {
            StatusText = statusText.Trim();
        }

        if (!string.IsNullOrWhiteSpace(summaryText))
        {
            SummaryText = summaryText.Trim();
        }

        RefreshStepStates();
    }

    public void Complete(FluxPackSummary summary)
    {
        string completedBuildName = string.IsNullOrWhiteSpace(summary.BuildName)
            ? BuildName
            : summary.BuildName.Trim();
        string completedPath = string.IsNullOrWhiteSpace(summary.OutputPath)
            ? OutputPath
            : summary.OutputPath.Trim();

        if (!string.IsNullOrWhiteSpace(completedBuildName))
        {
            BuildName = completedBuildName;
        }

        if (!string.IsNullOrWhiteSpace(completedPath))
        {
            OutputPath = completedPath;
        }

        activeStepIndex = LastStepIndex;
        IsRunning = false;
        IsCompleted = true;
        HasError = false;
        ErrorMessage = string.Empty;
        OverallPercent = 100;
        CurrentStep = "FluxPack готов";
        StatusText = string.IsNullOrWhiteSpace(completedPath)
            ? "Упаковка завершена"
            : $"Сохранено: {Path.GetFileName(completedPath)}";
        CompositionText = summary.GeneratedAssetsIncluded
            ? "Generated assets включены"
            : "Generated assets не включались";
        SummaryText = FormatSummary(summary);
        IsVisible = true;
        RefreshStepStates();
    }

    public void Fail(string message)
    {
        ErrorMessage = string.IsNullOrWhiteSpace(message)
            ? "Не удалось упаковать сборку."
            : message.Trim();
        CurrentStep = "Не удалось упаковать сборку";
        StatusText = "Упаковка остановлена";
        HasError = true;
        IsCompleted = false;
        IsRunning = false;
        IsVisible = true;
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
        HasError = false;
        ErrorMessage = string.Empty;
        OverallPercent = 0;
        activeStepIndex = (int)FluxPackPackageProcessStage.Prepare;
        CurrentStep = "Подготовка";
        StatusText = "Готовлю упаковку сборки";
        CompositionText = "Состав FluxPack уточняется";
        SummaryText = "Манифест ещё не записан";
        RefreshStepStates();
    }

    public static string ResolveStepState(
        FluxPackPackageProcessStage step,
        FluxPackPackageProcessStage activeStep,
        bool isCompleted)
    {
        return ResolveStepState((int)step, (int)activeStep, isCompleted);
    }

    public static string ResolveStepState(int stepIndex, int activeStepIndex, bool isCompleted)
    {
        stepIndex = ClampStepIndex(stepIndex);
        activeStepIndex = ClampStepIndex(activeStepIndex);

        if (isCompleted || stepIndex < activeStepIndex)
        {
            return CompleteStepState;
        }

        return stepIndex == activeStepIndex ? ActiveStepState : PendingStepState;
    }

    private void RefreshStepStates()
    {
        PrepareStepState = ResolveStepState(
            FluxPackPackageProcessStage.Prepare,
            (FluxPackPackageProcessStage)activeStepIndex,
            IsCompleted);
        CompositionStepState = ResolveStepState(
            FluxPackPackageProcessStage.Composition,
            (FluxPackPackageProcessStage)activeStepIndex,
            IsCompleted);
        ExportStepState = ResolveStepState(
            FluxPackPackageProcessStage.Export,
            (FluxPackPackageProcessStage)activeStepIndex,
            IsCompleted);
        SummaryStepState = ResolveStepState(
            FluxPackPackageProcessStage.Summary,
            (FluxPackPackageProcessStage)activeStepIndex,
            IsCompleted);
    }

    private static string FormatSummary(FluxPackSummary summary)
    {
        string manifestSize = summary.ManifestBytes == 0
            ? "размер уточняется"
            : TransferDriveOption.FormatBytes(summary.ManifestBytes);
        return $"Источники: {summary.SourceArchiveCount}, ассеты: {summary.GeneratedAssetCount}, патчи: {summary.CustomPatchCount}, конфиги: {summary.CustomConfigCount}, манифест: {manifestSize}";
    }

    private static string GetDefaultStepName(int stepIndex)
    {
        return ClampStepIndex(stepIndex) switch
        {
            (int)FluxPackPackageProcessStage.Composition => "Собираем состав",
            (int)FluxPackPackageProcessStage.Export => "Записываем FluxPack",
            (int)FluxPackPackageProcessStage.Summary => "Проверяем результат",
            _ => "Подготовка FluxPack"
        };
    }

    private static int ClampStepIndex(int stepIndex)
    {
        return Math.Clamp(stepIndex, (int)FluxPackPackageProcessStage.Prepare, LastStepIndex);
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
