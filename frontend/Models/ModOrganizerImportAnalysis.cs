namespace Fluxora.App.Models;

public sealed class ModOrganizerImportAnalysis
{
    public string SourceDirectory { get; init; } = string.Empty;
    public string DestinationRootDirectory { get; init; } = string.Empty;
    public string TargetProjectDirectory { get; init; } = string.Empty;
    public string TargetConfigPath { get; init; } = string.Empty;
    public string ProjectName { get; init; } = string.Empty;
    public string ProfileName { get; init; } = string.Empty;
    public string TemplateId { get; init; } = string.Empty;
    public string GameName { get; init; } = string.Empty;
    public string GamePath { get; init; } = string.Empty;
    public ulong TotalBytes { get; init; }
    public ulong AvailableBytes { get; init; }
    public int ModCount { get; init; }
    public int SeparatorCount { get; init; }
    public bool HasEnoughSpace { get; init; }
    public bool WillOverwrite { get; init; }
    public bool CanImport { get; init; }
    public string StatusMessage { get; init; } = string.Empty;
    public string WarningMessage { get; init; } = string.Empty;
}
