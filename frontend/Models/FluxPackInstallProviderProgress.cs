namespace Fluxora.App.Models;

public sealed class FluxPackInstallProviderProgress
{
    public string ProviderId { get; set; } = string.Empty;
    public string DisplayName { get; set; } = string.Empty;
    public ulong TotalCount { get; set; }
    public ulong CompletedCount { get; set; }
    public ulong PendingCount { get; set; }
    public ulong FailedCount { get; set; }
    public string CurrentItem { get; set; } = string.Empty;
    public string StatusText { get; set; } = string.Empty;
    public int ProgressPercent { get; set; }
}
