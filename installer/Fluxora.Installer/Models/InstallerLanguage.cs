namespace Fluxora.Installer.Models;

public sealed record InstallerLanguage(string Code, string NativeName, string EnglishName)
{
    public string DisplayName => NativeName;
}
