using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;

namespace Fluxora.App.Services;

public enum ApplicationLogLevel
{
    Debug,
    Info,
    Warning,
    Error
}

public enum ApplicationLogChannel
{
    Ui,
    Bridge,
    Operations,
    Crash
}

public sealed class ApplicationLogService : IAppService, IDisposable
{
    private static readonly AsyncLocal<OperationContext?> CurrentOperation = new();
    private static readonly UTF8Encoding Utf8NoBom = new(false);

    private readonly object syncRoot = new();
    private bool initialized;
    private string logDirectory = string.Empty;

    public string LogPath => UiLogPath;
    public string UiLogPath { get; private set; } = string.Empty;
    public string BridgeLogPath { get; private set; } = string.Empty;
    public string OperationsLogPath { get; private set; } = string.Empty;
    public string CrashLogPath { get; private set; } = string.Empty;

    public static string CurrentOperationId => CurrentOperation.Value?.OperationId ?? string.Empty;

    public Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (initialized)
        {
            return Task.CompletedTask;
        }

        logDirectory = ResolveLogDirectory();
        string stamp = DateTime.Now.ToString("yyyyMMdd");
        UiLogPath = Path.Combine(logDirectory, $"fluxora-ui-{stamp}.log");
        BridgeLogPath = Path.Combine(logDirectory, $"fluxora-bridge-{stamp}.log");
        OperationsLogPath = Path.Combine(logDirectory, $"fluxora-operations-{stamp}.log");
        CrashLogPath = Path.Combine(logDirectory, $"fluxora-crash-{stamp}.log");

        initialized = true;
        Info("Logging", $"UI logger initialized. path=\"{UiLogPath}\", bridgePath=\"{BridgeLogPath}\", operationsPath=\"{OperationsLogPath}\", crashPath=\"{CrashLogPath}\"");
        return Task.CompletedTask;
    }

    public OperationLogScope BeginOperation(string name, string details = "")
    {
        string operationId = CreateOperationId();
        OperationContext? previous = CurrentOperation.Value;
        CurrentOperation.Value = new OperationContext(operationId, name, previous);

        string suffix = string.IsNullOrWhiteSpace(details) ? string.Empty : $" {details.Trim()}";
        Info("Operation", $"Operation started. name=\"{name}\"{suffix}");
        return new OperationLogScope(this, previous, operationId, name);
    }

    public void Debug(string category, string message)
    {
        if (IsDebugEnabled())
        {
            Write(ApplicationLogChannel.Ui, ApplicationLogLevel.Debug, category, message);
        }
    }

    public void Info(string category, string message)
    {
        Write(ApplicationLogChannel.Ui, ApplicationLogLevel.Info, category, message);
    }

    public void Warning(string category, string message, Exception? exception = null)
    {
        Write(ApplicationLogChannel.Ui, ApplicationLogLevel.Warning, category, message, exception);
    }

    public void Error(string category, string message, Exception? exception = null)
    {
        Write(ApplicationLogChannel.Ui, ApplicationLogLevel.Error, category, message, exception);
    }

    public void BridgeDebug(string category, string message)
    {
        if (IsDebugEnabled())
        {
            Write(ApplicationLogChannel.Bridge, ApplicationLogLevel.Debug, category, message);
        }
    }

    public void BridgeInfo(string category, string message)
    {
        Write(ApplicationLogChannel.Bridge, ApplicationLogLevel.Info, category, message);
    }

    public void BridgeWarning(string category, string message, Exception? exception = null)
    {
        Write(ApplicationLogChannel.Bridge, ApplicationLogLevel.Warning, category, message, exception);
    }

    public void BridgeError(string category, string message, Exception? exception = null)
    {
        Write(ApplicationLogChannel.Bridge, ApplicationLogLevel.Error, category, message, exception);
    }

    public void OperationInfo(string category, string message)
    {
        Write(ApplicationLogChannel.Operations, ApplicationLogLevel.Info, category, message);
    }

    public void OperationWarning(string category, string message, Exception? exception = null)
    {
        Write(ApplicationLogChannel.Operations, ApplicationLogLevel.Warning, category, message, exception);
    }

    public void OperationError(string category, string message, Exception? exception = null)
    {
        Write(ApplicationLogChannel.Operations, ApplicationLogLevel.Error, category, message, exception);
    }

    public void CrashError(string category, string message, Exception? exception = null)
    {
        Write(ApplicationLogChannel.Crash, ApplicationLogLevel.Error, category, message, exception);
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

    private void Write(
        ApplicationLogChannel channel,
        ApplicationLogLevel level,
        string category,
        string message,
        Exception? exception = null)
    {
        if (!initialized)
        {
            return;
        }

        string safeCategory = string.IsNullOrWhiteSpace(category) ? "App" : category.Trim();
        string operationId = CurrentOperationId;
        string line = $"{DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss.fff zzz} [{FormatLevel(level)}] [{FormatChannel(channel)}] [{safeCategory}] [tid={Environment.CurrentManagedThreadId}]";
        if (!string.IsNullOrWhiteSpace(operationId))
        {
            line += $" [op={operationId}]";
        }

        line += $" {message}";
        if (exception is not null)
        {
            line += Environment.NewLine + exception;
        }

        lock (syncRoot)
        {
            AppendLine(PathForChannel(channel), line);
            if (channel != ApplicationLogChannel.Operations && !string.IsNullOrWhiteSpace(operationId))
            {
                AppendLine(OperationsLogPath, line);
            }
        }

        Trace.WriteLine(line);
    }

    private static void AppendLine(string path, string line)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path) ?? string.Empty);
            using FileStream stream = new(
                path,
                FileMode.Append,
                FileAccess.Write,
                FileShare.ReadWrite,
                4096,
                FileOptions.WriteThrough);
            using StreamWriter writer = new(stream, Utf8NoBom);
            writer.WriteLine(line);
        }
        catch
        {
        }
    }

    private string PathForChannel(ApplicationLogChannel channel)
    {
        return channel switch
        {
            ApplicationLogChannel.Bridge => BridgeLogPath,
            ApplicationLogChannel.Operations => OperationsLogPath,
            ApplicationLogChannel.Crash => CrashLogPath,
            _ => UiLogPath
        };
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

    private static string FormatChannel(ApplicationLogChannel channel)
    {
        return channel switch
        {
            ApplicationLogChannel.Ui => "UI",
            ApplicationLogChannel.Bridge => "Bridge",
            ApplicationLogChannel.Operations => "Operations",
            ApplicationLogChannel.Crash => "Crash",
            _ => "UI"
        };
    }

    private static bool IsDebugEnabled()
    {
        string? value = Environment.GetEnvironmentVariable("FLUXORA_DEBUG_LOGS");
        return string.Equals(value, "1", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(value, "true", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(value, "yes", StringComparison.OrdinalIgnoreCase);
    }

    private static string ResolveLogDirectory()
    {
        foreach (string directory in EnumerateLogDirectories())
        {
            try
            {
                Directory.CreateDirectory(directory);
                string probe = Path.Combine(directory, ".fluxora-log-probe");
                using (FileStream stream = new(probe, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
                {
                }

                File.Delete(probe);
                return directory;
            }
            catch
            {
            }
        }

        string fallback = Path.Combine(Path.GetTempPath(), "Fluxora", "logs");
        Directory.CreateDirectory(fallback);
        return fallback;
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

    private static string CreateOperationId()
    {
        return Guid.NewGuid().ToString("N");
    }

    internal sealed record OperationContext(
        string OperationId,
        string Name,
        OperationContext? Parent);

    public sealed class OperationLogScope : IDisposable
    {
        private readonly ApplicationLogService owner;
        private readonly OperationContext? previous;
        private readonly string name;
        private readonly Stopwatch stopwatch = Stopwatch.StartNew();
        private bool finished;

        internal OperationLogScope(
            ApplicationLogService owner,
            OperationContext? previous,
            string operationId,
            string name)
        {
            this.owner = owner;
            this.previous = previous;
            OperationId = operationId;
            this.name = name;
        }

        public string OperationId { get; }

        public void Complete(string message = "")
        {
            if (finished)
            {
                return;
            }

            finished = true;
            string suffix = string.IsNullOrWhiteSpace(message) ? string.Empty : $" {message.Trim()}";
            owner.OperationInfo("Operation", $"Operation completed. name=\"{name}\", elapsedMs={stopwatch.ElapsedMilliseconds}{suffix}");
        }

        public void Fail(Exception exception, string message = "")
        {
            if (finished)
            {
                return;
            }

            finished = true;
            string suffix = string.IsNullOrWhiteSpace(message) ? string.Empty : $" {message.Trim()}";
            owner.OperationError("Operation", $"Operation failed. name=\"{name}\", elapsedMs={stopwatch.ElapsedMilliseconds}{suffix}", exception);
        }

        public void Dispose()
        {
            if (!finished)
            {
                owner.BridgeDebug("Operation", $"Operation scope disposed without terminal status. name=\"{name}\", elapsedMs={stopwatch.ElapsedMilliseconds}");
            }

            CurrentOperation.Value = previous;
        }
    }
}
