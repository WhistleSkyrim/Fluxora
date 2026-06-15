using System.Windows;

namespace Fluxora.App.Services;

public static class ProgressAnimationService
{
    public static readonly DependencyProperty IsActiveProperty =
        DependencyProperty.RegisterAttached(
            "IsActive",
            typeof(bool),
            typeof(ProgressAnimationService),
            new PropertyMetadata(false));

    public static void SetIsActive(DependencyObject element, bool value)
    {
        element.SetValue(IsActiveProperty, value);
    }

    public static bool GetIsActive(DependencyObject element)
    {
        return (bool)element.GetValue(IsActiveProperty);
    }
}
