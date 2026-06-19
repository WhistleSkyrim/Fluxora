namespace Fluxora.Installer.Services;

public sealed class InstallerText
{
    private static readonly IReadOnlyDictionary<string, IReadOnlyDictionary<string, string>> Catalog =
        new Dictionary<string, IReadOnlyDictionary<string, string>>(StringComparer.OrdinalIgnoreCase)
        {
            ["en"] = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["WindowTitle"] = "Fluxora Setup",
                ["AppName"] = "Fluxora",
                ["AppSubtitle"] = "Mod Manager setup",
                ["StepLanguage"] = "Language",
                ["StepLegal"] = "Legal",
                ["StepTarget"] = "Install",
                ["StepProgress"] = "Progress",
                ["StepResult"] = "Done",
                ["LanguageTitle"] = "Choose installer language",
                ["LanguageBody"] = "The installer text, privacy policy and terms will use this language.",
                ["LegalTitle"] = "Review and accept",
                ["LegalBody"] = "Read the privacy policy and terms of use before installing Fluxora.",
                ["PrivacyTab"] = "Privacy policy",
                ["TermsTab"] = "Terms of use",
                ["AcceptPrivacy"] = "I have read and accept the privacy policy.",
                ["AcceptTerms"] = "I have read and accept the terms of use.",
                ["TargetTitle"] = "Choose installation folder",
                ["TargetBody"] = "Fluxora will be installed into this folder.",
                ["TargetLabel"] = "Installation folder",
                ["Browse"] = "Browse",
                ["Shortcut"] = "Create a desktop shortcut",
                ["TargetHint"] = "Recommended: install into your local user profile so no administrator rights are required.",
                ["Back"] = "Back",
                ["Next"] = "Next",
                ["Install"] = "Install",
                ["Cancel"] = "Cancel",
                ["Close"] = "Close",
                ["ProgressTitle"] = "Installing Fluxora",
                ["ProgressPreparing"] = "Preparing files",
                ["ProgressCopying"] = "Copying application files",
                ["ProgressFinalizing"] = "Creating shortcuts",
                ["ProgressCompleted"] = "Installation complete",
                ["SuccessTitle"] = "Fluxora is ready",
                ["SuccessDetail"] = "Fluxora has been installed.",
                ["ErrorTitle"] = "Installation failed",
                ["Launch"] = "Launch Fluxora",
                ["OpenFolder"] = "Open folder",
                ["MissingNative"] = "Native installer core is missing. Rebuild the installer.",
                ["MissingPath"] = "Choose an installation folder first."
            },
            ["de"] = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["WindowTitle"] = "Fluxora Setup",
                ["AppName"] = "Fluxora",
                ["AppSubtitle"] = "Mod Manager Installation",
                ["StepLanguage"] = "Sprache",
                ["StepLegal"] = "Rechtliches",
                ["StepTarget"] = "Installation",
                ["StepProgress"] = "Fortschritt",
                ["StepResult"] = "Fertig",
                ["LanguageTitle"] = "Sprache des Installers wählen",
                ["LanguageBody"] = "Der Installer, die Datenschutzerklärung und die Nutzungsbedingungen verwenden diese Sprache.",
                ["LegalTitle"] = "Prüfen und akzeptieren",
                ["LegalBody"] = "Bitte lesen Sie die Datenschutzerklärung und die Nutzungsbedingungen vor der Installation.",
                ["PrivacyTab"] = "Datenschutzerklärung",
                ["TermsTab"] = "Nutzungsbedingungen",
                ["AcceptPrivacy"] = "Ich habe die Datenschutzerklärung gelesen und akzeptiere sie.",
                ["AcceptTerms"] = "Ich habe die Nutzungsbedingungen gelesen und akzeptiere sie.",
                ["TargetTitle"] = "Installationsordner wählen",
                ["TargetBody"] = "Fluxora wird in diesen Ordner installiert.",
                ["TargetLabel"] = "Installationsordner",
                ["Browse"] = "Durchsuchen",
                ["Shortcut"] = "Desktop-Verknüpfung erstellen",
                ["TargetHint"] = "Empfohlen: Installation im lokalen Benutzerprofil, damit keine Administratorrechte erforderlich sind.",
                ["Back"] = "Zurück",
                ["Next"] = "Weiter",
                ["Install"] = "Installieren",
                ["Cancel"] = "Abbrechen",
                ["Close"] = "Schließen",
                ["ProgressTitle"] = "Fluxora wird installiert",
                ["ProgressPreparing"] = "Dateien werden vorbereitet",
                ["ProgressCopying"] = "Anwendungsdateien werden kopiert",
                ["ProgressFinalizing"] = "Verknüpfungen werden erstellt",
                ["ProgressCompleted"] = "Installation abgeschlossen",
                ["SuccessTitle"] = "Fluxora ist bereit",
                ["SuccessDetail"] = "Fluxora wurde installiert.",
                ["ErrorTitle"] = "Installation fehlgeschlagen",
                ["Launch"] = "Fluxora starten",
                ["OpenFolder"] = "Ordner öffnen",
                ["MissingNative"] = "Der native Installer-Core fehlt. Erstellen Sie den Installer erneut.",
                ["MissingPath"] = "Bitte wählen Sie zuerst einen Installationsordner."
            },
            ["ru"] = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["WindowTitle"] = "Установка Fluxora",
                ["AppName"] = "Fluxora",
                ["AppSubtitle"] = "Установка Mod Manager",
                ["StepLanguage"] = "Язык",
                ["StepLegal"] = "Документы",
                ["StepTarget"] = "Установка",
                ["StepProgress"] = "Процесс",
                ["StepResult"] = "Готово",
                ["LanguageTitle"] = "Выберите язык установщика",
                ["LanguageBody"] = "На этом языке будут показаны установщик, политика конфиденциальности и условия использования.",
                ["LegalTitle"] = "Проверьте и примите",
                ["LegalBody"] = "Перед установкой Fluxora прочитайте политику конфиденциальности и условия использования.",
                ["PrivacyTab"] = "Политика конфиденциальности",
                ["TermsTab"] = "Условия использования",
                ["AcceptPrivacy"] = "Я прочитал и принимаю политику конфиденциальности.",
                ["AcceptTerms"] = "Я прочитал и принимаю условия использования.",
                ["TargetTitle"] = "Выберите папку установки",
                ["TargetBody"] = "Fluxora будет установлена в выбранную папку.",
                ["TargetLabel"] = "Папка установки",
                ["Browse"] = "Обзор",
                ["Shortcut"] = "Создать ярлык на рабочем столе",
                ["TargetHint"] = "Рекомендуется установка в профиль пользователя, чтобы не требовались права администратора.",
                ["Back"] = "Назад",
                ["Next"] = "Далее",
                ["Install"] = "Установить",
                ["Cancel"] = "Отмена",
                ["Close"] = "Закрыть",
                ["ProgressTitle"] = "Устанавливаем Fluxora",
                ["ProgressPreparing"] = "Подготавливаем файлы",
                ["ProgressCopying"] = "Копируем файлы приложения",
                ["ProgressFinalizing"] = "Создаем ярлыки",
                ["ProgressCompleted"] = "Установка завершена",
                ["SuccessTitle"] = "Fluxora готова",
                ["SuccessDetail"] = "Fluxora установлена.",
                ["ErrorTitle"] = "Установка не удалась",
                ["Launch"] = "Запустить Fluxora",
                ["OpenFolder"] = "Открыть папку",
                ["MissingNative"] = "Не найден нативный installer core. Пересоберите установщик.",
                ["MissingPath"] = "Сначала выберите папку установки."
            }
        };

    private string languageCode = "en";

    public string LanguageCode
    {
        get => languageCode;
        set => languageCode = Catalog.ContainsKey(value) ? value : "en";
    }

    public string this[string key]
    {
        get
        {
            if (Catalog.TryGetValue(languageCode, out IReadOnlyDictionary<string, string>? language) &&
                language.TryGetValue(key, out string? value))
            {
                return value;
            }

            return Catalog["en"].TryGetValue(key, out string? fallback) ? fallback : key;
        }
    }
}
