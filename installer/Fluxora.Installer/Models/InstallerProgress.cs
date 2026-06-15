namespace Fluxora.Installer.Models;

public sealed class InstallerProgress
{
    public string Phase { get; set; } = string.Empty;
    public string CurrentItem { get; set; } = string.Empty;
    public double Percent { get; set; }
    public long CopiedBytes { get; set; }
    public long TotalBytes { get; set; }
}
