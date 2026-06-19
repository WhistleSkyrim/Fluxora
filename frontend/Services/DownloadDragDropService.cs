using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using Fluxora.App.Models;
using WpfButtonBase = System.Windows.Controls.Primitives.ButtonBase;
using WpfDataFormats = System.Windows.DataFormats;
using WpfDataObject = System.Windows.DataObject;
using WpfDragAction = System.Windows.DragAction;
using WpfDragDrop = System.Windows.DragDrop;
using WpfDragDropEffects = System.Windows.DragDropEffects;
using WpfDragEventArgs = System.Windows.DragEventArgs;
using WpfDragEventHandler = System.Windows.DragEventHandler;
using WpfGiveFeedbackEventArgs = System.Windows.GiveFeedbackEventArgs;
using WpfIDataObject = System.Windows.IDataObject;
using WpfMouseEventArgs = System.Windows.Input.MouseEventArgs;
using WpfPoint = System.Windows.Point;
using WpfQueryContinueDragEventArgs = System.Windows.QueryContinueDragEventArgs;
using WpfScrollBar = System.Windows.Controls.Primitives.ScrollBar;
using WpfTextBoxBase = System.Windows.Controls.Primitives.TextBoxBase;
using WpfThumb = System.Windows.Controls.Primitives.Thumb;

namespace Fluxora.App.Services;

public sealed class DownloadDragDropService
{
    private const string DownloadDataFormat = "Fluxora.DownloadItem";
    private static readonly TimeSpan QuickAnimationDuration = TimeSpan.FromMilliseconds(140);

    private readonly UIElement dragAdornerSurface;
    private readonly FrameworkElement downloadsDropSurface;
    private readonly FrameworkElement downloadsDropOverlay;
    private readonly DataGrid downloadsGrid;
    private readonly FrameworkElement modsDropSurface;
    private readonly DataGrid modsGrid;
    private readonly Func<bool> canImportFiles;
    private readonly Func<IReadOnlyList<string>, Task> importFilesAsync;
    private readonly Func<DownloadEntry, bool> canStartDownloadInstall;
    private readonly Func<DownloadEntry, int, Task> installDownloadAtInsertionIndexAsync;

    private WpfPoint? downloadDragStartPoint;
    private DataGridRow? draggedDownloadRow;
    private DragVisualAdorner? dragVisual;
    private DropIndicatorAdorner? installDropIndicator;
    private AdornerLayer? dragAdornerLayer;
    private AdornerLayer? installAdornerLayer;
    private ScrollViewer? modsScrollViewer;
    private bool isDownloadsDropOverlayVisible;

    public DownloadDragDropService(
        UIElement dragAdornerSurface,
        FrameworkElement downloadsDropSurface,
        FrameworkElement downloadsDropOverlay,
        DataGrid downloadsGrid,
        FrameworkElement modsDropSurface,
        DataGrid modsGrid,
        Func<bool> canImportFiles,
        Func<IReadOnlyList<string>, Task> importFilesAsync,
        Func<DownloadEntry, bool> canStartDownloadInstall,
        Func<DownloadEntry, int, Task> installDownloadAtInsertionIndexAsync)
    {
        this.dragAdornerSurface = dragAdornerSurface;
        this.downloadsDropSurface = downloadsDropSurface;
        this.downloadsDropOverlay = downloadsDropOverlay;
        this.downloadsGrid = downloadsGrid;
        this.modsDropSurface = modsDropSurface;
        this.modsGrid = modsGrid;
        this.canImportFiles = canImportFiles;
        this.importFilesAsync = importFilesAsync;
        this.canStartDownloadInstall = canStartDownloadInstall;
        this.installDownloadAtInsertionIndexAsync = installDownloadAtInsertionIndexAsync;
    }

    public void Attach()
    {
        downloadsDropSurface.AllowDrop = true;
        downloadsDropSurface.DragEnter += OnDownloadsFileDragEnterOrOver;
        downloadsDropSurface.DragOver += OnDownloadsFileDragEnterOrOver;
        downloadsDropSurface.DragLeave += OnDownloadsFileDragLeave;
        downloadsDropSurface.Drop += OnDownloadsFileDrop;

        downloadsGrid.AddHandler(
            Mouse.PreviewMouseDownEvent,
            new MouseButtonEventHandler(OnDownloadsPreviewMouseLeftButtonDown),
            true);
        downloadsGrid.PreviewMouseLeftButtonUp += OnDownloadsPreviewMouseLeftButtonUp;
        downloadsGrid.MouseMove += OnDownloadsMouseMove;
        downloadsGrid.GiveFeedback += OnDownloadsGiveFeedback;
        downloadsGrid.QueryContinueDrag += OnDownloadsQueryContinueDrag;

        modsDropSurface.AllowDrop = true;
        modsDropSurface.AddHandler(UIElement.DragOverEvent, new WpfDragEventHandler(OnModsDragOver), true);
        modsDropSurface.AddHandler(UIElement.DropEvent, new WpfDragEventHandler(OnModsDrop), true);
        modsDropSurface.AddHandler(UIElement.DragLeaveEvent, new WpfDragEventHandler(OnModsDragLeave), true);
    }

    private void OnDownloadsFileDragEnterOrOver(object sender, WpfDragEventArgs e)
    {
        if (!e.Data.GetDataPresent(WpfDataFormats.FileDrop))
        {
            return;
        }

        if (canImportFiles() &&
            TryGetSupportedDropFiles(e.Data, out IReadOnlyList<string> files) &&
            files.Count > 0)
        {
            e.Effects = WpfDragDropEffects.Copy;
            ShowDownloadsDropOverlay();
        }
        else
        {
            e.Effects = WpfDragDropEffects.None;
            HideDownloadsDropOverlay();
        }

        e.Handled = true;
    }

    private void OnDownloadsFileDragLeave(object sender, WpfDragEventArgs e)
    {
        if (!e.Data.GetDataPresent(WpfDataFormats.FileDrop))
        {
            return;
        }

        WpfPoint point = e.GetPosition(downloadsDropSurface);
        if (point.X < 0 ||
            point.Y < 0 ||
            point.X > downloadsDropSurface.ActualWidth ||
            point.Y > downloadsDropSurface.ActualHeight)
        {
            HideDownloadsDropOverlay();
        }
    }

    private async void OnDownloadsFileDrop(object sender, WpfDragEventArgs e)
    {
        if (!e.Data.GetDataPresent(WpfDataFormats.FileDrop))
        {
            return;
        }

        e.Handled = true;
        HideDownloadsDropOverlay();

        if (!canImportFiles() ||
            !TryGetSupportedDropFiles(e.Data, out IReadOnlyList<string> files) ||
            files.Count == 0)
        {
            e.Effects = WpfDragDropEffects.None;
            return;
        }

        e.Effects = WpfDragDropEffects.Copy;
        await importFilesAsync(files);
    }

    private void OnDownloadsPreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton != MouseButton.Left)
        {
            return;
        }

        if (SelectionInputService.HasRangeSelectionModifier(Keyboard.Modifiers) ||
            IsDragBlockedByInteractiveElement(e.OriginalSource as DependencyObject))
        {
            draggedDownloadRow = null;
            downloadDragStartPoint = null;
            return;
        }

        if (FindVisualParent<DataGridRow>(e.OriginalSource as DependencyObject) is { Item: DownloadEntry download } row)
        {
            if (canStartDownloadInstall(download))
            {
                draggedDownloadRow = row;
                downloadDragStartPoint = e.GetPosition(downloadsGrid);
            }

            return;
        }

        draggedDownloadRow = null;
        downloadDragStartPoint = null;
    }

    private void OnDownloadsPreviewMouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        if (dragVisual is null)
        {
            draggedDownloadRow = null;
            downloadDragStartPoint = null;
        }
    }

    private void OnDownloadsMouseMove(object sender, WpfMouseEventArgs e)
    {
        if (e.LeftButton != MouseButtonState.Pressed ||
            downloadDragStartPoint is null ||
            draggedDownloadRow?.Item is not DownloadEntry download ||
            !canStartDownloadInstall(download))
        {
            return;
        }

        WpfPoint currentPoint = e.GetPosition(downloadsGrid);
        if (Math.Abs(currentPoint.X - downloadDragStartPoint.Value.X) < SystemParameters.MinimumHorizontalDragDistance &&
            Math.Abs(currentPoint.Y - downloadDragStartPoint.Value.Y) < SystemParameters.MinimumVerticalDragDistance)
        {
            return;
        }

        StartDownloadDrag(download);
    }

    private void StartDownloadDrag(DownloadEntry download)
    {
        EnsureDragVisual(download);
        MoveDragVisualToCursor();
        AnimateDraggedDownloadRow(0.48);

        WpfDataObject data = new();
        data.SetData(DownloadDataFormat, download);
        try
        {
            WpfDragDrop.DoDragDrop(downloadsGrid, data, WpfDragDropEffects.Move);
        }
        finally
        {
            CleanupDownloadDragState();
            CleanupInstallDropIndicator();
        }
    }

    private void OnDownloadsQueryContinueDrag(object sender, WpfQueryContinueDragEventArgs e)
    {
        if (e.Action == WpfDragAction.Continue)
        {
            MoveDragVisualToCursor();
            return;
        }

        CleanupDownloadDragState();
        CleanupInstallDropIndicator();
    }

    private void OnDownloadsGiveFeedback(object sender, WpfGiveFeedbackEventArgs e)
    {
        if (dragVisual is null)
        {
            return;
        }

        MoveDragVisualToCursor();
    }

    private void OnModsDragOver(object sender, WpfDragEventArgs e)
    {
        if (!TryGetDraggedDownload(e, out DownloadEntry? download))
        {
            return;
        }

        if (download is null || !canStartDownloadInstall(download))
        {
            e.Effects = WpfDragDropEffects.None;
            HideInstallDropIndicator();
            e.Handled = true;
            return;
        }

        e.Effects = WpfDragDropEffects.Move;
        MoveDragVisual(e.GetPosition(dragAdornerSurface));
        AutoScrollMods(e.GetPosition(modsGrid));

        if (TryGetModInsertionTarget(e, out _, out double indicatorY))
        {
            EnsureInstallDropIndicator();
            installDropIndicator?.Update(indicatorY);
        }
        else
        {
            HideInstallDropIndicator();
        }

        e.Handled = true;
    }

    private async void OnModsDrop(object sender, WpfDragEventArgs e)
    {
        if (!TryGetDraggedDownload(e, out DownloadEntry? download))
        {
            return;
        }

        e.Handled = true;
        try
        {
            if (download is null ||
                !canStartDownloadInstall(download) ||
                !TryGetModInsertionTarget(e, out int insertionIndex, out _))
            {
                e.Effects = WpfDragDropEffects.None;
                return;
            }

            e.Effects = WpfDragDropEffects.Move;
            await installDownloadAtInsertionIndexAsync(download, insertionIndex);
        }
        finally
        {
            CleanupInstallDropIndicator();
        }
    }

    private void OnModsDragLeave(object sender, WpfDragEventArgs e)
    {
        if (!TryGetDraggedDownload(e, out _))
        {
            return;
        }

        WpfPoint point = e.GetPosition(modsDropSurface);
        if (point.X < 0 ||
            point.Y < 0 ||
            point.X > modsDropSurface.ActualWidth ||
            point.Y > modsDropSurface.ActualHeight)
        {
            HideInstallDropIndicator();
        }
    }

    private void EnsureDragVisual(DownloadEntry download)
    {
        dragAdornerLayer ??= AdornerLayer.GetAdornerLayer(dragAdornerSurface);
        if (dragAdornerLayer is null)
        {
            return;
        }

        dragVisual ??= new DragVisualAdorner(dragAdornerSurface, download);
        dragAdornerLayer.Add(dragVisual);
    }

    private void MoveDragVisualToCursor()
    {
        if (!TryGetCursorPosition(out WpfPoint screenPoint))
        {
            return;
        }

        MoveDragVisual(dragAdornerSurface.PointFromScreen(screenPoint));
    }

    private void MoveDragVisual(WpfPoint point)
    {
        dragVisual?.Move(point);
    }

    private void CleanupDownloadDragState()
    {
        downloadDragStartPoint = null;
        AnimateDraggedDownloadRow(1);
        draggedDownloadRow = null;

        if (dragAdornerLayer is not null && dragVisual is not null)
        {
            dragAdornerLayer.Remove(dragVisual);
        }

        dragVisual = null;
    }

    private void EnsureInstallDropIndicator()
    {
        installAdornerLayer ??= AdornerLayer.GetAdornerLayer(modsDropSurface);
        if (installAdornerLayer is null)
        {
            return;
        }

        if (installDropIndicator is null)
        {
            installDropIndicator = new DropIndicatorAdorner(modsDropSurface, "Установить сюда");
            installAdornerLayer.Add(installDropIndicator);
        }
    }

    private void CleanupInstallDropIndicator()
    {
        if (installAdornerLayer is not null && installDropIndicator is not null)
        {
            installAdornerLayer.Remove(installDropIndicator);
        }

        installDropIndicator = null;
    }

    private void HideInstallDropIndicator()
    {
        installDropIndicator?.Hide();
    }

    private bool TryGetModInsertionTarget(WpfDragEventArgs e, out int insertionIndex, out double indicatorY)
    {
        insertionIndex = modsGrid.Items.Count;
        indicatorY = Math.Clamp(
            modsDropSurface.ActualHeight / 2,
            16,
            Math.Max(16, modsDropSurface.ActualHeight - 16));

        if (modsGrid.Items.Count == 0)
        {
            insertionIndex = 0;
            return true;
        }

        WpfPoint point = e.GetPosition(modsDropSurface);
        DataGridRow? firstRealizedRow = null;
        DataGridRow? lastRealizedRow = null;
        double firstTop = 0;
        double lastBottom = Math.Max(0, modsDropSurface.ActualHeight - 8);

        foreach ((int rowIndex, DataGridRow row) in GetRealizedModRows())
        {
            WpfPoint rowTop = row.TranslatePoint(new WpfPoint(0, 0), modsDropSurface);
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
            insertionIndex = modsGrid.ItemContainerGenerator.IndexFromContainer(firstRealizedRow);
            indicatorY = firstTop;
            return true;
        }

        if (lastRealizedRow is not null)
        {
            insertionIndex = modsGrid.ItemContainerGenerator.IndexFromContainer(lastRealizedRow) + 1;
            indicatorY = lastBottom;
            return true;
        }

        return false;
    }

    private List<(int Index, DataGridRow Row)> GetRealizedModRows()
    {
        List<(int Index, DataGridRow Row)> rows = new();
        foreach (DataGridRow row in FindVisualChildren<DataGridRow>(modsGrid))
        {
            if (row.Item is not ModEntry ||
                row.Visibility != Visibility.Visible ||
                row.ActualHeight <= 0.5)
            {
                continue;
            }

            int rowIndex = modsGrid.ItemContainerGenerator.IndexFromContainer(row);
            if (rowIndex >= 0)
            {
                rows.Add((rowIndex, row));
            }
        }

        rows.Sort(static (left, right) => left.Index.CompareTo(right.Index));
        return rows;
    }

    private void AutoScrollMods(WpfPoint point)
    {
        modsScrollViewer ??= FindVisualChild<ScrollViewer>(modsGrid);
        if (modsScrollViewer is null)
        {
            return;
        }

        const double edgeSize = 34;
        if (point.Y < edgeSize)
        {
            modsScrollViewer.LineUp();
        }
        else if (point.Y > modsGrid.ActualHeight - edgeSize)
        {
            modsScrollViewer.LineDown();
        }
    }

    private void AnimateDraggedDownloadRow(double opacity)
    {
        if (draggedDownloadRow is null)
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
        draggedDownloadRow.BeginAnimation(UIElement.OpacityProperty, animation);
    }

    private void ShowDownloadsDropOverlay()
    {
        if (isDownloadsDropOverlayVisible)
        {
            return;
        }

        isDownloadsDropOverlayVisible = true;
        downloadsDropOverlay.Visibility = Visibility.Visible;
        AnimateDropOverlay(1);
    }

    private void HideDownloadsDropOverlay()
    {
        if (!isDownloadsDropOverlayVisible)
        {
            return;
        }

        isDownloadsDropOverlayVisible = false;
        DoubleAnimation opacity = CreateOverlayAnimation(0);
        opacity.Completed += (_, _) =>
        {
            if (!isDownloadsDropOverlayVisible)
            {
                downloadsDropOverlay.Visibility = Visibility.Collapsed;
            }
        };

        downloadsDropOverlay.BeginAnimation(UIElement.OpacityProperty, opacity, HandoffBehavior.SnapshotAndReplace);
    }

    private void AnimateDropOverlay(double opacity)
    {
        downloadsDropOverlay.BeginAnimation(
            UIElement.OpacityProperty,
            CreateOverlayAnimation(opacity),
            HandoffBehavior.SnapshotAndReplace);
    }

    private static DoubleAnimation CreateOverlayAnimation(double value)
    {
        return new DoubleAnimation(value, SystemParameters.ClientAreaAnimation
            ? new Duration(QuickAnimationDuration)
            : new Duration(TimeSpan.Zero))
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
        };
    }

    private static bool TryGetSupportedDropFiles(WpfIDataObject data, out IReadOnlyList<string> files)
    {
        files = Array.Empty<string>();
        if (!data.GetDataPresent(WpfDataFormats.FileDrop) ||
            data.GetData(WpfDataFormats.FileDrop) is not string[] droppedPaths)
        {
            return false;
        }

        files = ModArchiveFileFilter.GetSupportedExistingFiles(droppedPaths);
        return files.Count > 0;
    }

    private static bool TryGetDraggedDownload(WpfDragEventArgs e, out DownloadEntry? download)
    {
        download = null;
        if (!e.Data.GetDataPresent(DownloadDataFormat))
        {
            return false;
        }

        download = e.Data.GetData(DownloadDataFormat) as DownloadEntry;
        return download is not null;
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

    private static bool TryGetCursorPosition(out WpfPoint point)
    {
        if (GetCursorPos(out NativePoint nativePoint))
        {
            point = new WpfPoint(nativePoint.X, nativePoint.Y);
            return true;
        }

        point = default;
        return false;
    }

    [DllImport("user32.dll")]
    private static extern bool GetCursorPos(out NativePoint point);

    [StructLayout(LayoutKind.Sequential)]
    private readonly struct NativePoint
    {
        public readonly int X;
        public readonly int Y;
    }

}
