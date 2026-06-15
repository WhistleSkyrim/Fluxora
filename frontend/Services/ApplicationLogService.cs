using System.Diagnostics;
using System.IO;

namespace Fluxora.App.Services;

public enum ApplicationLogLevel
{
    Debug,
    Info,
    Warning,
    Error
}

public sealed class ApplicationLogService : IAppService, IDisposable
{
    private readonly object syncRoot = new();
    private bool initialized;
    private string logPath = string.Empty;

    public string LogPath => logPath;

    public Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (initialized)
        {
            return Task.CompletedTask;
        }

        logPath = ResolveLogPath();
        initialized = true;
        Info("Logging", $"UI logger initialized. path=\"{logPath}\"");
        return Task.CompletedTask;
    }

    public void Debug(string category, string message)
    {
        if (IsDebugEnabled())
        {
            Write(ApplicationLogLevel.Debug, category, message);
        }
    }

    public void Info(string category, string message)
    {
        Write(ApplicationLogLevel.Info, category, message);
    }

    public void Warning(string category, string message, Exception? exception = null)
    {
        Write(ApplicationLogLevel.Warning, category, message, exception);
    }

    public void Error(string category, string message, Exception? exception = null)
    {
        Write(ApplicationLogLevel.Error, category, message, exception);
    }

    public void Dispose()
    {
        if (!initialized)
        {
            return;
        }

        Info("Logging", "UI logger shut down.");
        initialized = false;
    }

    private void Write(ApplicationLogLevel level, string category, string message, Exception? exception = null)
    {
        if (!initialized)
        {
            return;
        }

        string safeCategory = string.IsNullOrWhiteSpace(category) ? "App" : category.Trim();
        string line = $"{DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss.fff zzz} [{FormatLevel(level)}] [{safeCategory}] [tid={Environment.CurrentManagedThreadId}] {message}";
        if (exception is not null)
        {
            line += Environment.NewLine + FormatException(exception);
        }

        lock (syncRoot)
        {
            try
            {
                File.AppendAllText(logPath, line + Environment.NewLine);
            }
            catch
            {
            }
        }

        Trace.WriteLine(line);
    }

    private static string FormatLevel(ApplicationLogLevel level)
    {
        return level switch
        {
            ApplicationLogLevel.Debug => "DEBUG",
            ApplicationLogLevel.Info => "INFO",
            ApplicationLogLevel.Warning => "WARNING",
            ApplicationLogLevel.Error => "ERROR",
            _ => "UNKNOWN"
        };
    }

    private static string FormatException(Exception exception)
    {
        return exception.ToString();
    }

    private static bool IsDebugEnabled()
    {
        string? value = Environment.GetEnvironmentVariable("FLUXORA_DEBUG_LOGS");
        return string.Equals(value, "1", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(value, "true", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(value, "yes", StringComparison.OrdinalIgnoreCase);
    }

    private static string ResolveLogPath()
    {
        foreach (string directory in EnumerateLogDirectories())
        {
            try
            {
                Directory.CreateDirectory(directory);
                string candidate = Path.Combine(directory, $"fluxora-app-{DateTime.Now:yyyyMMdd}.log");
                using FileStream stream = new(candidate, FileMode.Append, FileAccess.Write, FileShare.ReadWrite);
                return candidate;
            }
            catch
            {
            }
        }

        string fallback = Path.Combine(Path.GetTempPath(), "Fluxora", "logs");
        Directory.CreateDirectory(fallback);
        return Path.Combine(fallback, $"fluxora-app-{DateTime.Now:yyyyMMdd}.log");
    }

    private static IEnumerable<string> EnumerateLogDirectories()
    {
        yield return Path.Combine(AppContext.BaseDirectory, "logs");

        string appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        if (!string.IsNullOrWhiteSpace(appData))
        {
            yield return Path.Combine(appData, "Fluxora", "logs");
        }
    }
}
