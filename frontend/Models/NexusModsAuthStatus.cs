namespace Fluxora.App.Models;

public sealed class NexusModsAuthStatus
{
    public bool IsConfigured { get; set; }
    public bool IsLinked { get; set; }
    public string DisplayName { get; set; } = string.Empty;
    public string UserId { get; set; } = string.Empty;
    public string Message { get; set; } = string.Empty;
    public string ClientId { get; set; } = string.Empty;
    public string RedirectUri { get; set; } = string.Empty;
}
