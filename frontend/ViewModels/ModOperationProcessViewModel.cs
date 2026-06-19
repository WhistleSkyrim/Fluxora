using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Fluxora.App.ViewModels;

public sealed class ModOperationProcessViewModel : INotifyPropertyChanged
{
    private bool isVisible;
    private bool isRunning;
    private bool isCompleted;
    private string title = "Операция с модом";
    private string currentStep = "Подготовка";
    private string statusText = string.Empty;
    private string errorMessage = string.Empty;
    private double overallPercent;

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
        private set
        {
            if (SetField(ref isCompleted, value))
            {
                OnPropertyChanged(nameof(CanClose));
            }
        }
    }

    public bool HasError => !string.IsNullOrWhiteSpace(ErrorMessage);

    public bool CanClose => IsVisible && !IsRunning;

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

    public string ErrorMessage
    {
        get => errorMessage;
        private set
        {
            if (SetField(ref errorMessage, value))
            {
                OnPropertyChanged(nameof(HasError));
            }
        }
    }

    public double OverallPercent
    {
        get => overallPercent;
        private set => SetField(ref overallPercent, Math.Clamp(value, 0, 100));
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public void Start(string title, string currentStep, string statusText)
    {
        Title = string.IsNullOrWhiteSpace(title) ? "Операция с модом" : title;
        CurrentStep = string.IsNullOrWhiteSpace(currentStep) ? "Подготовка" : currentStep;
        StatusText = statusText;
        ErrorMessage = string.Empty;
        OverallPercent = 0;
        IsCompleted = false;
        IsRunning = true;
        IsVisible = true;
    }

    public void ApplyProgress(string currentStep, string statusText, double percent)
    {
        if (!IsVisible)
        {
            return;
        }

        if (!string.IsNullOrWhiteSpace(currentStep))
        {
            CurrentStep = currentStep;
        }

        if (!string.IsNullOrWhiteSpace(statusText))
        {
            StatusText = statusText;
        }

        OverallPercent = Math.Max(OverallPercent, Math.Clamp(percent, 0, 100));
    }

    public void Complete(string currentStep, string statusText)
    {
        ApplyProgress(currentStep, statusText, 100);
        ErrorMessage = string.Empty;
        IsRunning = false;
        IsCompleted = true;
    }

    public void Fail(string message)
    {
        ErrorMessage = string.IsNullOrWhiteSpace(message)
            ? "Операция не выполнена."
            : message;
        CurrentStep = "Ошибка";
        StatusText = "Проверьте детали и попробуйте снова.";
        IsRunning = false;
        IsCompleted = false;
        IsVisible = true;
    }

    public void Close()
    {
        if (!CanClose)
        {
            return;
        }

        Reset();
    }

    public void Reset()
    {
        IsRunning = false;
        IsCompleted = false;
        IsVisible = false;
        ErrorMessage = string.Empty;
        OverallPercent = 0;
        Title = "Операция с модом";
        CurrentStep = "Подготовка";
        StatusText = string.Empty;
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
