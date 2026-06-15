namespace Fluxora.App.Models;

public sealed class BuildDeletionProgress
{
    public string Phase { get; init; } = string.Empty;
    public string CurrentStep { get; init; } = string.Empty;
    public string CurrentItem { get; init; } = string.Empty;
    public int OverallPercent { get; init; }
    public ulong DeletedBytes { get; init; }
    public ulong TotalBytes { get; init; }
    public ulong DeletedEntries { get; init; }
    public ulong TotalEntries { get; init; }
}
