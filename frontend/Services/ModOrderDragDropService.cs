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
using WpfPoint = System.Windows.Point;
using WpfQueryContinueDragEventArgs = System.Windows.QueryContinueDragEventArgs;
using WpfScrollBar = System.Windows.Controls.Primitives.ScrollBar;
using WpfTextBoxBase = System.Windows.Controls.Primitives.TextBoxBase;
using WpfThumb = System.Windows.Controls.Primitives.Thumb;

namespace Fluxora.App.Services;

public sealed class ModOrderDragDropService
{
    private const string ModOrderDataFormat = "Fluxora.ModOrderItem";
    private static readonly TimeSpan QuickAnimationDuration = TimeSpan.FromMilliseconds(120);
    private static readonly TimeSpan ReorderAnimationDuration = TimeSpan.FromMilliseconds(260);

    private readonly DataGrid dataGrid;
    private readonly Func<ModEntry, bool> canStartDrag;
    private readonly Func<ModEntry, int, Task> moveAsync;
    private readonly Dictionary<string, double> lastKnownRowPositions = new(StringComparer.OrdinalIgnoreCase);

    private WpfPoint? dragStartPoint;
    private DataGridRow? draggedRow;
    private DragVisualAdorner? dragVisual;
    private DropIndicatorAdorner? dropIndicator;
    private AdornerLayer? adornerLayer;
    private ScrollViewer? scrollViewer;
    private INotifyCollectionChanged? currentItemsSource;
    private IReadOnlyDictionary<string, double>? pendingReorderAnimationRows;
    private bool isReorderAnimationScheduled;
    private bool isPositionSnapshotScheduled;
    private bool isAnimatingRows;
    private bool isScrollViewerSubscribed;

    public ModOrderDragDropService(
        DataGrid dataGrid,
        Func<ModEntry, bool> canStartDrag,
        Func<ModEntry, int, Task> moveAsync)
    {
        this.dataGrid = dataGrid;
        this.canStartDrag = canStartDrag;
        this.moveAsync = moveAsync;
    }

    public void Attach()
    {
        dataGrid.AllowDrop = true;
        dataGrid.AddHandler(
            Mouse.PreviewMouseDownEvent,
            new MouseButtonEventHandler(OnPreviewMouseLeftButtonDown),
            true);
        dataGrid.PreviewMouseLeftButtonUp += OnPreviewMouseLeftButtonUp;
        dataGrid.MouseMove += OnMouseMove;
        dataGrid.DragOver += OnDragOver;
        dataGrid.Drop += OnDrop;
        dataGrid.DragLeave += OnDragLeave;
        dataGrid.QueryContinueDrag += OnQueryContinueDrag;
        dataGrid.Loaded += OnDataGridLoaded;
        dataGrid.SizeChanged += OnDataGridSizeChanged;

        DependencyPropertyDescriptor
            .FromProperty(ItemsControl.ItemsSourceProperty, typeof(DataGrid))?
            .AddValueChanged(dataGrid, OnItemsSourceChanged);
        SubscribeToItemsSource();
    }

    private void OnPreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton != MouseButton.Left)
        {
            return;
        }

        if (SelectionInputService.HasRangeSelectionModifier(Keyboard.Modifiers) ||
            IsDragBlockedByInteractiveElement(e.OriginalSource as DependencyObject))
        {
            draggedRow = null;
            dragStartPoint = null;
            return;
        }

        if (FindVisualParent<DataGridRow>(e.OriginalSource as DependencyObject) is { Item: ModEntry mod } row)
        {
            if (canStartDrag(mod))
            {
                draggedRow = row;
                dragStartPoint = e.GetPosition(dataGrid);
            }

            return;
        }

        draggedRow = null;
        dragStartPoint = null;
    }

    private void OnPreviewMouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        if (dragVisual is null)
        {
            draggedRow = null;
            dragStartPoint = null;
        }
    }

    private void OnMouseMove(object sender, WpfMouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed ||
            dragStartPoint is null ||
            draggedRow?.Item is not ModEntry mod ||
            !canStartDrag(mod))
        {
            return;
        }

        WpfPoint currentPoint = e.GetPosition(dataGrid);
        if (Math.Abs(currentPoint.X - dragStartPoint.Value.X) < SystemParameters.MinimumHorizontalDragDistance &&
            Math.Abs(currentPoint.Y - dragStartPoint.Value.Y) < SystemParameters.MinimumVerticalDragDistance)
        {
            return;
        }

        StartDrag(mod, currentPoint);
    }

    private void StartDrag(ModEntry mod, WpfPoint currentPoint)
    {
        EnsureAdorners(mod);
        dragVisual?.Move(currentPoint);
        AnimateDraggedRow(0.48);

        System.Windows.DataObject data = new();
        data.SetData(ModOrderDataFormat, mod);
        try
        {
            System.Windows.DragDrop.DoDragDrop(dataGrid, data, System.Windows.DragDropEffects.Move);
        }
        finally
        {
            CleanupDragState();
        }
    }

    private void OnDragOver(object sender, WpfDragEventArgs e)
    {
        if (!TryGetDraggedMod(e, out _))
        {
            e.Effects = System.Windows.DragDropEffects.None;
            HideDropIndicator();
            e.Handled = true;
            return;
        }

        e.Effects = System.Windows.DragDropEffects.Move;
        WpfPoint point = e.GetPosition(dataGrid);
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
            if (!TryGetDraggedMod(e, out ModEntry? mod) ||
                mod is null ||
                !TryGetInsertionTarget(e, out int insertionIndex, out _))
            {
                e.Effects = System.Windows.DragDropEffects.None;
                return;
            }

            e.Effects = System.Windows.DragDropEffects.Move;
            await moveAsync(mod, insertionIndex);
        }
        finally
        {
            CleanupDragState();
        }
    }

    private void OnDataGridLoaded(object sender, RoutedEventArgs e)
    {
        SubscribeToItemsSource();
        EnsureScrollViewerSubscription();
        QueuePositionSnapshot();
    }

    private void OnDataGridSizeChanged(object sender, SizeChangedEventArgs e)
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
        if (ReferenceEquals(currentItemsSource, dataGrid.ItemsSource))
        {
            return;
        }

        if (currentItemsSource is not null)
        {
            currentItemsSource.CollectionChanged -= OnItemsCollectionChanged;
        }

        currentItemsSource = dataGrid.ItemsSource as INotifyCollectionChanged;
        if (currentItemsSource is not null)
        {
            currentItemsSource.CollectionChanged += OnItemsCollectionChanged;
        }
    }

    private void OnItemsCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (lastKnownRowPositions.Count == 0 ||
            e.Action != NotifyCollectionChangedAction.Move)
        {
            QueuePositionSnapshot();
            return;
        }

        pendingReorderAnimationRows ??= new Dictionary<string, double>(
            lastKnownRowPositions,
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
            await dataGrid.Dispatcher.InvokeAsync(dataGrid.UpdateLayout, DispatcherPriority.Loaded);
            if (pendingReorderAnimationRows is not null)
            {
                AnimateRowsFrom(pendingReorderAnimationRows);
            }
        }
        finally
        {
            pendingReorderAnimationRows = null;
            isReorderAnimationScheduled = false;
            if (!isAnimatingRows)
            {
                UpdateLastKnownRowPositions();
            }
        }
    }

    private void QueuePositionSnapshot()
    {
        if (isPositionSnapshotScheduled ||
            isReorderAnimationScheduled ||
            isAnimatingRows)
        {
            return;
        }

        isPositionSnapshotScheduled = true;
        _ = dataGrid.Dispatcher.InvokeAsync(
            () =>
            {
                isPositionSnapshotScheduled = false;
                if (!isReorderAnimationScheduled && !isAnimatingRows)
                {
                    UpdateLastKnownRowPositions();
                }
            },
            DispatcherPriority.Background);
    }

    private void OnDragLeave(object sender, WpfDragEventArgs e)
    {
        WpfPoint point = e.GetPosition(dataGrid);
        if (point.X < 0 ||
            point.Y < 0 ||
            point.X > dataGrid.ActualWidth ||
            point.Y > dataGrid.ActualHeight)
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

    private void EnsureAdorners(ModEntry mod)
    {
        adornerLayer ??= AdornerLayer.GetAdornerLayer(dataGrid);
        if (adornerLayer is null)
        {
            return;
        }

        dragVisual ??= new DragVisualAdorner(dataGrid, mod);
        dropIndicator ??= new DropIndicatorAdorner(dataGrid);
        adornerLayer.Add(dropIndicator);
        adornerLayer.Add(dragVisual);
    }

    private void CleanupDragState()
    {
        dragStartPoint = null;
        AnimateDraggedRow(1);
        draggedRow = null;

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

    private bool TryGetDraggedMod(WpfDragEventArgs e, out ModEntry? mod)
    {
        mod = null;
        if (!e.Data.GetDataPresent(ModOrderDataFormat))
        {
            return false;
        }

        mod = e.Data.GetData(ModOrderDataFormat) as ModEntry;
        return mod is not null;
    }

    private bool TryGetInsertionTarget(WpfDragEventArgs e, out int insertionIndex, out double indicatorY)
    {
        insertionIndex = dataGrid.Items.Count;
        indicatorY = Math.Max(0, dataGrid.ActualHeight - 8);

        if (dataGrid.Items.Count == 0)
        {
            return false;
        }

        WpfPoint point = e.GetPosition(dataGrid);
        DataGridRow? firstRealizedRow = null;
        DataGridRow? lastRealizedRow = null;
        double firstTop = 0;
        double lastBottom = indicatorY;

        foreach ((int rowIndex, DataGridRow row) in GetRealizedRows())
        {
            WpfPoint rowTop = row.TranslatePoint(new WpfPoint(0, 0), dataGrid);
            double top = rowTop.Y;
            double bottom = top + row.ActualHeight;
            firstRealizedRow ??= row;
            if (ReferenceEquals(firstRealizedRow, row))
            {
                firstTop = top;
            }

            lastRealizedRow = row;
            lastBottom = bottom;

            if (point.Y < top)
            {
                insertionIndex = rowIndex;
                indicatorY = top;
                return true;
            }

            if (point.Y > bottom)
            {
                continue;
            }

            bool insertAfter = point.Y >= top + row.ActualHeight / 2;
            insertionIndex = rowIndex + (insertAfter ? 1 : 0);
            indicatorY = insertAfter ? bottom : top;
            return true;
        }

        if (firstRealizedRow is not null && point.Y < firstTop)
        {
            insertionIndex = dataGrid.ItemContainerGenerator.IndexFromContainer(firstRealizedRow);
            indicatorY = firstTop;
            return true;
        }

        if (lastRealizedRow is not null)
        {
            insertionIndex = dataGrid.ItemContainerGenerator.IndexFromContainer(lastRealizedRow) + 1;
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
        else if (point.Y > dataGrid.ActualHeight - edgeSize)
        {
            scrollViewer.LineDown();
        }
    }

    private IReadOnlyDictionary<string, double> CaptureRowPositions()
    {
        Dictionary<string, double> positions = new(StringComparer.OrdinalIgnoreCase);
        foreach (DataGridRow row in FindVisualChildren<DataGridRow>(dataGrid))
        {
            if (row.Item is not ModEntry mod ||
                row.Visibility != Visibility.Visible ||
                row.ActualHeight <= 0.5)
            {
                continue;
            }

            positions[ModListItemKey(mod)] = row.TranslatePoint(new WpfPoint(0, 0), dataGrid).Y;
        }

        return positions;
    }

    private void UpdateLastKnownRowPositions()
    {
        lastKnownRowPositions.Clear();
        foreach ((string key, double y) in CaptureRowPositions())
        {
            lastKnownRowPositions[key] = y;
        }
    }

    private void AnimateRowsFrom(IReadOnlyDictionary<string, double> previousRows)
    {
        if (!SystemParameters.ClientAreaAnimation || previousRows.Count == 0)
        {
            return;
        }

        int animatedRows = 0;
        for (int index = 0; index < dataGrid.Items.Count; ++index)
        {
            if (dataGrid.ItemContainerGenerator.ContainerFromIndex(index) is not DataGridRow row ||
                row.Item is not ModEntry mod ||
                !previousRows.TryGetValue(ModListItemKey(mod), out double oldY))
            {
                continue;
            }

            double newY = row.TranslatePoint(new WpfPoint(0, 0), dataGrid).Y;
            double deltaY = oldY - newY;
            if (Math.Abs(deltaY) < 0.5)
            {
                continue;
            }

            TranslateTransform translate = EnsureTranslateTransform(row);
            translate.BeginAnimation(TranslateTransform.YProperty, null);
            translate.Y = deltaY;

            DoubleAnimation animation = new(0, new Duration(ReorderAnimationDuration))
            {
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
            };
            animation.Completed += (_, _) =>
            {
                translate.Y = 0;
            };
            translate.BeginAnimation(TranslateTransform.YProperty, animation, HandoffBehavior.SnapshotAndReplace);
            ++animatedRows;
        }

        if (animatedRows > 0)
        {
            CompleteRowAnimationsAfterDelay();
        }
    }

    private async void CompleteRowAnimationsAfterDelay()
    {
        isAnimatingRows = true;
        try
        {
            await Task.Delay(ReorderAnimationDuration + TimeSpan.FromMilliseconds(40));
        }
        finally
        {
            isAnimatingRows = false;
            UpdateLastKnownRowPositions();
        }
    }

    private static TranslateTransform EnsureTranslateTransform(DataGridRow row)
    {
        if (row.RenderTransform is TranslateTransform { IsFrozen: false } translate)
        {
            return translate;
        }

        translate = new TranslateTransform();
        row.RenderTransform = translate;
        return translate;
    }

    private List<(int Index, DataGridRow Row)> GetRealizedRows()
    {
        List<(int Index, DataGridRow Row)> rows = new();
        foreach (DataGridRow row in FindVisualChildren<DataGridRow>(dataGrid))
        {
            if (row.Item is not ModEntry ||
                row.Visibility != Visibility.Visible ||
                row.ActualHeight <= 0.5)
            {
                continue;
            }

            int rowIndex = dataGrid.ItemContainerGenerator.IndexFromContainer(row);
            if (rowIndex >= 0)
            {
                rows.Add((rowIndex, row));
            }
        }

        rows.Sort(static (left, right) => left.Index.CompareTo(right.Index));
        return rows;
    }

    private void EnsureScrollViewerSubscription()
    {
        scrollViewer ??= FindVisualChild<ScrollViewer>(dataGrid);
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

    private void AnimateDraggedRow(double opacity)
    {
        if (draggedRow is null)
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
        draggedRow.BeginAnimation(UIElement.OpacityProperty, animation);
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

            if (current is DataGridRow)
            {
                return false;
            }

            current = VisualTreeHelper.GetParent(current);
        }

        return false;
    }

    private static string ModListItemKey(ModEntry mod)
    {
        return string.IsNullOrWhiteSpace(mod.OrderId) ? mod.Id : mod.OrderId;
    }

}
