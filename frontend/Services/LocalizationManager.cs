using System.IO;
using System.Text.Json;

namespace Fluxora.App.Services;

public sealed class LocalizationManager
{
    private readonly object syncRoot = new();
    private IReadOnlyDictionary<string, string> translations = new Dictionary<string, string>();

    public static LocalizationManager Current { get; } = new();

    public event EventHandler? LanguageChanged;

    public string CurrentLanguageCode { get; private set; } = "ru";

    public string Text(string fallback)
    {
        if (string.IsNullOrEmpty(fallback))
        {
            return fallback;
        }

        lock (syncRoot)
        {
            return translations.TryGetValue(fallback, out string? localized) &&
                !string.IsNullOrWhiteSpace(localized)
                ? localized
                : fallback;
        }
    }

    public string Format(string fallback, params object?[] args)
    {
        return string.Format(Text(fallback), args);
    }

    public async Task LoadLanguageAsync(Models.LanguageOption language, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        IReadOnlyDictionary<string, string> nextTranslations = string.IsNullOrWhiteSpace(language.FilePath)
            ? new Dictionary<string, string>()
            : await Task.Run(() => ReadTranslations(language.FilePath), cancellationToken);

        lock (syncRoot)
        {
            translations = nextTranslations;
            CurrentLanguageCode = string.IsNullOrWhiteSpace(language.Code) ? "ru" : language.Code;
        }

        LanguageChanged?.Invoke(this, EventArgs.Empty);
    }

    private static IReadOnlyDictionary<string, string> ReadTranslations(string filePath)
    {
        if (!File.Exists(filePath))
        {
            return new Dictionary<string, string>();
        }

        try
        {
            string json = File.ReadAllText(filePath);
            LanguageFile? languageFile = JsonSerializer.Deserialize<LanguageFile>(json, JsonOptions);
            return languageFile?.Translations is null
                ? new Dictionary<string, string>()
                : new Dictionary<string, string>(languageFile.Translations, StringComparer.Ordinal);
        }
        catch (JsonException)
        {
            return new Dictionary<string, string>();
        }
    }

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true
    };

    private sealed class LanguageFile
    {
        public Dictionary<string, string>? Translations { get; set; }
    }
}
