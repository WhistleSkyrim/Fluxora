using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO;
using System.Runtime.CompilerServices;
using Fluxora.App.Models;

namespace Fluxora.App.ViewModels;

public sealed class FluxPackInstallProcessViewModel : INotifyPropertyChanged
{
    private bool isVisible;
    private bool isRunning;
    private bool isCompleted;
    private bool hasError;
    private int overallPercent;
    private string currentStep = "Ожидание FluxPack";
    private string currentItem = string.Empty;
    private string statusText = "Выберите FluxPack для установки";
    private string sourceSummaryText = "Источники ещё не прочитаны";
    private string resultText = string.Empty;
    private string errorMessage = string.Empty;
    private string fluxPackPath = string.Empty;
    private string buildName = "Сборка";

    public event PropertyChangedEventHandler? PropertyChanged;

    public ObservableCollection<FluxPackInstallProviderViewModel> Providers { get; } = new();

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

    public int OverallPercent
    {
        get => overallPercent;
        private set => SetField(ref overallPercent, Math.Clamp(value, 0, 100));
    }

    public string CurrentStep
    {
        get => currentStep;
        private set => SetField(ref currentStep, value);
    }

    public string CurrentItem
    {
        get => currentItem;
        private set => SetField(ref currentItem, value);
    }

    public string StatusText
    {
        get => statusText;
        private set => SetField(ref statusText, value);
    }

    public string SourceSummaryText
    {
        get => sourceSummaryText;
        private set => SetField(ref sourceSummaryText, value);
    }

    public string ResultText
    {
        get => resultText;
        private set => SetField(ref resultText, value);
    }

    public string ErrorMessage
    {
        get => errorMessage;
        private set => SetField(ref errorMessage, value);
    }

    public string FluxPackPath
    {
        get => fluxPackPath;
        private set => SetField(ref fluxPackPath, value);
    }

    public string BuildName
    {
        get => buildName;
        private set => SetField(ref buildName, value);
    }

    public void Start(string selectedFluxPackPath)
    {
        FluxPackPath = selectedFluxPackPath?.Trim() ?? string.Empty;
        BuildName = string.IsNullOrWhiteSpace(FluxPackPath)
            ? "Сборка"
            : Path.GetFileNameWithoutExtension(FluxPackPath);
        Providers.Clear();
        ErrorMessage = string.Empty;
        ResultText = string.Empty;
        HasError = false;
        IsCompleted = false;
        OverallPercent = 0;
        CurrentStep = "FluxPack читается";
        CurrentItem = Path.GetFileName(FluxPackPath);
        StatusText = "Проверяем формат и install plan";
        SourceSummaryText = "Источники ещё не прочитаны";
        IsRunning = true;
        IsVisible = true;
    }

    public void ApplyProgress(FluxPackInstallProgress progress)
    {
        if (!IsVisible || IsCompleted || HasError)
        {
            return;
        }

        OverallPercent = progress.OverallPercent;
        CurrentStep = string.IsNullOrWhiteSpace(progress.CurrentStep)
            ? CurrentStep
            : progress.CurrentStep.Trim();
        CurrentItem = progress.CurrentItem?.Trim() ?? string.Empty;
        StatusText = string.IsNullOrWhiteSpace(progress.StatusMessage)
            ? StatusText
            : progress.StatusMessage.Trim();
        SourceSummaryText = FormatSourceSummary(progress);
        SyncProviders(progress.Providers);
    }

    public void Complete(FluxPackInstallResult result)
    {
        BuildName = string.IsNullOrWhiteSpace(result.BuildName)
            ? BuildName
            : result.BuildName.Trim();
        OverallPercent = 100;
        IsRunning = false;
        IsCompleted = true;
        HasError = false;
        ErrorMessage = string.Empty;
        CurrentStep = result.HasWarnings ? "Установка завершена с предупреждениями" : "Сборка установлена";
        CurrentItem = BuildName;
        StatusText = result.HasWarnings
            ? "Часть источников не была установлена"
            : "FluxPack установлен";
        SourceSummaryText = FormatResultSources(result);
        ResultText = $"Конфиги: {result.AppliedConfigCount}, порядок профиля: {result.AppliedProfileOrderItemCount}";
        IsVisible = true;
    }

    public void Fail(string message)
    {
        ErrorMessage = string.IsNullOrWhiteSpace(message)
            ? "Не удалось установить FluxPack."
            : message.Trim();
        CurrentStep = "FluxPack не установлен";
        StatusText = "Установка остановлена";
        HasError = true;
        IsCompleted = false;
        IsRunning = false;
        IsVisible = true;
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
        ResultText = string.Empty;
        OverallPercent = 0;
        Providers.Clear();
        CurrentStep = "Ожидание FluxPack";
        CurrentItem = string.Empty;
        StatusText = "Выберите FluxPack для установки";
        SourceSummaryText = "Источники ещё не прочитаны";
    }

    private void SyncProviders(IReadOnlyList<FluxPackInstallProviderProgress>? providers)
    {
        if (providers is null)
        {
            return;
        }

        HashSet<string> seen = new(StringComparer.OrdinalIgnoreCase);
        foreach (FluxPackInstallProviderProgress provider in providers)
        {
            string key = string.IsNullOrWhiteSpace(provider.ProviderId)
                ? "unknown"
                : provider.ProviderId.Trim();
            seen.Add(key);

            FluxPackInstallProviderViewModel? existing =
                Providers.FirstOrDefault(item => string.Equals(item.ProviderId, key, StringComparison.OrdinalIgnoreCase));
            if (existing is null)
            {
                existing = new FluxPackInstallProviderViewModel();
                Providers.Add(existing);
            }

            existing.Apply(provider);
        }

        for (int index = Providers.Count - 1; index >= 0; --index)
        {
            if (!seen.Contains(Providers[index].ProviderId))
            {
                Providers.RemoveAt(index);
            }
        }
    }

    private static string FormatSourceSummary(FluxPackInstallProgress progress)
    {
        ulong failed = progress.FailedSourceCount + progress.PendingSourceCount;
        ulong processed = progress.InstalledSourceCount + failed;
        ulong remaining = progress.TotalSourceCount > processed ? progress.TotalSourceCount - processed : 0;
        return $"Источники: {progress.InstalledSourceCount}/{progress.TotalSourceCount} установлено, осталось {remaining}, ошибок {failed}";
    }

    private static string FormatResultSources(FluxPackInstallResult result)
    {
        ulong failed = result.FailedSourceCount + result.PendingSourceCount;
        return $"Источники: {result.InstalledSourceCount}/{result.TotalSourceCount} установлено, ошибок {failed}";
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
