using System.IO;

namespace Fluxora.App.Services;

public sealed class StartupIntegrityService
{
    private static readonly string[] RequiredRelativeFiles =
    {
        Path.Combine("lang", "ru.json"),
        Path.Combine("lang", "en.json"),
        Path.Combine("lang", "de.json"),
        "FluxoraCore.dll",
        "FluxoraVfs.dll"
    };

    private readonly ApplicationLogService? logger;

    public StartupIntegrityService(ApplicationLogService? logger = null)
    {
        this.logger = logger;
    }

    public Task<StartupIntegrityReport> InspectAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        string baseDirectory = AppContext.BaseDirectory;
        List<string> missingFiles = new();

        foreach (string relativeFile in RequiredRelativeFiles)
        {
            cancellationToken.ThrowIfCancellationRequested();
            string path = Path.Combine(baseDirectory, relativeFile);
            if (!File.Exists(path))
            {
                missingFiles.Add(relativeFile);
            }
        }

        if (missingFiles.Count > 0)
        {
            logger?.Warning(
                "Startup",
                $"Startup resource check found missing files: {string.Join(", ", missingFiles)}");
        }
        else
        {
            logger?.Info("Startup", "Startup resource check completed.");
        }

        return Task.FromResult(new StartupIntegrityReport(missingFiles));
    }
}

public sealed record StartupIntegrityReport(IReadOnlyList<string> MissingFiles)
{
    public bool IsHealthy => MissingFiles.Count == 0;

    public string Detail => IsHealthy
        ? "Runtime-файлы и языковые пакеты на месте"
        : $"Не найдено файлов: {MissingFiles.Count}. Продолжаем с fallback";
}
