using System.ComponentModel;
using System.Runtime.CompilerServices;
using Fluxora.App.Models;

namespace Fluxora.App.ViewModels;

public sealed class StartupSplashViewModel : INotifyPropertyChanged, IProgress<StartupProgress>
{
    private string title = "Запускаем Fluxora";
    private string detail = "Готовим окно и проверяем окружение";
    private double progress = 4;

    public event PropertyChangedEventHandler? PropertyChanged;

    public string AppName => "Fluxora";

    public string Title
    {
        get => title;
        private set => SetField(ref title, value);
    }

    public string Detail
    {
        get => detail;
        private set => SetField(ref detail, value);
    }

    public double Progress
    {
        get => progress;
        private set
        {
            double nextValue = Math.Clamp(value, 0, 100);
            if (SetField(ref progress, nextValue))
            {
                OnPropertyChanged(nameof(ProgressText));
            }
        }
    }

    public string ProgressText => $"{Progress:0}%";

    public void Report(StartupProgress value)
    {
        Title = string.IsNullOrWhiteSpace(value.Title) ? Title : value.Title;
        Detail = string.IsNullOrWhiteSpace(value.Detail) ? Detail : value.Detail;
        Progress = value.Percent;
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
