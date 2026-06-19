using System.ComponentModel;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Effects;
using System.Windows.Threading;
using Fluxora.App.Models;
using Fluxora.App.Services;
using Fluxora.App.ViewModels;

namespace Fluxora.App;

public partial class FomodInstallerWindow : Window
{
    private readonly WindowChromeService windowChromeService;
    private readonly FomodInstallerViewModel viewModel;

    public FomodInstallerWindow(FomodInstallerInfo installer)
    {
        InitializeComponent();
        viewModel = new FomodInstallerViewModel(installer);
        DataContext = viewModel;
        viewModel.PropertyChanged += OnViewModelPropertyChanged;
        Loaded += OnLoaded;
        windowChromeService = new WindowChromeService(this);
        windowChromeService.Attach();
        Title = viewModel.ModuleTitle;
    }

    public IReadOnlyList<string> SelectedOptionIds => viewModel.SelectedOptionIds;

    private void OnPrimaryClick(object sender, RoutedEventArgs e)
    {
        if (!viewModel.IsLastStep)
        {
            if (!viewModel.MoveNext())
            {
                ScrollValidationTargetIntoView();
            }
            return;
        }

        if (!viewModel.TryFinish())
        {
            ScrollValidationTargetIntoView();
            return;
        }

        DialogResult = true;
        Close();
    }

    private void OnCancelClick(object sender, RoutedEventArgs e)
    {
        DialogResult = false;
        Close();
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        ScrollValidationTargetIntoView();
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(FomodInstallerViewModel.ValidationTargetGroup) or nameof(FomodInstallerViewModel.CurrentStep))
        {
            Dispatcher.BeginInvoke(ScrollValidationTargetIntoView, DispatcherPriority.Loaded);
        }
    }

    private void OnOptionPreview(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { DataContext: FomodOptionViewModel option })
        {
            viewModel.ShowOptionDetails(option);
        }
    }

    private void OnPreviewImageClick(object sender, RoutedEventArgs e)
    {
        if (sender is not FrameworkElement { DataContext: FomodOptionViewModel { HasPreviewImage: true } option })
        {
            return;
        }

        LightboxImage.DataContext = option;
        DialogSurface.Effect = new BlurEffect { Radius = 8 };
        ImageLightboxOverlay.Visibility = Visibility.Visible;
        LightboxCloseButton.Focus();
    }

    private void OnLightboxCloseClick(object sender, RoutedEventArgs e)
    {
        CloseImageLightbox();
    }

    private void OnLightboxBackgroundMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (ReferenceEquals(e.OriginalSource, ImageLightboxOverlay))
        {
            CloseImageLightbox();
        }
    }

    private void OnWindowDragMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton != MouseButton.Left)
        {
            return;
        }

        try
        {
            DragMove();
        }
        catch (InvalidOperationException)
        {
        }
    }

    protected override void OnClosed(EventArgs e)
    {
        Loaded -= OnLoaded;
        viewModel.PropertyChanged -= OnViewModelPropertyChanged;
        base.OnClosed(e);
    }

    private void ScrollValidationTargetIntoView()
    {
        FomodGroupViewModel? targetGroup = viewModel.ValidationTargetGroup;
        if (targetGroup is null)
        {
            return;
        }

        GroupItemsControl.UpdateLayout();
        FrameworkElement? targetElement = FindElementForDataContext(GroupItemsControl, targetGroup);
        if (targetElement is null)
        {
            return;
        }

        targetElement.BringIntoView(new Rect(0, 0, targetElement.ActualWidth, targetElement.ActualHeight));
    }

    private void CloseImageLightbox()
    {
        ImageLightboxOverlay.Visibility = Visibility.Collapsed;
        LightboxImage.DataContext = null;
        DialogSurface.Effect = null;
    }

    private static FrameworkElement? FindElementForDataContext(DependencyObject root, object dataContext)
    {
        int childCount = VisualTreeHelper.GetChildrenCount(root);
        for (int index = 0; index < childCount; index++)
        {
            DependencyObject child = VisualTreeHelper.GetChild(root, index);
            if (child is FrameworkElement element && ReferenceEquals(element.DataContext, dataContext))
            {
                return element;
            }

            FrameworkElement? descendant = FindElementForDataContext(child, dataContext);
            if (descendant is not null)
            {
                return descendant;
            }
        }

        return null;
    }
}
