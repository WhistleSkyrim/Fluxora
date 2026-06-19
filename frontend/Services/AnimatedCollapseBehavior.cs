using System.Windows;

namespace Fluxora.App.Services;

/// <summary>
/// Attached behaviour that applies a folded row's visibility instantly when its bound "hidden"
/// flag flips. Separator folding used to animate row layout height, but that made virtualised
/// scroll hosts continuously recompute their extent and caused scrollbar jitter/clipping.
/// This is a UI-only concern: the C++ core never knows a separator is folded.
/// </summary>
public static class AnimatedCollapseBehavior
{
    /// <summary>Bind this to the row item's "is folded away" flag (e.g. <c>IsHidden</c>).</summary>
    public static readonly DependencyProperty IsHiddenProperty =
        DependencyProperty.RegisterAttached(
            "IsHidden",
            typeof(bool?),
            typeof(AnimatedCollapseBehavior),
            new PropertyMetadata(null, OnIsHiddenChanged));

    public static void SetIsHidden(DependencyObject element, bool? value) =>
        element.SetValue(IsHiddenProperty, value);

    public static bool? GetIsHidden(DependencyObject element) =>
        (bool?)element.GetValue(IsHiddenProperty);

    private static void OnIsHiddenChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not FrameworkElement element || e.NewValue is not bool hidden)
        {
            return;
        }

        element.BeginAnimation(UIElement.OpacityProperty, null);
        element.Opacity = hidden ? 0d : 1d;
        element.Visibility = hidden ? Visibility.Collapsed : Visibility.Visible;
    }
}
