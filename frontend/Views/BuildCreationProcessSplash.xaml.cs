using System.ComponentModel;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media.Animation;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Views;

public partial class BuildCreationProcessSplash : System.Windows.Controls.UserControl
{
    private const int StepFadeOutMilliseconds = 160;
    private const int StepFadeInMilliseconds = 220;
    private const int StateCrossfadeMilliseconds = 320;

    public static readonly DependencyProperty CancelCommandProperty = DependencyProperty.Register(
        nameof(CancelCommand),
        typeof(ICommand),
        typeof(BuildCreationProcessSplash),
        new PropertyMetadata(null));

    public static readonly DependencyProperty CloseCommandProperty = DependencyProperty.Register(
        nameof(CloseCommand),
        typeof(ICommand),
        typeof(BuildCreationProcessSplash),
        new PropertyMetadata(null));

    private INotifyPropertyChanged? notifySource;
    private string currentStep = string.Empty;
    private bool isFinalState;

    public BuildCreationProcessSplash()
    {
        InitializeComponent();
        DataContextChanged += OnSplashDataContextChanged;
    }

    public ICommand? CancelCommand
    {
        get => (ICommand?)GetValue(CancelCommandProperty);
        set => SetValue(CancelCommandProperty, value);
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
        SyncState(animate: false);
    }

    private void OnSourcePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        switch (e.PropertyName)
        {
            case nameof(BuildCreationProcessViewModel.CurrentStep):
                Dispatcher.Invoke(() => AnimateStepChange(GetStepFromDataContext()));
                break;
            case nameof(BuildCreationProcessViewModel.IsVisible):
            case nameof(BuildCreationProcessViewModel.IsCompleted):
            case nameof(BuildCreationProcessViewModel.IsCanceled):
            case nameof(BuildCreationProcessViewModel.HasError):
                Dispatcher.Invoke(() => SyncState(animate: true));
                break;
        }
    }

    private string GetStepFromDataContext()
    {
        return DataContext is BuildCreationProcessViewModel viewModel
            ? viewModel.CurrentStep
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
            StepTranslate.BeginAnimation(System.Windows.Media.TranslateTransform.YProperty, slideIn);
        };

        StepText.BeginAnimation(OpacityProperty, fadeOut);
        StepTranslate.BeginAnimation(System.Windows.Media.TranslateTransform.YProperty, slideOut);
    }

    private void SyncState(bool animate)
    {
        if (DataContext is not BuildCreationProcessViewModel viewModel || !viewModel.IsVisible)
        {
            isFinalState = false;
            SpinnerLayer.Opacity = 1;
            DoneLayer.Opacity = 0;
            return;
        }

        bool nextFinalState = viewModel.IsCompleted || viewModel.IsCanceled || viewModel.HasError;
        if (nextFinalState == isFinalState && animate)
        {
            SyncStateGlyph(viewModel);
            return;
        }

        isFinalState = nextFinalState;
        SyncStateGlyph(viewModel);

        double spinnerTarget = nextFinalState ? 0 : 1;
        double doneTarget = nextFinalState ? 1 : 0;

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

    private void SyncStateGlyph(BuildCreationProcessViewModel viewModel)
    {
        StateGlyph.Text = viewModel.HasError
            ? "\uE7BA"
            : viewModel.IsCanceled
                ? "\uE711"
                : "\uE73E";
        StateGlyph.Foreground = (System.Windows.Media.Brush)FindResource(
            viewModel.HasError
                ? "CreationErrorBrush"
                : viewModel.IsCanceled
                    ? "CreationCancelBrush"
                    : "CreationSuccessBrush");
    }
}
