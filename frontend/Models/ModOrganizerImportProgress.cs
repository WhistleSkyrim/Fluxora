namespace Fluxora.App.Models;

public sealed class ModOrganizerImportProgress
{
    public string Phase { get; init; } = string.Empty;
    public string CurrentStep { get; init; } = string.Empty;
    public string CurrentItem { get; init; } = string.Empty;
    public int OverallPercent { get; init; }
    public int CopyPercent { get; init; }
    public int DatabasePercent { get; init; }
    public ulong CopiedBytes { get; init; }
    public ulong TotalBytes { get; init; }
}
