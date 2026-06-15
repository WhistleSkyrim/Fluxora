namespace Fluxora.Installer.Models;

public sealed class InstallerResult
{
    public string InstallDirectory { get; set; } = string.Empty;
    public string ApplicationPath { get; set; } = string.Empty;
    public string DesktopShortcutPath { get; set; } = string.Empty;
    public bool CreatedDesktopShortcut { get; set; }
}
