namespace Fluxora.App.Models;

public sealed class BuildSettingsResult
{
    public required BuildPathSettings Paths { get; init; }

    public required IReadOnlyList<GameExecutableEntry> Executables { get; init; }
}
