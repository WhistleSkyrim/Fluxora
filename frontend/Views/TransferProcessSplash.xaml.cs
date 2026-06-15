using System.ComponentModel;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Views;

public partial class TransferProcessSplash : System.Windows.Controls.UserControl
{
    private const int StepFadeOutMilliseconds = 160;
    private const int StepFadeInMilliseconds = 220;
    private const int StateCrossfadeMilliseconds = 320;

    public static readonly DependencyProperty CloseCommandProperty = DependencyProperty.Register(
        nameof(CloseCommand),
        typeof(ICommand),
        typeof(TransferProcessSplash),
        new PropertyMetadata(null));

    private INotifyPropertyChanged? notifySource;
    private string currentStep = string.Empty;
    private bool isCompletedState;

    public TransferProcessSplash()
    {
        InitializeComponent();
        DataContextChanged += OnSplashDataContextChanged;
    }

    public ICommand? CloseCommand
    {
        get => (ICommand?)GetValue(CloseCommandProperty);
        set => SetValue(CloseCommandProperty, value);
    }

    private void OnSplashDataContextChanged(object sender, DependencyPropertyChangedEventArgs e)
    {
        if (notifySource is not null)
        {
            notifySource.PropertyChanged -= OnSourcePropertyChanged;
        }

        notifySource = e.NewValue as INotifyPropertyChanged;
        if (notifySource is not null)
        {
            notifySource.PropertyChanged += OnSourcePropertyChanged;
        }

        SetStepText(GetStepFromDataContext());
        SyncCompletionState(animate: false);
    }

    private void OnSourcePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        switch (e.PropertyName)
        {
            case nameof(SettingsWindowViewModel.TransferCurrentStep):
                Dispatcher.Invoke(() => AnimateStepChange(GetStepFromDataContext()));
                break;
            case nameof(SettingsWindowViewModel.IsTransferCompleted):
            case nameof(SettingsWindowViewModel.IsTransferProcessVisible):
                Dispatcher.Invoke(() => SyncCompletionState(animate: true));
                break;
        }
    }

    private string GetStepFromDataContext()
    {
        return DataContext is SettingsWindowViewModel viewModel
            ? viewModel.TransferCurrentStep
            : string.Empty;
    }

    private void SetStepText(string step)
    {
        currentStep = step;
        StepText.Text = step;
        StepText.Opacity = 1;
        StepTranslate.Y = 0;
    }

    private void AnimateStepChange(string nextStep)
    {
        if (string.IsNullOrWhiteSpace(nextStep) ||
            string.Equals(currentStep, nextStep, StringComparison.Ordinal))
        {
            return;
        }

        DoubleAnimation fadeOut = new(0, TimeSpan.FromMilliseconds(StepFadeOutMilliseconds))
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseIn }
        };
        DoubleAnimation slideOut = new(-8, TimeSpan.FromMilliseconds(StepFadeOutMilliseconds))
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseIn }
        };

        fadeOut.Completed += (_, _) =>
        {
            currentStep = nextStep;
            StepText.Text = nextStep;
            StepTranslate.Y = 8;

            DoubleAnimation fadeIn = new(1, TimeSpan.FromMilliseconds(StepFadeInMilliseconds))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            };
            DoubleAnimation slideIn = new(0, TimeSpan.FromMilliseconds(StepFadeInMilliseconds))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            };

            StepText.BeginAnimation(OpacityProperty, fadeIn);
            StepTranslate.BeginAnimation(TranslateTransform.YProperty, slideIn);
        };

        StepText.BeginAnimation(OpacityProperty, fadeOut);
        StepTranslate.BeginAnimation(TranslateTransform.YProperty, slideOut);
    }

    private void SyncCompletionState(bool animate)
    {
        bool completed = DataContext is SettingsWindowViewModel { IsTransferCompleted: true };
        if (completed == isCompletedState && animate)
        {
            return;
        }

        isCompletedState = completed;
        double spinnerTarget = completed ? 0 : 1;
        double doneTarget = completed ? 1 : 0;

        if (!animate)
        {
            SpinnerLayer.Opacity = spinnerTarget;
            DoneLayer.Opacity = doneTarget;
            return;
        }

        Duration duration = new(TimeSpan.FromMilliseconds(StateCrossfadeMilliseconds));
        IEasingFunction easing = new CubicEase { EasingMode = EasingMode.EaseInOut };
        SpinnerLayer.BeginAnimation(OpacityProperty, new DoubleAnimation(spinnerTarget, duration) { EasingFunction = easing });
        DoneLayer.BeginAnimation(OpacityProperty, new DoubleAnimation(doneTarget, duration) { EasingFunction = easing });
    }
}
