using System.Collections.ObjectModel;

namespace Fluxora.App.Services;

public static class OrderedCollectionSyncService
{
    public static void Sync<T>(
        ObservableCollection<T> target,
        IReadOnlyList<T> source,
        Func<T, string> keySelector,
        Func<T, T, bool> areEquivalent,
        StringComparer? keyComparer = null)
    {
        keyComparer ??= StringComparer.OrdinalIgnoreCase;

        HashSet<string> sourceKeys = new(keyComparer);
        foreach (T item in source)
        {
            sourceKeys.Add(keySelector(item));
        }

        for (int index = target.Count - 1; index >= 0; --index)
        {
            if (!sourceKeys.Contains(keySelector(target[index])))
            {
                target.RemoveAt(index);
            }
        }

        for (int index = 0; index < source.Count; ++index)
        {
            T incoming = source[index];
            string key = keySelector(incoming);
            int existingIndex = IndexOf(target, keySelector, key, keyComparer);
            if (existingIndex < 0)
            {
                target.Insert(index, incoming);
                continue;
            }

            if (existingIndex != index)
            {
                target.Move(existingIndex, index);
            }

            if (!areEquivalent(target[index], incoming))
            {
                target[index] = incoming;
            }
        }
    }

    private static int IndexOf<T>(
        ObservableCollection<T> target,
        Func<T, string> keySelector,
        string key,
        StringComparer keyComparer)
    {
        for (int index = 0; index < target.Count; ++index)
        {
            if (keyComparer.Equals(keySelector(target[index]), key))
            {
                return index;
            }
        }

        return -1;
    }
}
