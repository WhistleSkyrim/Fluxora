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

    public string UiTemplateId { get; set; } = string.Empty;
    public GameCapabilities GameCapabilities { get; set; } = new();
    public GameHealthSummary GameHealthSummary { get; set; } = new();
    public ProjectFingerprint? ProjectFingerprint { get; set; }
    public ContentLayoutSummary ContentLayoutSummary { get; set; } = new();

    /// <summary>
    /// Resolved (base + game) template that shaped this build. Used by the detail
    /// view to show the build's structure and functional modules.
    /// </summary>
    public ResolvedTemplate? Template { get; init; }

    public DateTimeOffset CreatedAt { get; init; } = DateTimeOffset.Now;

    public DateTimeOffset? LastLaunchedAt { get; set; }

    public void ApplyPathSettings(BuildPathSettings settings)
    {
        Paths = settings.Clone();
        Paths.ApplyFallbacks(ProjectDirectory, GamePath);
        GamePath = Paths.GameDirectory;
    }
}
