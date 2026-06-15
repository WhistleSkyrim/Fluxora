using System.Collections.Specialized;
using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;
using Fluxora.App.Models;
using WpfButtonBase = System.Windows.Controls.Primitives.ButtonBase;
using WpfDragEventArgs = System.Windows.DragEventArgs;
using WpfMouseEventArgs = System.Windows.Input.MouseEventArgs;
using WpfListBox = System.Windows.Controls.ListBox;
using WpfPoint = System.Windows.Point;
using WpfQueryContinueDragEventArgs = System.Windows.QueryContinueDragEventArgs;
using WpfScrollBar = System.Windows.Controls.Primitives.ScrollBar;
using WpfTextBoxBase = System.Windows.Controls.Primitives.TextBoxBase;
using WpfThumb = System.Windows.Controls.Primitives.Thumb;

namespace Fluxora.App.Services;

public sealed class PluginOrderDragDropService
{
    private const string PluginOrderDataFormat = "Fluxora.PluginOrderItem";
    private static readonly TimeSpan QuickAnimationDuration = TimeSpan.FromMilliseconds(120);
    private static readonly TimeSpan ReorderAnimationDuration = TimeSpan.FromMilliseconds(260);

    private readonly WpfListBox listBox;
    private readonly Func<PluginEntry, bool> canStartDrag;
    private readonly Func<PluginEntry, int, Task> moveAsync;
    private readonly Dictionary<string, double> lastKnownItemPositions = new(StringComparer.OrdinalIgnoreCase);

    private WpfPoint? dragStartPoint;
    private ListBoxItem? draggedItem;
    private DragVisualAdorner? dragVisual;
    private DropIndicatorAdorner? dropIndicator;
    private AdornerLayer? adornerLayer;
    private ScrollViewer? scrollViewer;
    private INotifyCollectionChanged? currentItemsSource;
    private IReadOnlyDictionary<string, double>? pendingReorderAnimationItems;
    private bool isReorderAnimationScheduled;
    private bool isPositionSnapshotScheduled;
    private bool isAnimatingItems;
    private bool isScrollViewerSubscribed;

    public PluginOrderDragDropService(
        WpfListBox listBox,
        Func<PluginEntry, bool> canStartDrag,
        Func<PluginEntry, int, Task> moveAsync)
    {
        this.listBox = listBox;
        this.canStartDrag = canStartDrag;
        this.moveAsync = moveAsync;
    }

    public void Attach()
    {
        listBox.AllowDrop = true;
        listBox.PreviewMouseLeftButtonDown += OnPreviewMouseLeftButtonDown;
        listBox.PreviewMouseLeftButtonUp += OnPreviewMouseLeftButtonUp;
        listBox.MouseMove += OnMouseMove;
        listBox.DragOver += OnDragOver;
        listBox.Drop += OnDrop;
        listBox.DragLeave += OnDragLeave;
        listBox.QueryContinueDrag += OnQueryContinueDrag;
        listBox.Loaded += OnListBoxLoaded;
        listBox.SizeChanged += OnListBoxSizeChanged;

        DependencyPropertyDescriptor
            .FromProperty(ItemsControl.ItemsSourceProperty, typeof(WpfListBox))?
            .AddValueChanged(listBox, OnItemsSourceChanged);
        SubscribeToItemsSource();
    }

    private void OnPreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (IsDragBlockedByInteractiveElement(e.OriginalSource as DependencyObject))
        {
            draggedItem = null;
            dragStartPoint = null;
            return;
        }

        if (FindVisualParent<ListBoxItem>(e.OriginalSource as DependencyObject) is { DataContext: PluginEntry plugin } item &&
            canStartDrag(plugin))
        {
            draggedItem = item;
            dragStartPoint = e.GetPosition(listBox);
            listBox.SelectedItem = plugin;
            return;
        }

        draggedItem = null;
        dragStartPoint = null;
    }

    private void OnPreviewMouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        if (dragVisual is null)
        {
            draggedItem = null;
            dragStartPoint = null;
        }
    }

    private void OnMouseMove(object sender, WpfMouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed ||
            dragStartPoint is null ||
            draggedItem?.DataContext is not PluginEntry plugin ||
            !canStartDrag(plugin))
        {
            return;
        }

        WpfPoint currentPoint = e.GetPosition(listBox);
        if (Math.Abs(currentPoint.X - dragStartPoint.Value.X) < SystemParameters.MinimumHorizontalDragDistance &&
            Math.Abs(currentPoint.Y - dragStartPoint.Value.Y) < SystemParameters.MinimumVerticalDragDistance)
        {
            return;
        }

        StartDrag(plugin, currentPoint);
    }

    private void StartDrag(PluginEntry plugin, WpfPoint currentPoint)
    {
        EnsureAdorners(plugin);
        dragVisual?.Move(currentPoint);
        AnimateDraggedItem(0.48);

        System.Windows.DataObject data = new();
        data.SetData(PluginOrderDataFormat, plugin);
        try
        {
            System.Windows.DragDrop.DoDragDrop(listBox, data, System.Windows.DragDropEffects.Move);
        }
        finally
        {
            CleanupDragState();
        }
    }

    private void OnDragOver(object sender, WpfDragEventArgs e)
    {
        if (!TryGetDraggedPlugin(e, out _))
        {
            e.Effects = System.Windows.DragDropEffects.None;
            HideDropIndicator();
            e.Handled = true;
            return;
        }

        e.Effects = System.Windows.DragDropEffects.Move;
        WpfPoint point = e.GetPosition(listBox);
        dragVisual?.Move(point);
        AutoScroll(point);

        if (TryGetInsertionTarget(e, out _, out double indicatorY))
        {
            dropIndicator?.Update(indicatorY);
        }
        else
        {
            HideDropIndicator();
        }

        e.Handled = true;
    }

    private async void OnDrop(object sender, WpfDragEventArgs e)
    {
        e.Handled = true;
        try
        {
            if (!TryGetDraggedPlugin(e, out PluginEntry? plugin) ||
                plugin is null ||
                !TryGetInsertionTarget(e, out int insertionIndex, out _))
            {
                e.Effects = System.Windows.DragDropEffects.None;
                return;
            }

            e.Effects = System.Windows.DragDropEffects.Move;
            await moveAsync(plugin, insertionIndex);
        }
        finally
        {
            CleanupDragState();
        }
    }

    private void OnListBoxLoaded(object sender, RoutedEventArgs e)
    {
        SubscribeToItemsSource();
        EnsureScrollViewerSubscription();
        QueuePositionSnapshot();
    }

    private void OnListBoxSizeChanged(object sender, SizeChangedEventArgs e)
    {
        QueuePositionSnapshot();
    }

    private void OnItemsSourceChanged(object? sender, EventArgs e)
    {
        SubscribeToItemsSource();
        QueuePositionSnapshot();
    }

    private void SubscribeToItemsSource()
    {
        if (ReferenceEquals(currentItemsSource, listBox.ItemsSource))
        {
            return;
        }

        if (currentItemsSource is not null)
        {
            currentItemsSource.CollectionChanged -= OnItemsCollectionChanged;
        }

        currentItemsSource = listBox.ItemsSource as INotifyCollectionChanged;
        if (currentItemsSource is not null)
        {
            currentItemsSource.CollectionChanged += OnItemsCollectionChanged;
        }
    }

    private void OnItemsCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (lastKnownItemPositions.Count == 0 ||
            e.Action != NotifyCollectionChangedAction.Move)
        {
            QueuePositionSnapshot();
            return;
        }

        pendingReorderAnimationItems ??= new Dictionary<string, double>(
            lastKnownItemPositions,
            StringComparer.OrdinalIgnoreCase);
        ScheduleReorderAnimation();
    }

    private async void ScheduleReorderAnimation()
    {
        if (isReorderAnimationScheduled)
        {
            return;
        }

        isReorderAnimationScheduled = true;
        try
        {
            await listBox.Dispatcher.InvokeAsync(listBox.UpdateLayout, DispatcherPriority.Loaded);
            if (pendingReorderAnimationItems is not null)
            {
                AnimateItemsFrom(pendingReorderAnimationItems);
            }
        }
        finally
        {
            pendingReorderAnimationItems = null;
            isReorderAnimationScheduled = false;
            if (!isAnimatingItems)
            {
                UpdateLastKnownItemPositions();
            }
        }
    }

    private void QueuePositionSnapshot()
    {
        if (isPositionSnapshotScheduled ||
            isReorderAnimationScheduled ||
            isAnimatingItems)
        {
            return;
        }

        isPositionSnapshotScheduled = true;
        _ = listBox.Dispatcher.InvokeAsync(
            () =>
            {
                isPositionSnapshotScheduled = false;
                if (!isReorderAnimationScheduled && !isAnimatingItems)
                {
                    UpdateLastKnownItemPositions();
                }
            },
            DispatcherPriority.Background);
    }

    private void OnDragLeave(object sender, WpfDragEventArgs e)
    {
        WpfPoint point = e.GetPosition(listBox);
        if (point.X < 0 ||
            point.Y < 0 ||
            point.X > listBox.ActualWidth ||
            point.Y > listBox.ActualHeight)
        {
            HideDropIndicator();
        }
    }

    private void OnQueryContinueDrag(object sender, WpfQueryContinueDragEventArgs e)
    {
        if (e.Action != System.Windows.DragAction.Continue)
        {
            CleanupDragState();
        }
    }

    private void EnsureAdorners(PluginEntry plugin)
    {
        adornerLayer ??= AdornerLayer.GetAdornerLayer(listBox);
        if (adornerLayer is null)
        {
            return;
        }

        dragVisual ??= new DragVisualAdorner(listBox, plugin);
        dropIndicator ??= new DropIndicatorAdorner(listBox);
        adornerLayer.Add(dropIndicator);
        adornerLayer.Add(dragVisual);
    }

    private void CleanupDragState()
    {
        dragStartPoint = null;
        AnimateDraggedItem(1);
        draggedItem = null;

        if (adornerLayer is not null)
        {
            if (dragVisual is not null)
            {
                adornerLayer.Remove(dragVisual);
            }

            if (dropIndicator is not null)
            {
                adornerLayer.Remove(dropIndicator);
            }
        }

        dragVisual = null;
        dropIndicator = null;
    }

    private void HideDropIndicator()
    {
        dropIndicator?.Hide();
    }

    private bool TryGetDraggedPlugin(WpfDragEventArgs e, out PluginEntry? plugin)
    {
        plugin = null;
        if (!e.Data.GetDataPresent(PluginOrderDataFormat))
        {
            return false;
        }

        plugin = e.Data.GetData(PluginOrderDataFormat) as PluginEntry;
        return plugin is not null;
    }

    private bool TryGetInsertionTarget(WpfDragEventArgs e, out int insertionIndex, out double indicatorY)
    {
        insertionIndex = listBox.Items.Count;
        indicatorY = Math.Max(0, listBox.ActualHeight - 8);

        if (listBox.Items.Count == 0)
        {
            return false;
        }

        WpfPoint point = e.GetPosition(listBox);
        ListBoxItem? firstRealizedItem = null;
        ListBoxItem? lastRealizedItem = null;
        double firstTop = 0;
        double lastBottom = indicatorY;

        foreach ((int itemIndex, ListBoxItem item) in GetRealizedItems())
        {
            WpfPoint itemTop = item.TranslatePoint(new WpfPoint(0, 0), listBox);
            double top = itemTop.Y;
            double bottom = top + item.ActualHeight;
            firstRealizedItem ??= item;
            if (ReferenceEquals(firstRealizedItem, item))
            {
                firstTop = top;
            }

            lastRealizedItem = item;
            lastBottom = bottom;

            if (point.Y < top)
            {
                insertionIndex = itemIndex;
                indicatorY = top;
                return true;
            }

            if (point.Y > bottom)
            {
                continue;
            }

            bool insertAfter = point.Y >= top + item.ActualHeight / 2;
            insertionIndex = itemIndex + (insertAfter ? 1 : 0);
            indicatorY = insertAfter ? bottom : top;
            return true;
        }

        if (firstRealizedItem is not null && point.Y < firstTop)
        {
            insertionIndex = listBox.ItemContainerGenerator.IndexFromContainer(firstRealizedItem);
            indicatorY = firstTop;
            return true;
        }

        if (lastRealizedItem is not null)
        {
            insertionIndex = listBox.ItemContainerGenerator.IndexFromContainer(lastRealizedItem) + 1;
            indicatorY = lastBottom;
        }

        return true;
    }

    private void AutoScroll(WpfPoint point)
    {
        EnsureScrollViewerSubscription();
        if (scrollViewer is null)
        {
            return;
        }

        const double edgeSize = 34;
        if (point.Y < edgeSize)
        {
            scrollViewer.LineUp();
        }
        else if (point.Y > listBox.ActualHeight - edgeSize)
        {
            scrollViewer.LineDown();
        }
    }

    private IReadOnlyDictionary<string, double> CaptureItemPositions()
    {
        Dictionary<string, double> positions = new(StringComparer.OrdinalIgnoreCase);
        foreach (ListBoxItem item in FindVisualChildren<ListBoxItem>(listBox))
        {
            if (item.DataContext is not PluginEntry plugin ||
                item.Visibility != Visibility.Visible ||
                item.ActualHeight <= 0.5)
            {
                continue;
            }

            positions[PluginListItemKey(plugin)] = item.TranslatePoint(new WpfPoint(0, 0), listBox).Y;
        }

        return positions;
    }

    private void UpdateLastKnownItemPositions()
    {
        lastKnownItemPositions.Clear();
        foreach ((string key, double y) in CaptureItemPositions())
        {
            lastKnownItemPositions[key] = y;
        }
    }

    private void AnimateItemsFrom(IReadOnlyDictionary<string, double> previousItems)
    {
        if (!SystemParameters.ClientAreaAnimation || previousItems.Count == 0)
        {
            return;
        }

        int animatedItems = 0;
        for (int index = 0; index < listBox.Items.Count; ++index)
        {
            if (listBox.ItemContainerGenerator.ContainerFromIndex(index) is not ListBoxItem item ||
                item.DataContext is not PluginEntry plugin ||
                !previousItems.TryGetValue(PluginListItemKey(plugin), out double oldY))
            {
                continue;
            }

            double newY = item.TranslatePoint(new WpfPoint(0, 0), listBox).Y;
            double deltaY = oldY - newY;
            if (Math.Abs(deltaY) < 0.5)
            {
                continue;
            }

            TranslateTransform translate = EnsureTranslateTransform(item);
            translate.BeginAnimation(TranslateTransform.YProperty, null);
            translate.Y = deltaY;
            item.CacheMode = new BitmapCache();

            DoubleAnimation animation = new(0, new Duration(ReorderAnimationDuration))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            };
            animation.Completed += (_, _) =>
            {
                translate.Y = 0;
                item.CacheMode = null;
            };
            translate.BeginAnimation(TranslateTransform.YProperty, animation, HandoffBehavior.SnapshotAndReplace);
            ++animatedItems;
        }

        if (animatedItems > 0)
        {
            CompleteItemAnimationsAfterDelay();
        }
    }

    private async void CompleteItemAnimationsAfterDelay()
    {
        isAnimatingItems = true;
        try
        {
            await Task.Delay(ReorderAnimationDuration + TimeSpan.FromMilliseconds(40));
        }
        finally
        {
            isAnimatingItems = false;
            UpdateLastKnownItemPositions();
        }
    }

    private static TranslateTransform EnsureTranslateTransform(ListBoxItem item)
    {
        if (item.RenderTransform is TranslateTransform { IsFrozen: false } translate)
        {
            return translate;
        }

        translate = new TranslateTransform();
        item.RenderTransform = translate;
        return translate;
    }

    private List<(int Index, ListBoxItem Item)> GetRealizedItems()
    {
        List<(int Index, ListBoxItem Item)> items = new();
        foreach (ListBoxItem item in FindVisualChildren<ListBoxItem>(listBox))
        {
            if (item.DataContext is not PluginEntry ||
                item.Visibility != Visibility.Visible ||
                item.ActualHeight <= 0.5)
            {
                continue;
            }

            int itemIndex = listBox.ItemContainerGenerator.IndexFromContainer(item);
            if (itemIndex >= 0)
            {
                items.Add((itemIndex, item));
            }
        }

        items.Sort(static (left, right) => left.Index.CompareTo(right.Index));
        return items;
    }

    private void EnsureScrollViewerSubscription()
    {
        scrollViewer ??= FindVisualChild<ScrollViewer>(listBox);
        if (scrollViewer is not null && !isScrollViewerSubscribed)
        {
            scrollViewer.ScrollChanged += OnScrollViewerScrollChanged;
            isScrollViewerSubscribed = true;
        }
    }

    private void OnScrollViewerScrollChanged(object sender, ScrollChangedEventArgs e)
    {
        QueuePositionSnapshot();
    }

    private void AnimateDraggedItem(double opacity)
    {
        if (draggedItem is null)
        {
            return;
        }

        Duration duration = SystemParameters.ClientAreaAnimation
            ? new Duration(QuickAnimationDuration)
            : new Duration(TimeSpan.Zero);
        DoubleAnimation animation = new(opacity, duration)
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
        };
        draggedItem.BeginAnimation(UIElement.OpacityProperty, animation);
    }

    private static T? FindVisualParent<T>(DependencyObject? current) where T : DependencyObject
    {
        while (current is not null)
        {
            if (current is T match)
            {
                return match;
            }

            current = VisualTreeHelper.GetParent(current);
        }

        return null;
    }

    private static T? FindVisualChild<T>(DependencyObject current) where T : DependencyObject
    {
        for (int index = 0; index < VisualTreeHelper.GetChildrenCount(current); ++index)
        {
            DependencyObject child = VisualTreeHelper.GetChild(current, index);
            if (child is T match)
            {
                return match;
            }

            T? descendant = FindVisualChild<T>(child);
            if (descendant is not null)
            {
                return descendant;
            }
        }

        return null;
    }

    private static IEnumerable<T> FindVisualChildren<T>(DependencyObject current) where T : DependencyObject
    {
        for (int index = 0; index < VisualTreeHelper.GetChildrenCount(current); ++index)
        {
            DependencyObject child = VisualTreeHelper.GetChild(current, index);
            if (child is T match)
            {
                yield return match;
            }

            foreach (T descendant in FindVisualChildren<T>(child))
            {
                yield return descendant;
            }
        }
    }

    private static bool IsDragBlockedByInteractiveElement(DependencyObject? current)
    {
        while (current is not null)
        {
            if (current is WpfButtonBase or WpfTextBoxBase or WpfScrollBar or WpfThumb)
            {
                return true;
            }

            if (current is ListBoxItem)
            {
                return false;
            }

            current = VisualTreeHelper.GetParent(current);
        }

        return false;
    }

    private static string PluginListItemKey(PluginEntry plugin)
    {
        return string.IsNullOrWhiteSpace(plugin.OrderId) ? plugin.Id : plugin.OrderId;
    }
}
