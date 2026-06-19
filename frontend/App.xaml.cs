using System.Windows;
using System.Windows.Threading;
using Fluxora.App.Models;
using Fluxora.App.Services;
using Fluxora.App.ViewModels;
using Fluxora.App.Views;

namespace Fluxora.App;

public partial class App : System.Windows.Application
{
    private static readonly TimeSpan StartupSplashMinimumDuration = TimeSpan.FromMilliseconds(900);

    public static IReadOnlyList<string> StartupNxmLinks { get; private set; } = Array.Empty<string>();

    private ApplicationLogService? applicationLogService;
    private SingleInstanceService? singleInstanceService;
    private CoreBridgeService? coreBridgeService;
    private SettingsService? settingsService;
    private LanguageCatalogService? languageCatalogService;
    private MainWindow? mainWindow;

    protected override async void OnStartup(StartupEventArgs e)
    {
        ShutdownMode = ShutdownMode.OnExplicitShutdown;
        applicationLogService = new ApplicationLogService();
        await applicationLogService.InitializeAsync();
        RegisterLoggingHandlers();
        applicationLogService.Info("App", $"Application starting. args={e.Args.Length}");

        StartupNxmLinks = NxmProtocolService.ExtractNxmLinks(e.Args);
        singleInstanceService = new SingleInstanceService();
        if (!singleInstanceService.TryAcquirePrimaryInstance())
        {
            applicationLogService.Info("App", "Another Fluxora instance is already running. Forwarding activation.");
            singleInstanceService.SendActivationToPrimaryInstance(e.Args);
            singleInstanceService.Dispose();
            singleInstanceService = null;
            Shutdown();
            return;
        }

        singleInstanceService.StartListening(HandleExternalActivation);
        base.OnStartup(e);

        StartupSplashViewModel splashViewModel = new();
        StartupSplashWindow startupSplash = new()
        {
            DataContext = splashViewModel
        };
        startupSplash.Show();

        DateTimeOffset splashShownAt = DateTimeOffset.Now;

        try
        {
            StartupInitializationService startupInitializationService = new(applicationLogService);
            StartupInitializationResult startup = await startupInitializationService.InitializeAsync(splashViewModel);

            coreBridgeService = startup.CoreBridgeService;
            settingsService = startup.SettingsService;
            languageCatalogService = startup.LanguageCatalogService;

            splashViewModel.Report(new StartupProgress(
                "Создаем интерфейс",
                "Подготавливаем главное окно",
                84));

            mainWindow = new MainWindow(coreBridgeService, settingsService, languageCatalogService, applicationLogService);
            MainWindow = mainWindow;

            splashViewModel.Report(new StartupProgress(
                "Загружаем данные",
                "Считываем шаблоны и каталог сборок",
                91));

            await mainWindow.InitializeAsync(StartupNxmLinks);

            splashViewModel.Report(new StartupProgress(
                "Готово",
                "Открываем Fluxora",
                100));
            await HoldStartupSplashAsync(splashShownAt);
        }
        catch (Exception exception)
        {
            applicationLogService.CrashError("Startup", "Application startup failed.", exception);
            startupSplash.Close();
            System.Windows.MessageBox.Show(
                exception.Message,
                "Fluxora",
                MessageBoxButton.OK,
                MessageBoxImage.Error);
            Shutdown();
            return;
        }

        mainWindow.Show();
        ShutdownMode = ShutdownMode.OnMainWindowClose;
        startupSplash.Close();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        applicationLogService?.Info("App", $"Application exiting. exitCode={e.ApplicationExitCode}");
        singleInstanceService?.Dispose();
        applicationLogService?.Dispose();
        base.OnExit(e);
    }

    private void HandleExternalActivation(IReadOnlyList<string> nxmLinks)
    {
        Dispatcher.BeginInvoke(() =>
        {
            mainWindow?.HandleExternalActivation(nxmLinks);
        });
    }

    private void RegisterLoggingHandlers()
    {
        DispatcherUnhandledException += OnDispatcherUnhandledException;
        AppDomain.CurrentDomain.UnhandledException += OnUnhandledException;
        TaskScheduler.UnobservedTaskException += OnUnobservedTaskException;
    }

    private void OnDispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        applicationLogService?.CrashError("WPF", "Unhandled dispatcher exception.", e.Exception);
    }

    private void OnUnhandledException(object sender, UnhandledExceptionEventArgs e)
    {
        if (e.ExceptionObject is Exception exception)
        {
            applicationLogService?.CrashError("AppDomain", "Unhandled application domain exception.", exception);
            return;
        }

        applicationLogService?.CrashError("AppDomain", $"Unhandled application domain exception object: {e.ExceptionObject}");
    }

    private void OnUnobservedTaskException(object? sender, UnobservedTaskExceptionEventArgs e)
    {
        applicationLogService?.CrashError("Tasks", "Unobserved task exception.", e.Exception);
    }

    private static async Task HoldStartupSplashAsync(DateTimeOffset shownAt)
    {
        TimeSpan visibleFor = DateTimeOffset.Now - shownAt;
        if (visibleFor < StartupSplashMinimumDuration)
        {
            await Task.Delay(StartupSplashMinimumDuration - visibleFor);
        }
    }
}
