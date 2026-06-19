namespace Fluxora.App.Models;

public sealed class GameCapabilities
{
    public int Bits { get; set; }
    public bool SupportsPlugins { get; set; }
    public bool SupportsLoadOrder { get; set; }
    public bool SupportsRootFiles { get; set; }
    public bool SupportsArchives { get; set; }
    public bool SupportsScriptExtender { get; set; }
    public bool SupportsIniProfiles { get; set; }
    public bool SupportsSaveProfiles { get; set; }
    public bool SupportsGameSpecificVfs { get; set; }
    public bool SupportsContentLayoutRules { get; set; }
    public List<string> Enabled { get; set; } = new();
}

public sealed class GameHealthSummary
{
    public string GameId { get; set; } = string.Empty;
    public string DisplayName { get; set; } = string.Empty;
    public string Status { get; set; } = "unknown";
    public string Summary { get; set; } = string.Empty;
    public bool HasBlockers { get; set; }
    public bool AllowsAutomation { get; set; }
    public List<string> MatchedFiles { get; set; } = new();
    public List<string> MissingFiles { get; set; } = new();
    public List<string> Warnings { get; set; } = new();
    public List<GameHealthFinding> Findings { get; set; } = new();
}

public sealed class GameHealthFinding
{
    public string Severity { get; set; } = string.Empty;
    public string Code { get; set; } = string.Empty;
    public string Message { get; set; } = string.Empty;
    public string Path { get; set; } = string.Empty;
    public bool Critical { get; set; }
}

public sealed class ProjectFingerprint
{
    public string Algorithm { get; set; } = string.Empty;
    public string GameId { get; set; } = string.Empty;
    public string GameDisplayName { get; set; } = string.Empty;
    public string GameDefinitionVersion { get; set; } = string.Empty;
    public string DefinitionBundleVersion { get; set; } = string.Empty;
    public string SupportModuleVersion { get; set; } = string.Empty;
    public string SelectedInstallPath { get; set; } = string.Empty;
    public string CanonicalInstallPath { get; set; } = string.Empty;
    public string SelectedExecutable { get; set; } = string.Empty;
    public string DetectedStoreSource { get; set; } = string.Empty;
    public string DetectionSource { get; set; } = string.Empty;
    public string DetectionConfidence { get; set; } = string.Empty;
    public string HealthStatusAtCreation { get; set; } = string.Empty;
    public string GameVersion { get; set; } = string.Empty;
    public string Timestamp { get; set; } = string.Empty;
}

public sealed class ContentLayoutSummary
{
    public bool Supported { get; set; }
    public bool HasWarnings { get; set; }
    public bool HasBlockers { get; set; }
    public string DataFolder { get; set; } = string.Empty;
    public bool SupportsRootFiles { get; set; }
    public string RootFileWrapperDirectory { get; set; } = string.Empty;
    public List<string> PluginExtensions { get; set; } = new();
    public List<string> ArchiveExtensions { get; set; } = new();
    public List<string> ScriptExtenderLoaders { get; set; } = new();
    public List<string> GameDataDirectories { get; set; } = new();
    public List<string> ScriptExtenderDataPaths { get; set; } = new();
    public string Summary { get; set; } = string.Empty;
    public List<string> Details { get; set; } = new();
    public List<string> Warnings { get; set; } = new();
    public List<string> Blockers { get; set; } = new();
}

public sealed class ContentLayoutPreview
{
    public string GameId { get; set; } = string.Empty;
    public string GameDisplayName { get; set; } = string.Empty;
    public string RootFileWrapperDirectory { get; set; } = string.Empty;
    public bool CanInstall { get; set; }
    public ContentLayoutPreviewSummary Summary { get; set; } = new();
    public List<ContentLayoutPreviewEntry> Entries { get; set; } = new();
    public List<ContentLayoutFinding> ValidationFindings { get; set; } = new();
    public string ExplanationSummary { get; set; } = string.Empty;
    public List<string> ExplanationDetails { get; set; } = new();
}

public sealed class ContentLayoutPreviewSummary
{
    public bool Supported { get; set; }
    public bool HasWarnings { get; set; }
    public bool HasBlockers { get; set; }
    public int TotalEntries { get; set; }
    public int PlannedEntries { get; set; }
    public int GameDataEntries { get; set; }
    public int GameRootEntries { get; set; }
    public int PluginEntries { get; set; }
    public int ArchiveEntries { get; set; }
    public int ScriptExtenderEntries { get; set; }
    public int UnknownEntries { get; set; }
    public int UnsafeEntries { get; set; }
}

public sealed class ContentLayoutPreviewEntry
{
    public string SourcePath { get; set; } = string.Empty;
    public string Target { get; set; } = string.Empty;
    public string ContentArea { get; set; } = string.Empty;
    public string TargetRelativePath { get; set; } = string.Empty;
    public string Classification { get; set; } = string.Empty;
    public string Explanation { get; set; } = string.Empty;
    public bool ManualOverrideAllowed { get; set; }
    public List<string> SafeManualTargets { get; set; } = new();
}

public sealed class ContentLayoutFinding
{
    public string Severity { get; set; } = string.Empty;
    public string Path { get; set; } = string.Empty;
    public string Classification { get; set; } = string.Empty;
    public string Message { get; set; } = string.Empty;
    public bool BlocksInstall { get; set; }
}

public sealed class ExecutableDisplayMetadata
{
    public string Id { get; set; } = string.Empty;
    public string DisplayName { get; set; } = string.Empty;
    public string ExecutableName { get; set; } = string.Empty;
    public string Role { get; set; } = string.Empty;
    public string WorkingDirectoryKind { get; set; } = string.Empty;
    public bool IsPrimary { get; set; }
    public bool IsLauncher { get; set; }
    public bool IsScriptExtender { get; set; }
}

public sealed class LaunchTrackingMetadata
{
    public string Kind { get; set; } = "directProcess";
    public List<string> ExpectedChildProcessNames { get; set; } = new();
    public string HandoffDisplayName { get; set; } = string.Empty;
    public int HandoffTimeoutMs { get; set; }
}
