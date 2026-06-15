using Fluxora.App.Models;

namespace Fluxora.App.Services;

public sealed class StartupInitializationService
{
    private readonly ApplicationLogService? logger;
    private readonly StartupIntegrityService integrityService;

    public StartupInitializationService(ApplicationLogService? logger = null)
    {
        this.logger = logger;
        integrityService = new StartupIntegrityService(logger);
    }

    public async Task<StartupInitializationResult> InitializeAsync(
        IProgress<StartupProgress>? progress = null,
        CancellationToken cancellationToken = default)
    {
        Report(progress, "Поднимаем приложение", "Настраиваем локализацию и журнал", 8);
        LocalizationScope.Initialize();

        CoreBridgeService coreBridgeService = new(logger);
        SettingsService settingsService = new();
        LanguageCatalogService languageCatalogService = new(coreBridgeService);

        Report(progress, "Проверяем ресурсы", "Сверяем runtime-файлы и языковые пакеты", 18);
        StartupIntegrityReport integrity = await integrityService.InspectAsync(cancellationToken);
        Report(progress, "Проверяем ресурсы", integrity.Detail, 28);

        Report(progress, "Проверяем C++ core", "Подключаем native-мост", 40);
        await coreBridgeService.InitializeAsync(cancellationToken);
        Report(
            progress,
            "Проверяем C++ core",
            coreBridgeService.IsCoreAvailable
                ? "Native-мост готов"
                : "Native-мост недоступен, UI запустится в fallback",
            52);

        Report(progress, "Готовим папки", "Проверяем рабочие каталоги", 60);
        await settingsService.InitializeAsync(cancellationToken);

        Report(progress, "Загружаем язык", "Читаем локализацию и выбранный язык", 70);
        await languageCatalogService.InitializeAsync(cancellationToken);

        Report(progress, "Основные сервисы готовы", "Переходим к интерфейсу", 78);
        return new StartupInitializationResult(coreBridgeService, settingsService, languageCatalogService);
    }

    private static void Report(
        IProgress<StartupProgress>? progress,
        string title,
        string detail,
        double percent)
    {
        progress?.Report(new StartupProgress(title, detail, percent));
    }
}

public sealed record StartupInitializationResult(
    CoreBridgeService CoreBridgeService,
    SettingsService SettingsService,
    LanguageCatalogService LanguageCatalogService);
