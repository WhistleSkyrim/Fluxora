namespace Fluxora.App.Models;

public sealed class ExecutableLaunchSession
{
    public const int DefaultHandoffTimeoutMs = 30000;

    public string SessionId { get; init; } = Guid.NewGuid().ToString("N");
    public int ProcessId { get; init; }
    public string ProcessName { get; init; } = string.Empty;
    public int LauncherProcessId { get; init; }
    public string LauncherProcessName { get; init; } = string.Empty;
    public string DisplayName { get; init; } = string.Empty;
    public string ExecutableId { get; init; } = string.Empty;
    public string ResolvedExecutablePath { get; init; } = string.Empty;
    public string ResolvedWorkingDirectory { get; init; } = string.Empty;
    public string ProjectName { get; init; } = string.Empty;
    public string ConfigPath { get; init; } = string.Empty;
    public string LaunchTrackingKind { get; init; } = "directProcess";
    public string HandoffDisplayName { get; init; } = string.Empty;
    public int HandoffTimeoutMs { get; init; } = DefaultHandoffTimeoutMs;
    public List<string> ExpectedChildProcessNames { get; init; } = new();
    public DateTimeOffset StartedAtUtc { get; init; } = DateTimeOffset.UtcNow;
    public DateTimeOffset ProcessStartTimeUtc { get; init; }

    public bool TracksExpectedChildProcess =>
        string.Equals(LaunchTrackingKind, "expectedChildProcess", StringComparison.OrdinalIgnoreCase);
}
