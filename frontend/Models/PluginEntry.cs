using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Fluxora.App.Models;

public sealed class PluginEntry : ICollapsibleListItem, INotifyPropertyChanged
{
    public required string Id { get; init; }
    public string OrderId { get; init; } = string.Empty;
    public string Kind { get; init; } = "plugin";
    public int Order { get; init; }
    public required string Name { get; init; }
    public string SeparatorTitle { get; init; } = string.Empty;
    public string Extension { get; init; } = string.Empty;
    public string SourceMod { get; init; } = string.Empty;
    public bool IsEnabled { get; set; }
    public bool IsMaster { get; init; }
    public bool IsLight { get; init; }
    public bool IsLocked { get; init; }
    public string LockReason { get; init; } = string.Empty;

    public bool IsSeparator => string.Equals(Kind, "separator", StringComparison.OrdinalIgnoreCase);

    public bool IsPlugin => !IsSeparator;

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

    public bool CanToggle => IsPlugin && !IsLocked;
    public bool CanMove => !IsLocked;
    public string OrderText => IsSeparator ? string.Empty : (Order + 1).ToString("00");

    public string DisplayName => IsSeparator
        ? (string.IsNullOrWhiteSpace(SeparatorTitle) ? Name : SeparatorTitle)
        : Name;

    public string TypeText
    {
        get
        {
            if (IsSeparator)
            {
                return string.Empty;
            }

            if (IsMaster)
            {
                return "Master";
            }

            return IsLight ? "Light" : "Plugin";
        }
    }

    public string SourceText
    {
        get
        {
            if (IsSeparator)
            {
                return "Разделитель визуального порядка.";
            }

            if (!string.IsNullOrWhiteSpace(LockReason))
            {
                return LockReason;
            }

            return string.IsNullOrWhiteSpace(SourceMod)
                ? "Файл найден в списке модов"
                : $"Мод: {SourceMod}";
        }
    }
}
