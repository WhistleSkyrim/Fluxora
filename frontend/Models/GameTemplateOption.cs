namespace Fluxora.App.Models;

/// <summary>
/// A game template the user can layer on top of the base template when creating
/// a build. The data originates entirely from the C++ core; this is a transport
/// model for the UI.
/// </summary>
public sealed class GameTemplateOption
{
    public string Id { get; set; } = string.Empty;
    public string DisplayName { get; set; } = string.Empty;

    // Deprecated bridge compatibility fields kept until the frontend finishes
    // consuming UiTemplateId/GameCapabilities directly.
    public string GameName { get; set; } = string.Empty;
    public string Summary { get; set; } = string.Empty;

    public string UiTemplateId { get; set; } = string.Empty;
    public GameCapabilities GameCapabilities { get; set; } = new();
    public List<string> ArchiveExtensions { get; set; } = new();
    public List<string> RequiredFiles { get; set; } = new();

    public override string ToString() => DisplayName;
}
