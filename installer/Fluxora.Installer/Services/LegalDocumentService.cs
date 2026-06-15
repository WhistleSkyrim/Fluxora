using System.IO;
using System.Reflection;

namespace Fluxora.Installer.Services;

public sealed class LegalDocumentService
{
    public string ReadDocument(string languageCode, string documentName)
    {
        string normalizedLanguage = NormalizeLanguage(languageCode);
        string normalizedDocument = documentName.Equals("terms", StringComparison.OrdinalIgnoreCase)
            ? "terms"
            : "privacy";

        Assembly assembly = Assembly.GetExecutingAssembly();
        string suffix = $".Resources.Legal.{normalizedLanguage}.{normalizedDocument}.txt";
        string? resourceName = assembly
            .GetManifestResourceNames()
            .FirstOrDefault(name => name.EndsWith(suffix, StringComparison.OrdinalIgnoreCase));

        if (resourceName is null && normalizedLanguage != "en")
        {
            return ReadDocument("en", normalizedDocument);
        }

        if (resourceName is null)
        {
            throw new InvalidOperationException("Legal document resource is missing.");
        }

        using Stream? stream = assembly.GetManifestResourceStream(resourceName);
        if (stream is null)
        {
            throw new InvalidOperationException("Legal document resource could not be opened.");
        }

        using StreamReader reader = new(stream);
        return reader.ReadToEnd();
    }

    private static string NormalizeLanguage(string languageCode)
    {
        return languageCode.Equals("de", StringComparison.OrdinalIgnoreCase)
            ? "de"
            : languageCode.Equals("ru", StringComparison.OrdinalIgnoreCase)
                ? "ru"
                : "en";
    }
}
