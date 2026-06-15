namespace Fluxora.App.Models;

/// <summary>
/// A row in an ordered list that can sit under a collapsible separator. This is pure UI
/// view state — the C++ core never sees which separators the user has folded. Implemented by
/// both <see cref="ModEntry"/> and <see cref="PluginEntry"/> so a single
/// <c>SeparatorCollapseService</c> can drive both lists.
/// </summary>
public interface ICollapsibleListItem
{
    /// <summary>True when this row is itself a separator header.</summary>
    bool IsSeparator { get; }

    /// <summary>Stable identity used to remember a separator's folded state across list rebuilds.</summary>
    string CollapseKey { get; }

    /// <summary>For separators: whether this separator is currently folded.</summary>
    bool IsCollapsed { get; set; }

    /// <summary>True when a folded separator above this row hides it.</summary>
    bool IsHidden { get; set; }

    /// <summary>True when this row belongs to a separator group and should be indented.</summary>
    bool IsUnderSeparator { get; set; }
}
