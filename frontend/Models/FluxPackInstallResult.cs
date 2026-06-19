namespace Fluxora.App.Models;

public sealed class FluxPackInstallResult
{
    public FluxPackSummary Summary { get; set; } = new();
    public string ConfigPath { get; set; } = string.Empty;
    public string ProjectDirectory { get; set; } = string.Empty;
    public string BuildName { get; set; } = string.Empty;
    public ulong TotalSourceCount { get; set; }
    public ulong InstalledSourceCount { get; set; }
    public ulong PendingSourceCount { get; set; }
    public ulong FailedSourceCount { get; set; }
    public ulong AppliedConfigCount { get; set; }
    public ulong AppliedProfileOrderItemCount { get; set; }
    public bool HasWarnings { get; set; }
}
