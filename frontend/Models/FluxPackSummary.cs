namespace Fluxora.App.Models;

public sealed class FluxPackSummary
{
    public string OutputPath { get; set; } = string.Empty;
    public string BuildName { get; set; } = string.Empty;
    public int FormatVersion { get; set; }
    public ulong ManifestBytes { get; set; }
    public ulong SourceArchiveCount { get; set; }
    public ulong GeneratedAssetCount { get; set; }
    public ulong CustomPatchCount { get; set; }
    public ulong CustomConfigCount { get; set; }
    public ulong InstallStepCount { get; set; }
    public bool GeneratedAssetsIncluded { get; set; }
    public bool InstallPlanAvailable { get; set; }
}
