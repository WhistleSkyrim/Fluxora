using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Fluxora.App.Models;

public sealed class ModEntry : ICollapsibleListItem, INotifyPropertyChanged
{
    public required string Id { get; init; }
    public string OrderId { get; init; } = string.Empty;
    public string Kind { get; init; } = "mod";
    public int Order { get; init; }
    public string ModUuid { get; init; } = string.Empty;
    public string SeparatorTitle { get; init; } = string.Empty;
    public required string Name { get; init; }
    public string Version { get; init; } = "Unknown";
    public string LatestVersion { get; init; } = string.Empty;
    public string LastCheckedAt { get; init; } = string.Empty;
    public string UpdateStatus { get; init; } = string.Empty;
    public string ConflictStatus { get; init; } = string.Empty;
    public int FileCount { get; init; }
    public int ConflictingFileCount { get; init; }
    public int OverwrittenFileCount { get; init; }
    public int OverwritingFileCount { get; init; }
    private bool isEnabled;
    public bool IsEnabled
    {
        get => isEnabled;
        set => SetField(ref isEnabled, value);
    }
    public bool CanCheckUpdates { get; init; }
    public bool HasUpdate { get; init; }

    public bool IsSeparator => string.Equals(Kind, "separator", StringComparison.OrdinalIgnoreCase);

    public bool IsMod => !IsSeparator;

    public string CollapseKey => string.IsNullOrWhiteSpace(OrderId) ? Id : OrderId;

    private bool isCollapsed;
    public bool IsCollapsed
    {
        get => isCollapsed;
        set => SetField(ref isCollapsed, value);
    }

    private bool isHidden;
    public bool IsHidden
    {
        get => isHidden;
        set => SetField(ref isHidden, value);
    }

    private bool isUnderSeparator;
    public bool IsUnderSeparator
    {
        get => isUnderSeparator;
        set => SetField(ref isUnderSeparator, value);
    }

    private bool isSelected;
    public bool IsSelected
    {
        get => isSelected;
        set => SetField(ref isSelected, value);
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    private void SetField(ref bool field, bool value, [CallerMemberName] string? propertyName = null)
    {
        if (field == value)
        {
            return;
        }

        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }

    public string DisplayName => IsSeparator
        ? (string.IsNullOrWhiteSpace(SeparatorTitle) ? Name : SeparatorTitle)
        : Name;

    public string VersionText => IsSeparator
        ? string.Empty
        : string.IsNullOrWhiteSpace(Version)
            ? "Unknown"
            : Version;

    public string LatestVersionText => IsSeparator
        ? string.Empty
        : string.IsNullOrWhiteSpace(LatestVersion)
            ? "Unknown"
            : LatestVersion;

    public string FileCountText => IsSeparator
        ? string.Empty
        : FileCount < 0
            ? "Файлы не просканированы"
        : FileCount == 0
            ? "Файлов нет"
            : $"{FileCount} файлов";

    public string UpdateBadgeText
    {
        get
        {
            if (IsSeparator)
            {
                return string.Empty;
            }

            if (HasUpdate)
            {
                return "Update";
            }

            return CanCheckUpdates ? "Nexus" : "Local";
        }
    }

    public bool IsFullyOverwritten => IsMod &&
        FileCount > 0 &&
        OverwrittenFileCount >= FileCount;

    public bool ShowsOverwritesIcon => IsMod &&
        !IsFullyOverwritten &&
        OverwritingFileCount > 0;

    public bool ShowsOverwrittenIcon => IsMod &&
        !IsFullyOverwritten &&
        OverwrittenFileCount > 0;

    public bool HasConflictIndicator => IsFullyOverwritten ||
        ShowsOverwritesIcon ||
        ShowsOverwrittenIcon;

    public string ConflictIndicatorToolTip
    {
        get
        {
            if (IsSeparator || !HasConflictIndicator)
            {
                return string.Empty;
            }

            if (IsFullyOverwritten)
            {
                return "Мод полностью перекрыт другими модами";
            }

            if (ShowsOverwritesIcon && ShowsOverwrittenIcon)
            {
                return $"Перекрывает файлов: {OverwritingFileCount}; перекрыто файлов: {OverwrittenFileCount}";
            }

            return ShowsOverwritesIcon
                ? $"Мод перекрывает файлов: {OverwritingFileCount}"
                : $"Мод перекрыт файлов: {OverwrittenFileCount}";
        }
    }
}
