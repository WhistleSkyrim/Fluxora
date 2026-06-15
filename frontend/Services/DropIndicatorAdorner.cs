using System.Globalization;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using WpfApplication = System.Windows.Application;
using WpfBrush = System.Windows.Media.Brush;
using WpfColor = System.Windows.Media.Color;
using WpfFontFamily = System.Windows.Media.FontFamily;
using WpfPen = System.Windows.Media.Pen;
using WpfPoint = System.Windows.Point;

namespace Fluxora.App.Services;

internal sealed class DropIndicatorAdorner : Adorner
{
    private static readonly WpfBrush AccentBrush = new SolidColorBrush(WpfColor.FromRgb(139, 92, 246));
    private static readonly WpfBrush LabelBackgroundBrush = new SolidColorBrush(WpfColor.FromRgb(36, 26, 70));
    private static readonly WpfBrush LabelTextBrush = new SolidColorBrush(WpfColor.FromRgb(247, 244, 255));

    private readonly FormattedText labelText;
    private double y;
    private bool isVisible;

    public DropIndicatorAdorner(UIElement adornedElement)
        : base(adornedElement)
    {
        IsHitTestVisible = false;
        labelText = new FormattedText(
            "Вставить сюда",
            CultureInfo.CurrentUICulture,
            System.Windows.FlowDirection.LeftToRight,
            new Typeface(
                WpfApplication.Current.TryFindResource("FluxoraFontBody") as WpfFontFamily
                    ?? new WpfFontFamily("Segoe UI Variable, Segoe UI"),
                FontStyles.Normal, FontWeights.SemiBold, FontStretches.Normal),
            10,
            LabelTextBrush,
            VisualTreeHelper.GetDpi(WpfApplication.Current.MainWindow ?? WpfApplication.Current.Windows[0]).PixelsPerDip);
    }

    public void Update(double indicatorY)
    {
        y = Math.Clamp(indicatorY, 4, Math.Max(4, RenderSize.Height - 4));
        isVisible = true;
        InvalidateVisual();
    }

    public void Hide()
    {
        if (!isVisible)
        {
            return;
        }

        isVisible = false;
        InvalidateVisual();
    }

    protected override void OnRender(DrawingContext drawingContext)
    {
        base.OnRender(drawingContext);
        if (!isVisible)
        {
            return;
        }

        double right = Math.Max(24, RenderSize.Width - 8);
        WpfPen linePen = new(AccentBrush, 2.2)
        {
            StartLineCap = PenLineCap.Round,
            EndLineCap = PenLineCap.Round
        };
        drawingContext.DrawLine(linePen, new WpfPoint(8, y), new WpfPoint(right, y));

        System.Windows.Rect labelRect = new(14, Math.Max(4, y - 12), labelText.Width + 16, 22);
        drawingContext.DrawRoundedRectangle(LabelBackgroundBrush, new WpfPen(AccentBrush, 1), labelRect, 8, 8);
        drawingContext.DrawText(labelText, new WpfPoint(labelRect.Left + 8, labelRect.Top + 4));
    }
}
