namespace Fluxora.App.Models;

public sealed class DownloadEntry
{
    public required string Id { get; init; }
    public required string Name { get; init; }
    public string FileName { get; init; } = string.Empty;
    public string LocalPath { get; init; } = string.Empty;
    public string Source { get; init; } = string.Empty;
    public string Status { get; init; } = "Готово";
    public string SizeText { get; init; } = string.Empty;
    public string CreatedAtText { get; init; } = string.Empty;
    public int ProgressPercent { get; init; }
    public string ProgressText { get; init; } = string.Empty;
    public string EtaText { get; init; } = string.Empty;
    public string DownloadSpeedText { get; init; } = string.Empty;
    public bool IsDownloading { get; init; }
    public bool HasKnownProgress { get; init; }
    public bool CanResume { get; init; }
    public bool HasProgressDisplay => IsDownloading || (CanResume && (HasKnownProgress || !string.IsNullOrWhiteSpace(ProgressText)));
    public bool HasProgressHeader => HasProgressDisplay && (HasKnownProgress || HasDownloadSpeedDisplay);
    public bool HasDownloadSpeedDisplay => IsDownloading && !string.IsNullOrWhiteSpace(DownloadSpeedText);
    public string DownloadSpeedDisplayText => HasDownloadSpeedDisplay ? $"Скорость {DownloadSpeedText}" : string.Empty;
    public string ProgressPercentText => HasProgressDisplay && HasKnownProgress ? $"{ProgressPercent}%" : string.Empty;
    public bool HasProgressCaption => HasProgressDisplay && (!string.IsNullOrWhiteSpace(ProgressText) || !string.IsNullOrWhiteSpace(EtaText));
    public bool IsProgressIndeterminate => IsDownloading && !HasKnownProgress;
    public bool CanInstall { get; init; }
    public bool CanDelete { get; init; } = true;
}
