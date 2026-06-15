namespace Fluxora.App.Models;

public sealed class LanguageOption
{
    public string Code { get; init; } = string.Empty;

    public string Name { get; init; } = string.Empty;

    public string NativeName { get; init; } = string.Empty;

    public string FilePath { get; init; } = string.Empty;

    public bool IsSelectable => true;

    public string DisplayName
    {
        get
        {
            if (string.IsNullOrWhiteSpace(NativeName))
            {
                return string.IsNullOrWhiteSpace(Name) ? Code : Name;
            }

            if (string.IsNullOrWhiteSpace(Name) ||
                string.Equals(NativeName, Name, StringComparison.OrdinalIgnoreCase))
            {
                return NativeName;
            }

            return $"{NativeName} / {Name}";
        }
    }

    public override string ToString() => DisplayName;
}
