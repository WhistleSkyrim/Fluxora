using System.Windows;
using System.Windows.Controls.Primitives;
using System.Windows.Media.Animation;
using ProgressBar = System.Windows.Controls.ProgressBar;

namespace Fluxora.App.Services;

/// <summary>
/// Attached behaviour that drives a <see cref="ProgressBar"/> towards a target value with an
/// eased animation, so progress glides forward instead of snapping between integer updates.
/// Bind <c>SmoothProgressService.TargetValue</c> to the view-model percentage and leave the
/// bar's own <see cref="ProgressBar.Value"/> unbound &mdash; this service owns it.
/// </summary>
public static class SmoothProgressService
{
    private static readonly Duration FillDuration = new(TimeSpan.FromMilliseconds(480));

    public static readonly DependencyProperty TargetValueProperty =
        DependencyProperty.RegisterAttached(
            "TargetValue",
            typeof(double),
            typeof(SmoothProgressService),
            new PropertyMetadata(0.0, OnTargetValueChanged));

    public static void SetTargetValue(DependencyObject element, double value)
    {
        element.SetValue(TargetValueProperty, value);
    }

    public static double GetTargetValue(DependencyObject element)
    {
        return (double)element.GetValue(TargetValueProperty);
    }

    private static void OnTargetValueChanged(DependencyObject element, DependencyPropertyChangedEventArgs e)
    {
        if (element is not ProgressBar bar)
        {
            return;
        }

        double target = (double)e.NewValue;

        // A drop (e.g. a reset back to zero before the next run) should land instantly rather
        // than visibly draining; only forward motion is animated.
        if (target <= bar.Value)
        {
            bar.BeginAnimation(RangeBase.ValueProperty, null);
            bar.Value = target;
            return;
        }

        DoubleAnimation animation = new(target, FillDuration)
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut }
        };
        bar.BeginAnimation(RangeBase.ValueProperty, animation);
    }
}
