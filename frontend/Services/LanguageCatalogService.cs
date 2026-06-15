using System.Globalization;
using System.IO;
using System.Text.Json;
using Fluxora.App.Models;

namespace Fluxora.App.Services;

public sealed class LanguageCatalogService : IAppService
{
    private readonly CoreBridgeService coreBridgeService;
    private readonly List<LanguageOption> availableLanguages = new();

    public LanguageCatalogService(CoreBridgeService coreBridgeService)
    {
        this.coreBridgeService = coreBridgeService;
        LanguageDirectory = Path.Combine(AppContext.BaseDirectory, "lang");
    }

    public string LanguageDirectory { get; }

    public IReadOnlyList<LanguageOption> AvailableLanguages => availableLanguages;

    public LanguageOption? CurrentLanguage { get; private set; }

    public async Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        Directory.CreateDirectory(LanguageDirectory);
        RefreshAvailableLanguages();

        string preferredLanguage = coreBridgeService.GetAppLanguageCode();
        if (string.IsNullOrWhiteSpace(preferredLanguage))
        {
            preferredLanguage = CultureInfo.CurrentUICulture.Name;
        }

        LanguageOption language = ResolveLanguage(preferredLanguage);
        CurrentLanguage = language;
        await LocalizationManager.Current.LoadLanguageAsync(language, cancellationToken);
        await coreBridgeService.SaveAppLanguageCodeAsync(language.Code, cancellationToken);
    }

    public async Task ChangeLanguageAsync(LanguageOption language, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (CurrentLanguage is not null &&
            string.Equals(CurrentLanguage.Code, language.Code, StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        await LocalizationManager.Current.LoadLanguageAsync(language, cancellationToken);
        await coreBridgeService.SaveAppLanguageCodeAsync(language.Code, cancellationToken);
        CurrentLanguage = language;
    }

    public void RefreshAvailableLanguages()
    {
        availableLanguages.Clear();

        foreach (string filePath in Directory.EnumerateFiles(LanguageDirectory, "*.json").OrderBy(path => path))
        {
            LanguageOption option = ReadLanguageOption(filePath);
            if (!string.IsNullOrWhiteSpace(option.Code))
            {
                availableLanguages.Add(option);
            }
        }

        if (availableLanguages.Count == 0)
        {
            availableLanguages.Add(new LanguageOption
            {
                Code = "ru",
                Name = "Russian",
                NativeName = "Русский",
                FilePath = string.Empty
            });
        }
    }

    public LanguageOption ResolveLanguage(string languageCode)
    {
        string normalized = NormalizeCode(languageCode);
        LanguageOption? exact = availableLanguages.FirstOrDefault(language =>
            string.Equals(NormalizeCode(language.Code), normalized, StringComparison.OrdinalIgnoreCase));
        if (exact is not null)
        {
            return exact;
        }

        string neutral = GetNeutralCode(normalized);
        LanguageOption? neutralMatch = availableLanguages.FirstOrDefault(language =>
            string.Equals(GetNeutralCode(language.Code), neutral, StringComparison.OrdinalIgnoreCase));
        if (neutralMatch is not null)
        {
            return neutralMatch;
        }

        return availableLanguages.FirstOrDefault(language =>
                string.Equals(GetNeutralCode(language.Code), "en", StringComparison.OrdinalIgnoreCase)) ??
            availableLanguages.FirstOrDefault(language =>
                string.Equals(GetNeutralCode(language.Code), "ru", StringComparison.OrdinalIgnoreCase)) ??
            availableLanguages[0];
    }

    private static LanguageOption ReadLanguageOption(string filePath)
    {
        LanguageFile? languageFile = null;
        try
        {
            languageFile = JsonSerializer.Deserialize<LanguageFile>(File.ReadAllText(filePath), JsonOptions);
        }
        catch (JsonException)
        {
        }

        string code = string.IsNullOrWhiteSpace(languageFile?.Code)
            ? Path.GetFileNameWithoutExtension(filePath)
            : languageFile.Code;

        return new LanguageOption
        {
            Code = NormalizeCode(code),
            Name = string.IsNullOrWhiteSpace(languageFile?.Name) ? code : languageFile.Name,
            NativeName = string.IsNullOrWhiteSpace(languageFile?.NativeName)
                ? string.IsNullOrWhiteSpace(languageFile?.Name) ? code : languageFile.Name
                : languageFile.NativeName,
            FilePath = filePath
        };
    }

    private static string NormalizeCode(string languageCode)
    {
        return string.IsNullOrWhiteSpace(languageCode)
            ? "en"
            : languageCode.Trim().Replace('_', '-').ToLowerInvariant();
    }

    private static string GetNeutralCode(string languageCode)
    {
        string normalized = NormalizeCode(languageCode);
        int separator = normalized.IndexOf('-');
        return separator <= 0 ? normalized : normalized[..separator];
    }

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true
    };

    private sealed class LanguageFile
    {
        public string Code { get; set; } = string.Empty;

        public string Name { get; set; } = string.Empty;

        public string NativeName { get; set; } = string.Empty;
    }
}
