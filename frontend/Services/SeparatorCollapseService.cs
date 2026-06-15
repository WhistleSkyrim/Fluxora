using System;
using System.Collections.Generic;
using Fluxora.App.Models;

namespace Fluxora.App.Services;

/// <summary>
/// Tracks which separators the user has folded in an ordered list (mods or plugins) and
/// projects that state onto the rows. This is a UI-only concern: the C++ core owns mod/plugin
/// order, this service only decides what is visually hidden or indented.
///
/// State is keyed by the separator's stable <see cref="ICollapsibleListItem.CollapseKey"/> so a
/// fold survives the full list rebuilds that happen on every sync.
/// </summary>
public sealed class SeparatorCollapseService
{
    private readonly HashSet<string> collapsedKeys = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Flips the folded state of a separator. No-op for non-separators.</summary>
    public void Toggle(ICollapsibleListItem? separator)
    {
        if (separator is not { IsSeparator: true } || string.IsNullOrWhiteSpace(separator.CollapseKey))
        {
            return;
        }

        if (!collapsedKeys.Add(separator.CollapseKey))
        {
            collapsedKeys.Remove(separator.CollapseKey);
        }
    }

    /// <summary>
    /// Recomputes <see cref="ICollapsibleListItem.IsCollapsed"/>,
    /// <see cref="ICollapsibleListItem.IsHidden"/> and
    /// <see cref="ICollapsibleListItem.IsUnderSeparator"/> for every row in document order.
    /// Call after each list sync and after every <see cref="Toggle"/>.
    /// </summary>
    public void Apply<T>(IReadOnlyList<T> items) where T : ICollapsibleListItem
    {
        bool insideCollapsedGroup = false;
        bool seenSeparator = false;

        foreach (T item in items)
        {
            if (item.IsSeparator)
            {
                bool collapsed = !string.IsNullOrWhiteSpace(item.CollapseKey) &&
                                 collapsedKeys.Contains(item.CollapseKey);
                item.IsCollapsed = collapsed;
                item.IsHidden = false;          // a separator header is always visible
                item.IsUnderSeparator = false;  // headers are not indented
                insideCollapsedGroup = collapsed;
                seenSeparator = true;
            }
            else
            {
                item.IsHidden = insideCollapsedGroup;
                item.IsUnderSeparator = seenSeparator;
            }
        }
    }
}
