using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Media;

namespace Fluxora.App.Services;

public static class LocalizationScope
{
    private static readonly DependencyProperty OriginalValuesProperty =
        DependencyProperty.RegisterAttached(
            "OriginalValues",
            typeof(Dictionary<DependencyProperty, string>),
            typeof(LocalizationScope),
            new PropertyMetadata(null));

    private static bool isInitialized;

    public static void Initialize()
    {
        if (isInitialized)
        {
            return;
        }

        isInitialized = true;
        EventManager.RegisterClassHandler(
            typeof(Window),
            FrameworkElement.LoadedEvent,
            new RoutedEventHandler(OnElementLoaded));
        EventManager.RegisterClassHandler(
            typeof(System.Windows.Controls.UserControl),
            FrameworkElement.LoadedEvent,
            new RoutedEventHandler(OnElementLoaded));
        EventManager.RegisterClassHandler(
            typeof(System.Windows.Controls.ContextMenu),
            FrameworkElement.LoadedEvent,
            new RoutedEventHandler(OnElementLoaded));

        LocalizationManager.Current.LanguageChanged += (_, _) => ApplyToOpenWindows();
    }

    public static void Apply(DependencyObject root)
    {
        HashSet<DependencyObject> visited = new(ReferenceEqualityComparer.Instance);
        ApplyRecursive(root, visited);
    }

    private static void OnElementLoaded(object sender, RoutedEventArgs e)
    {
        if (sender is DependencyObject root)
        {
            Apply(root);
        }
    }

    private static void ApplyToOpenWindows()
    {
        System.Windows.Application.Current?.Dispatcher.BeginInvoke(() =>
        {
            foreach (Window window in System.Windows.Application.Current.Windows)
            {
                Apply(window);
            }
        });
    }

    private static void ApplyRecursive(DependencyObject target, HashSet<DependencyObject> visited)
    {
        if (!visited.Add(target))
        {
            return;
        }

        LocalizeTarget(target);

        foreach (object child in LogicalTreeHelper.GetChildren(target))
        {
            if (child is DependencyObject dependencyChild)
            {
                ApplyRecursive(dependencyChild, visited);
            }
        }

        int visualChildren = GetVisualChildrenCount(target);
        for (int index = 0; index < visualChildren; index++)
        {
            ApplyRecursive(VisualTreeHelper.GetChild(target, index), visited);
        }
    }

    private static void LocalizeTarget(DependencyObject target)
    {
        if (target is Window window)
        {
            LocalizeStringProperty(window, Window.TitleProperty);
        }

        if (target is TextBlock textBlock)
        {
            LocalizeStringProperty(textBlock, TextBlock.TextProperty);
        }

        if (target is Run run)
        {
            LocalizeStringProperty(run, Run.TextProperty);
        }

        if (target is ContentControl contentControl)
        {
            LocalizeStringProperty(contentControl, ContentControl.ContentProperty);
        }

        if (target is HeaderedContentControl headeredContentControl)
        {
            LocalizeStringProperty(headeredContentControl, HeaderedContentControl.HeaderProperty);
        }

        if (target is HeaderedItemsControl headeredItemsControl)
        {
            LocalizeStringProperty(headeredItemsControl, HeaderedItemsControl.HeaderProperty);
        }

        if (target is FrameworkElement frameworkElement)
        {
            LocalizeStringProperty(frameworkElement, FrameworkElement.ToolTipProperty);
            LocalizeStringProperty(frameworkElement, AutomationProperties.NameProperty);
        }

        if (target is DataGrid dataGrid)
        {
            foreach (DataGridColumn column in dataGrid.Columns)
            {
                LocalizeStringProperty(column, DataGridColumn.HeaderProperty);
            }
        }
    }

    private static void LocalizeStringProperty(DependencyObject target, DependencyProperty property)
    {
        if (BindingOperations.GetBindingExpressionBase(target, property) is not null)
        {
            return;
        }

        if (target.GetValue(property) is not string currentValue)
        {
            return;
        }

        Dictionary<DependencyProperty, string> originals = GetOriginalValues(target);
        if (!originals.TryGetValue(property, out string? originalValue))
        {
            originalValue = currentValue;
            originals[property] = originalValue;
        }

        string localizedValue = LocalizationManager.Current.Text(originalValue);
        if (!string.Equals(currentValue, localizedValue, StringComparison.Ordinal))
        {
            target.SetValue(property, localizedValue);
        }
    }

    private static Dictionary<DependencyProperty, string> GetOriginalValues(DependencyObject target)
    {
        if (target.GetValue(OriginalValuesProperty) is Dictionary<DependencyProperty, string> values)
        {
            return values;
        }

        values = new Dictionary<DependencyProperty, string>();
        target.SetValue(OriginalValuesProperty, values);
        return values;
    }

    private static int GetVisualChildrenCount(DependencyObject target)
    {
        try
        {
            return VisualTreeHelper.GetChildrenCount(target);
        }
        catch (InvalidOperationException)
        {
            return 0;
        }
        catch (ArgumentException)
        {
            return 0;
        }
    }
}
