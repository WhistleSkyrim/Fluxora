using System.ComponentModel;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace Fluxora.App.Views;

public partial class CreateProjectWizardSplash : System.Windows.Controls.UserControl
{
    private const string StepIndexPropertyName = "CreateProjectStepIndex";
    private const string IsPanelOpenPropertyName = "IsCreateProjectPanelOpen";
    private const string GameSearchTextPropertyName = "GameSearchText";
    private const double StepTravel = 44;
    private INotifyPropertyChanged? observedViewModel;
    private int currentStepIndex;
    private bool hasAppliedInitialStep;

    public CreateProjectWizardSplash()
    {
        InitializeComponent();
        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
        DataContextChanged += OnDataContextChanged;
    }

    private FrameworkElement[] StepPanels => new[]
    {
        NameStepPanel,
        GameStepPanel,
        ExeStepPanel,
        InstallStepPanel
    };

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        ApplyStepVisibility(ResolveStepIndex(), animate: false);
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        ObserveViewModel(null);
    }

    private void OnDataContextChanged(object sender, DependencyPropertyChangedEventArgs e)
    {
        ObserveViewModel(e.NewValue as INotifyPropertyChanged);
        ApplyStepVisibility(ResolveStepIndex(), animate: false);
    }

    private void ObserveViewModel(INotifyPropertyChanged? nextViewModel)
    {
        if (ReferenceEquals(observedViewModel, nextViewModel))
        {
            return;
        }

        if (observedViewModel is not null)
        {
            observedViewModel.PropertyChanged -= OnViewModelPropertyChanged;
        }

        observedViewModel = nextViewModel;
        if (observedViewModel is not null)
        {
            observedViewModel.PropertyChanged += OnViewModelPropertyChanged;
        }
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == StepIndexPropertyName)
        {
            Dispatcher.Invoke(() => ApplyStepVisibility(ResolveStepIndex(), animate: true));
            return;
        }

        if (e.PropertyName == IsPanelOpenPropertyName)
        {
            Dispatcher.Invoke(() => ApplyStepVisibility(ResolveStepIndex(), animate: false));
            return;
        }

        if (e.PropertyName == GameSearchTextPropertyName)
        {
            Dispatcher.Invoke(AnimateGameListRefresh);
        }
    }

    private int ResolveStepIndex()
    {
        object? value = DataContext?.GetType().GetProperty(StepIndexPropertyName)?.GetValue(DataContext);
        int index = value is int stepIndex ? stepIndex : 0;
        return Math.Clamp(index, 0, StepPanels.Length - 1);
    }

    private void ApplyStepVisibility(int nextStepIndex, bool animate)
    {
        FrameworkElement[] panels = StepPanels;
        if (!hasAppliedInitialStep)
        {
            currentStepIndex = nextStepIndex;
            hasAppliedInitialStep = true;
            animate = false;
        }

        if (!animate || !SystemParameters.ClientAreaAnimation || nextStepIndex == currentStepIndex)
        {
            for (int index = 0; index < panels.Length; index++)
            {
                SetPanelInstant(panels[index], index == nextStepIndex);
            }

            currentStepIndex = nextStepIndex;
            return;
        }

        int previousStepIndex = currentStepIndex;
        int direction = nextStepIndex > previousStepIndex ? 1 : -1;
        FrameworkElement outgoing = panels[previousStepIndex];
        FrameworkElement incoming = panels[nextStepIndex];

        for (int index = 0; index < panels.Length; index++)
        {
            if (index != previousStepIndex && index != nextStepIndex)
            {
                SetPanelInstant(panels[index], visible: false);
            }
        }

        currentStepIndex = nextStepIndex;
        AnimateStepTransition(outgoing, incoming, direction);
    }

    private static void SetPanelInstant(FrameworkElement panel, bool visible)
    {
        TranslateTransform transform = EnsureTranslateTransform(panel);
        transform.X = 0;
        panel.BeginAnimation(OpacityProperty, null);
        transform.BeginAnimation(TranslateTransform.XProperty, null);
        panel.Opacity = visible ? 1 : 0;
        panel.Visibility = visible ? Visibility.Visible : Visibility.Collapsed;
    }

    private static void AnimateStepTransition(FrameworkElement outgoing, FrameworkElement incoming, int direction)
    {
        TranslateTransform outgoingTransform = EnsureTranslateTransform(outgoing);
        TranslateTransform incomingTransform = EnsureTranslateTransform(incoming);
        TimeSpan duration = TimeSpan.FromMilliseconds(260);
        IEasingFunction easing = new CubicEase { EasingMode = EasingMode.EaseOut };

        outgoing.Visibility = Visibility.Visible;
        outgoing.Opacity = 1;
        outgoingTransform.X = 0;

        incoming.Visibility = Visibility.Visible;
        incoming.Opacity = 0;
        incomingTransform.X = StepTravel * direction;

        outgoing.BeginAnimation(
            OpacityProperty,
            new DoubleAnimation(1, 0, duration) { EasingFunction = easing });
        outgoingTransform.BeginAnimation(
            TranslateTransform.XProperty,
            new DoubleAnimation(0, -StepTravel * direction, duration) { EasingFunction = easing });

        DoubleAnimation incomingOpacity = new(0, 1, duration) { EasingFunction = easing };
        incomingOpacity.Completed += (_, _) =>
        {
            outgoing.Visibility = Visibility.Collapsed;
            outgoing.Opacity = 0;
            outgoingTransform.X = 0;
            incoming.Opacity = 1;
            incomingTransform.X = 0;
        };
        incoming.BeginAnimation(OpacityProperty, incomingOpacity);
        incomingTransform.BeginAnimation(
            TranslateTransform.XProperty,
            new DoubleAnimation(StepTravel * direction, 0, duration) { EasingFunction = easing });
    }

    private void AnimateGameListRefresh()
    {
        if (!IsLoaded ||
            GameTemplateList.Visibility != Visibility.Visible ||
            !SystemParameters.ClientAreaAnimation)
        {
            return;
        }

        TranslateTransform transform = EnsureTranslateTransform(GameTemplateList);
        TimeSpan duration = TimeSpan.FromMilliseconds(180);
        IEasingFunction easing = new CubicEase { EasingMode = EasingMode.EaseOut };

        GameTemplateList.BeginAnimation(OpacityProperty, null);
        transform.BeginAnimation(TranslateTransform.YProperty, null);
        GameTemplateList.Opacity = 0.68;
        transform.Y = 6;
        GameTemplateList.BeginAnimation(
            OpacityProperty,
            new DoubleAnimation(0.68, 1, duration) { EasingFunction = easing });
        transform.BeginAnimation(
            TranslateTransform.YProperty,
            new DoubleAnimation(6, 0, duration) { EasingFunction = easing });
    }

    private static TranslateTransform EnsureTranslateTransform(FrameworkElement panel)
    {
        if (panel.RenderTransform is TranslateTransform translateTransform)
        {
            return translateTransform;
        }

        translateTransform = new TranslateTransform();
        panel.RenderTransform = translateTransform;
        return translateTransform;
    }
}
