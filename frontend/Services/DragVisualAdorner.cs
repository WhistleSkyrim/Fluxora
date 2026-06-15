using System.Globalization;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using Fluxora.App.Models;
using WpfApplication = System.Windows.Application;
using WpfBrush = System.Windows.Media.Brush;
using WpfColor = System.Windows.Media.Color;
using WpfFontFamily = System.Windows.Media.FontFamily;
using WpfPen = System.Windows.Media.Pen;
using WpfPoint = System.Windows.Point;

namespace Fluxora.App.Services;

internal sealed class DragVisualAdorner : Adorner
{
    private static readonly WpfBrush BackgroundBrush = new SolidColorBrush(WpfColor.FromRgb(32, 29, 49));
    private static readonly WpfBrush BorderBrush = new SolidColorBrush(WpfColor.FromRgb(139, 92, 246));
    private static readonly WpfBrush TextBrush = new SolidColorBrush(WpfColor.FromRgb(247, 244, 255));
    private static readonly WpfBrush MutedTextBrush = new SolidColorBrush(WpfColor.FromRgb(197, 190, 218));

    private readonly string caption;
    private readonly string title;
    private readonly FormattedText captionText;
    private readonly FormattedText titleText;
    private WpfPoint position;

    public DragVisualAdorner(UIElement adornedElement, ModEntry item)
        : this(adornedElement, item.IsSeparator ? "Разделитель" : "Мод", item.DisplayName)
    {
    }

    public DragVisualAdorner(UIElement adornedElement, PluginEntry item)
        : this(adornedElement, item.IsSeparator ? "Разделитель" : "Плагин", item.DisplayName)
    {
    }

    private DragVisualAdorner(UIElement adornedElement, string caption, string title)
        : base(adornedElement)
    {
        IsHitTestVisible = false;
        this.caption = caption;
        this.title = title;
        captionText = CreateText(caption, 10, MutedTextBrush, FontWeights.SemiBold, 520);
        titleText = CreateText(title, 12, TextBrush, FontWeights.Bold, 520);
    }

    public void Move(WpfPoint point)
    {
        position = point;
        InvalidateVisual();
    }

    protected override void OnRender(DrawingContext drawingContext)
    {
        base.OnRender(drawingContext);

        double maxWidth = Math.Max(180, RenderSize.Width - 36);
        captionText.MaxTextWidth = maxWidth;
        titleText.MaxTextWidth = maxWidth;

        double width = Math.Min(maxWidth, Math.Max(210, Math.Max(captionText.Width, titleText.Width) + 28));
        double height = 54;
        double left = Math.Min(Math.Max(12, position.X + 16), Math.Max(12, RenderSize.Width - width - 12));
        double top = Math.Min(Math.Max(10, position.Y + 16), Math.Max(10, RenderSize.Height - height - 10));
        System.Windows.Rect rect = new(left, top, width, height);

        drawingContext.PushOpacity(0.94);
        drawingContext.DrawRoundedRectangle(
            BackgroundBrush,
            new WpfPen(BorderBrush, 1.2),
            rect,
            10,
            10);
        drawingContext.Pop();

        drawingContext.DrawText(captionText, new WpfPoint(left + 14, top + 8));
        titleText.MaxTextWidth = width - 28;
        titleText.Trimming = TextTrimming.CharacterEllipsis;
        drawingContext.DrawText(titleText, new WpfPoint(left + 14, top + 26));
    }

    private static FormattedText CreateText(
        string text,
        double size,
        WpfBrush brush,
        FontWeight weight,
        double maxWidth)
    {
        FormattedText formattedText = new(
            text,
            CultureInfo.CurrentUICulture,
            System.Windows.FlowDirection.LeftToRight,
            new Typeface(
                WpfApplication.Current.TryFindResource("FluxoraFontBody") as WpfFontFamily
                    ?? new WpfFontFamily("Segoe UI Variable, Segoe UI"),
                FontStyles.Normal, weight, FontStretches.Normal),
            size,
            brush,
            VisualTreeHelper.GetDpi(WpfApplication.Current.MainWindow ?? WpfApplication.Current.Windows[0]).PixelsPerDip)
        {
            MaxTextWidth = maxWidth,
            Trimming = TextTrimming.CharacterEllipsis
        };

        return formattedText;
    }
}
