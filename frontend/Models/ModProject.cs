namespace Fluxora.App.Models;

public sealed class ModProject
{
    public required string Id { get; init; }
    public required string Name { get; set; }
    public required string GameName { get; init; }
    public required string GamePath { get; set; }
    public required string InstallRootDirectory { get; init; }
    public required string ProjectDirectory { get; init; }
    public string ConfigPath { get; init; } = string.Empty;
    public BuildPathSettings Paths { get; set; } = new();

    public List<GameExecutableEntry> Executables { get; set; } = new();

    /// <summary>Id of the game template this build was created from.</summary>
    public string TemplateId { get; init; } = string.Empty;

    /// <summary>
    /// Resolved (base + game) template that shaped this build. Used by the detail
    /// view to show the build's structure and functional modules.
    /// </summary>
    public ResolvedTemplate? Template { get; init; }

    public DateTimeOffset CreatedAt { get; init; } = DateTimeOffset.Now;

    public void ApplyPathSettings(BuildPathSettings settings)
    {
        Paths = settings.Clone();
        Paths.ApplyFallbacks(ProjectDirectory, GamePath);
        GamePath = Paths.GameDirectory;
    }
}
