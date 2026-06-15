namespace Fluxora.App.Models;

public sealed class GameExecutableLaunchResult
{
    public string Id { get; init; } = string.Empty;
    public string DisplayName { get; init; } = string.Empty;
    public string ExecutablePath { get; init; } = string.Empty;
    public string Arguments { get; init; } = string.Empty;
    public string WorkingDirectory { get; init; } = string.Empty;
    public string IconPath { get; init; } = string.Empty;
    public string ResolvedExecutablePath { get; init; } = string.Empty;
    public string ResolvedWorkingDirectory { get; init; } = string.Empty;
}
