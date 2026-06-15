using System.Windows;
using System.Windows.Media;
using Control = System.Windows.Controls.Control;
using Brush = System.Windows.Media.Brush;

namespace Fluxora.App.Controls;

/// <summary>
/// A lightweight vector icon. The icon geometry is authored on a 24x24 canvas (the same
/// coordinate space as the SVG sources in the repository-level Icons folder) and is rendered crisp at any size.
///
/// The stroke colour follows the inherited <see cref="Control.Foreground"/>, so an icon
/// placed inside a button automatically picks up that button's text/hover colour without
/// any extra bindings. This keeps the icons themeable from a single place.
/// </summary>
public sealed class LineIcon : Control
{
    static LineIcon()
    {
        DefaultStyleKeyProperty.OverrideMetadata(
            typeof(LineIcon),
            new FrameworkPropertyMetadata(typeof(LineIcon)));
    }

    public static readonly DependencyProperty DataProperty = DependencyProperty.Register(
        nameof(Data),
        typeof(Geometry),
        typeof(LineIcon),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty StrokeThicknessProperty = DependencyProperty.Register(
        nameof(StrokeThickness),
        typeof(double),
        typeof(LineIcon),
        new FrameworkPropertyMetadata(1.8, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty FillProperty = DependencyProperty.Register(
        nameof(Fill),
        typeof(Brush),
        typeof(LineIcon),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    /// <summary>The icon path geometry, authored on a 0..24 canvas.</summary>
    public Geometry? Data
    {
        get => (Geometry?)GetValue(DataProperty);
        set => SetValue(DataProperty, value);
    }

    /// <summary>Stroke weight expressed in the 24-unit icon space.</summary>
    public double StrokeThickness
    {
        get => (double)GetValue(StrokeThicknessProperty);
        set => SetValue(StrokeThicknessProperty, value);
    }

    /// <summary>Optional fill for solid icons. Most icons are stroke-only.</summary>
    public Brush? Fill
    {
        get => (Brush?)GetValue(FillProperty);
        set => SetValue(FillProperty, value);
    }
}
