namespace Fluxora.App.Models;

public sealed class FluxPackInstallProgress
{
    public string Phase { get; set; } = string.Empty;
    public string CurrentStep { get; set; } = string.Empty;
    public string CurrentItem { get; set; } = string.Empty;
    public string StatusMessage { get; set; } = string.Empty;
    public int OverallPercent { get; set; }
    public ulong TotalSourceCount { get; set; }
    public ulong InstalledSourceCount { get; set; }
    public ulong PendingSourceCount { get; set; }
    public ulong FailedSourceCount { get; set; }
    public List<FluxPackInstallProviderProgress> Providers { get; set; } = new();
}
