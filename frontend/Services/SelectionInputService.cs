using System.Windows.Input;

namespace Fluxora.App.Services;

public static class SelectionInputService
{
    public static bool HasRangeSelectionModifier(ModifierKeys modifiers)
    {
        return (modifiers & (ModifierKeys.Control | ModifierKeys.Shift)) != ModifierKeys.None;
    }

    public static bool IsSelectAllGesture(Key key, Key systemKey, ModifierKeys modifiers)
    {
        return (key == Key.A || systemKey == Key.A) &&
            (modifiers & ModifierKeys.Control) == ModifierKeys.Control;
    }

    public static RangeSelectionGesture ResolveGesture(ModifierKeys modifiers)
    {
        if ((modifiers & ModifierKeys.Shift) == ModifierKeys.Shift)
        {
            return RangeSelectionGesture.Extend;
        }

        return (modifiers & ModifierKeys.Control) == ModifierKeys.Control
            ? RangeSelectionGesture.Toggle
            : RangeSelectionGesture.Replace;
    }
}
