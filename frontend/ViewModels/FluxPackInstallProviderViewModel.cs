using System.ComponentModel;
using System.Runtime.CompilerServices;
using Fluxora.App.Models;

namespace Fluxora.App.ViewModels;

public sealed class FluxPackInstallProviderViewModel : INotifyPropertyChanged
{
    private string providerId = string.Empty;
    private string displayName = string.Empty;
    private string iconText = "?";
    private string accentBrush = "#A78BFA";
    private ulong totalCount;
    private ulong completedCount;
    private ulong pendingCount;
    private ulong failedCount;
    private int progressPercent;
    private string currentItem = string.Empty;
    private string statusText = "Ожидает";
    private string countText = "0 модов";

    public event PropertyChangedEventHandler? PropertyChanged;

    public string ProviderId
    {
        get => providerId;
        private set => SetField(ref providerId, value);
    }

    public string DisplayName
    {
        get => displayName;
        private set => SetField(ref displayName, value);
    }

    public string IconText
    {
        get => iconText;
        private set => SetField(ref iconText, value);
    }

    public string AccentBrush
    {
        get => accentBrush;
        private set => SetField(ref accentBrush, value);
    }

    public ulong TotalCount
    {
        get => totalCount;
        private set => SetField(ref totalCount, value);
    }

    public ulong CompletedCount
    {
        get => completedCount;
        private set => SetField(ref completedCount, value);
    }

    public ulong PendingCount
    {
        get => pendingCount;
        private set => SetField(ref pendingCount, value);
    }

    public ulong FailedCount
    {
        get => failedCount;
        private set => SetField(ref failedCount, value);
    }

    public int ProgressPercent
    {
        get => progressPercent;
        private set => SetField(ref progressPercent, Math.Clamp(value, 0, 100));
    }

    public string CurrentItem
    {
        get => currentItem;
        private set => SetField(ref currentItem, value);
    }

    public string StatusText
    {
        get => statusText;
        private set => SetField(ref statusText, value);
    }

    public string CountText
    {
        get => countText;
        private set => SetField(ref countText, value);
    }

    public void Apply(FluxPackInstallProviderProgress progress)
    {
        string id = string.IsNullOrWhiteSpace(progress.ProviderId)
            ? "unknown"
            : progress.ProviderId.Trim().ToLowerInvariant();
        ProviderId = id;
        DisplayName = string.IsNullOrWhiteSpace(progress.DisplayName)
            ? ResolveDisplayName(id)
            : progress.DisplayName.Trim();
        IconText = ResolveIconText(id);
        AccentBrush = ResolveAccentBrush(id);
        TotalCount = progress.TotalCount;
        CompletedCount = progress.CompletedCount;
        PendingCount = progress.PendingCount;
        FailedCount = progress.FailedCount;
        ProgressPercent = progress.ProgressPercent;
        CurrentItem = progress.CurrentItem?.Trim() ?? string.Empty;
        StatusText = string.IsNullOrWhiteSpace(progress.StatusText)
            ? "Ожидает"
            : progress.StatusText.Trim();
        CountText = FormatCountText(progress);
    }

    private static string FormatCountText(FluxPackInstallProviderProgress progress)
    {
        ulong unresolved = progress.PendingCount + progress.FailedCount;
        ulong remaining = progress.TotalCount > progress.CompletedCount + unresolved
            ? progress.TotalCount - progress.CompletedCount - unresolved
            : 0;
        string errors = unresolved > 0
            ? $", ошибок {unresolved}"
            : string.Empty;
        return $"{progress.CompletedCount}/{progress.TotalCount} установлено, осталось {remaining}{errors}";
    }

    private static string ResolveDisplayName(string providerId) => providerId switch
    {
        "nexus" => "Nexus Mods",
        "github" => "GitHub",
        "mega" => "MEGA",
        "modernflow" => "ModernFlow",
        "direct" => "Прямая ссылка",
        _ => "Другие источники"
    };

    private static string ResolveIconText(string providerId) => providerId switch
    {
        "nexus" => "N",
        "github" => "GH",
        "mega" => "M",
        "modernflow" => "MF",
        "direct" => "DL",
        _ => "?"
    };

    private static string ResolveAccentBrush(string providerId) => providerId switch
    {
        "nexus" => "#F97316",
        "github" => "#E5E7EB",
        "mega" => "#EF4444",
        "modernflow" => "#38BDF8",
        "direct" => "#22C55E",
        _ => "#A78BFA"
    };

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        return true;
    }
}
