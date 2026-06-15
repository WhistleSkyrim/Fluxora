using System;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace Fluxora.App.Services;

/// <summary>
/// Attached behaviour that smoothly folds a list row (a <c>DataGridRow</c> or <c>ListBoxItem</c>)
/// in and out when its bound "hidden" flag flips, instead of snapping
/// <see cref="UIElement.Visibility"/>. It drives a <see cref="FrameworkElement.LayoutTransform"/>
/// ScaleY — so the row's layout height shrinks and the rows below slide up/down — together with a
/// short opacity fade.
///
/// The animation only plays for a genuine user fold. When a virtualised container is recycled onto
/// a different row its <see cref="FrameworkElement.DataContext"/> changes, and we apply the target
/// state instantly so scrolling a long list never animates. This is a UI-only concern: the C++
/// core never knows a separator is folded.
/// </summary>
public static class AnimatedCollapseBehavior
{
    private const int DurationMs = 160;

    private sealed class RowState
    {
        public readonly ScaleTransform Scale = new(1d, 1d);
        public object? Context;
    }

    private static readonly ConditionalWeakTable<FrameworkElement, RowState> States = new();

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

        RowState state = States.GetValue(element, static _ => new RowState());

        // Own the row's LayoutTransform (a recycled container may carry a stale one).
        if (!ReferenceEquals(element.LayoutTransform, state.Scale))
        {
            element.LayoutTransform = state.Scale;
        }

        // First time we see this container (old value is unset) or the container was recycled onto
        // a different row (DataContext changed): snap to the target, do not animate.
        bool firstBind = e.OldValue is not bool;
        if (firstBind || !ReferenceEquals(state.Context, element.DataContext))
        {
            state.Context = element.DataContext;
            ApplyInstant(element, state.Scale, hidden);
            return;
        }

        AnimateFold(element, state.Scale, collapse: hidden);
    }

    private static void ApplyInstant(FrameworkElement element, ScaleTransform scale, bool hidden)
    {
        scale.BeginAnimation(ScaleTransform.ScaleYProperty, null);
        element.BeginAnimation(UIElement.OpacityProperty, null);
        scale.ScaleY = hidden ? 0d : 1d;
        element.Opacity = hidden ? 0d : 1d;
        element.Visibility = hidden ? Visibility.Collapsed : Visibility.Visible;
    }

    private static void AnimateFold(FrameworkElement element, ScaleTransform scale, bool collapse)
    {
        // Stay rendered while we shrink/grow; the collapse hides the row only once it reaches zero.
        element.Visibility = Visibility.Visible;

        var duration = new Duration(TimeSpan.FromMilliseconds(DurationMs));
        var ease = new CubicEase { EasingMode = collapse ? EasingMode.EaseIn : EasingMode.EaseOut };

        var scaleAnim = new DoubleAnimation
        {
            To = collapse ? 0d : 1d,
            Duration = duration,
            EasingFunction = ease,
        };
        var fadeAnim = new DoubleAnimation
        {
            To = collapse ? 0d : 1d,
            Duration = duration,
            EasingFunction = ease,
        };

        if (collapse)
        {
            scaleAnim.Completed += (_, _) =>
            {
                // Guard against a fast re-expand landing before this collapse finished.
                if (GetIsHidden(element) is true)
                {
                    element.Visibility = Visibility.Collapsed;
                }
            };
        }

        scale.BeginAnimation(ScaleTransform.ScaleYProperty, scaleAnim);
        element.BeginAnimation(UIElement.OpacityProperty, fadeAnim);
    }
}
