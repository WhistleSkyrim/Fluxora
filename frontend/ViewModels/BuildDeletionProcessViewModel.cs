using System.ComponentModel;
using System.Runtime.CompilerServices;
using Fluxora.App.Models;

namespace Fluxora.App.ViewModels;

public sealed class BuildDeletionProcessViewModel : INotifyPropertyChanged
{
    private bool isVisible;
    private bool isRunning;
    private bool isCompleted;
    private bool hasError;
    private double overallPercent;
    private string currentStep = "Подготовка";
    private string statusText = "Готовлю удаление";
    private string bytesText = "Считаю размер";
    private string entriesText = "Считаю элементы";
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

    public string BytesText
    {
        get => bytesText;
        private set => SetField(ref bytesText, value);
    }

    public string EntriesText
    {
        get => entriesText;
        private set => SetField(ref entriesText, value);
    }

    public string ErrorMessage
    {
        get => errorMessage;
        private set => SetField(ref errorMessage, value);
    }

    public void Start()
    {
        IsVisible = true;
        IsRunning = true;
        IsCompleted = false;
        HasError = false;
        OverallPercent = 0;
        CurrentStep = "Подготовка";
        StatusText = "Готовлю удаление";
        BytesText = "Считаю размер";
        EntriesText = "Считаю элементы";
        ErrorMessage = string.Empty;
    }

    public void ApplyProgress(BuildDeletionProgress progress)
    {
        if (!IsVisible)
        {
            return;
        }

        if (string.Equals(progress.Phase, "complete", StringComparison.OrdinalIgnoreCase))
        {
            Complete();
            return;
        }

        if (IsCompleted || HasError)
        {
            return;
        }

        CurrentStep = NormalizeStep(progress.CurrentStep);
        OverallPercent = Math.Max(OverallPercent, progress.OverallPercent);
        BytesText = FormatBytesText(progress);
        EntriesText = FormatEntriesText(progress);
        StatusText = FormatStatusText(progress);
    }

    public void Complete()
    {
        IsRunning = false;
        IsCompleted = true;
        HasError = false;
        OverallPercent = 100;
        CurrentStep = "Сборка удалена";
        StatusText = "Готово";
        BytesText = "Все файлы удалены";
        EntriesText = "Список сборок обновляется";
        ErrorMessage = string.Empty;
    }

    public void Fail(string message)
    {
        IsRunning = false;
        IsCompleted = false;
        HasError = true;
        CurrentStep = "Не удалось удалить сборку";
        StatusText = "Удаление остановлено";
        ErrorMessage = LocalizeDeleteError(message);
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
    }

    private static string NormalizeStep(string step)
    {
        if (string.IsNullOrWhiteSpace(step))
        {
            return "Удаляю сборку";
        }

        if (step.Contains("конфиг", StringComparison.OrdinalIgnoreCase))
        {
            return "Завершаю удаление";
        }

        if (step.Contains("папк", StringComparison.OrdinalIgnoreCase))
        {
            return "Удаляю файлы сборки";
        }

        return step;
    }

    private static string FormatBytesText(BuildDeletionProgress progress)
    {
        return progress.TotalBytes == 0
            ? "Размер уточняется"
            : $"{TransferDriveOption.FormatBytes(progress.DeletedBytes)} из {TransferDriveOption.FormatBytes(progress.TotalBytes)}";
    }

    private static string FormatEntriesText(BuildDeletionProgress progress)
    {
        return progress.TotalEntries == 0
            ? "Считаю элементы"
            : $"{progress.DeletedEntries} / {progress.TotalEntries} элементов";
    }

    private static string FormatStatusText(BuildDeletionProgress progress)
    {
        if (progress.TotalBytes > 0)
        {
            return $"Удалено {TransferDriveOption.FormatBytes(progress.DeletedBytes)}";
        }

        if (progress.TotalEntries > 0)
        {
            return $"Обработано {progress.DeletedEntries} из {progress.TotalEntries} элементов";
        }

        return "Готовлю список файлов";
    }

    private static string LocalizeDeleteError(string message)
    {
        if (message.Contains("directory is not empty", StringComparison.OrdinalIgnoreCase))
        {
            return "Windows не успела освободить одну из папок. Повторите удаление ещё раз.";
        }

        if (message.Contains("access is denied", StringComparison.OrdinalIgnoreCase))
        {
            return "Нет доступа к одному из файлов сборки. Проверьте права на папку и повторите удаление.";
        }

        if (message.Contains("being used by another process", StringComparison.OrdinalIgnoreCase) ||
            message.Contains("locked by another process", StringComparison.OrdinalIgnoreCase))
        {
            return "Один из файлов занят другим процессом. Закройте связанные приложения и повторите удаление.";
        }

        if (message.Contains("path too long", StringComparison.OrdinalIgnoreCase) ||
            message.Contains("filename or extension is too long", StringComparison.OrdinalIgnoreCase))
        {
            return "Windows отклонила слишком длинный путь. Переместите сборку ближе к корню диска и повторите удаление.";
        }

        return "Удаление остановлено. Повторите попытку или закройте приложения, которые могли обратиться к папке сборки.";
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
