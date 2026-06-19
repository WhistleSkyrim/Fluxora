namespace Fluxora.App.Services;

public enum RangeSelectionGesture
{
    Replace,
    Toggle,
    Extend
}

public static class RangeSelectionService
{
    public static string? Apply<T>(
        IReadOnlyList<T> items,
        T? item,
        Func<T, string> keySelector,
        Func<T, bool> isSelected,
        Action<T, bool> setSelected,
        string? anchorKey,
        RangeSelectionGesture gesture,
        StringComparer? keyComparer = null)
        where T : class
    {
        keyComparer ??= StringComparer.OrdinalIgnoreCase;
        if (item is null)
        {
            if (gesture == RangeSelectionGesture.Replace)
            {
                Clear(items, setSelected);
                return null;
            }

            return anchorKey;
        }

        int itemIndex = IndexOf(items, keySelector, keySelector(item), keyComparer);
        if (itemIndex < 0)
        {
            return anchorKey;
        }

        string itemKey = keySelector(items[itemIndex]);
        switch (gesture)
        {
            case RangeSelectionGesture.Toggle:
                setSelected(items[itemIndex], !isSelected(items[itemIndex]));
                return itemKey;

            case RangeSelectionGesture.Extend:
                int anchorIndex = string.IsNullOrWhiteSpace(anchorKey)
                    ? -1
                    : IndexOf(items, keySelector, anchorKey, keyComparer);
                if (anchorIndex < 0)
                {
                    anchorIndex = itemIndex;
                }

                int start = Math.Min(anchorIndex, itemIndex);
                int end = Math.Max(anchorIndex, itemIndex);
                for (int index = start; index <= end; ++index)
                {
                    setSelected(items[index], true);
                }

                return itemKey;

            default:
                Clear(items, setSelected);
                setSelected(items[itemIndex], true);
                return itemKey;
        }
    }

    public static string? SelectAll<T>(
        IReadOnlyList<T> items,
        Func<T, string> keySelector,
        Action<T, bool> setSelected,
        string? preferredAnchorKey = null,
        StringComparer? keyComparer = null)
    {
        keyComparer ??= StringComparer.OrdinalIgnoreCase;
        foreach (T item in items)
        {
            setSelected(item, true);
        }

        if (!string.IsNullOrWhiteSpace(preferredAnchorKey) &&
            IndexOf(items, keySelector, preferredAnchorKey, keyComparer) >= 0)
        {
            return preferredAnchorKey;
        }

        return items.Count == 0 ? null : keySelector(items[0]);
    }

    public static void Clear<T>(IReadOnlyList<T> items, Action<T, bool> setSelected)
    {
        foreach (T item in items)
        {
            setSelected(item, false);
        }
    }

    private static int IndexOf<T>(
        IReadOnlyList<T> items,
        Func<T, string> keySelector,
        string key,
        StringComparer keyComparer)
    {
        for (int index = 0; index < items.Count; ++index)
        {
            if (keyComparer.Equals(keySelector(items[index]), key))
            {
                return index;
            }
        }

        return -1;
    }
}
