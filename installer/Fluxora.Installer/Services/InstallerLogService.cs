using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;

namespace Fluxora.Installer.Services;

public enum InstallerLogChannel
{
    Ui,
    Bridge,
    Operations,
    Crash
}

public sealed class InstallerLogService : IDisposable
{
    private static readonly AsyncLocal<OperationContext?> CurrentOperation = new();
    private static readonly UTF8Encoding Utf8NoBom = new(false);

    private readonly object syncRoot = new();
    private bool initialized;

    public string LogPath => UiLogPath;
    public string UiLogPath { get; private set; } = string.Empty;
    public string BridgeLogPath { get; private set; } = string.Empty;
    public string OperationsLogPath { get; private set; } = string.Empty;
    public string CrashLogPath { get; private set; } = string.Empty;

    public static string CurrentOperationId => CurrentOperation.Value?.OperationId ?? string.Empty;

    public void Initialize()
    {
        if (initialized)
        {
            return;
        }

        string directory = ResolveLogDirectory();
        string stamp = DateTime.Now.ToString("yyyyMMdd");
        UiLogPath = Path.Combine(directory, $"fluxora-installer-ui-{stamp}.log");
        BridgeLogPath = Path.Combine(directory, $"fluxora-installer-bridge-{stamp}.log");
        OperationsLogPath = Path.Combine(directory, $"fluxora-installer-operations-{stamp}.log");
        CrashLogPath = Path.Combine(directory, $"fluxora-installer-crash-{stamp}.log");

        initialized = true;
        Info($"Installer UI logger initialized. path=\"{UiLogPath}\", bridgePath=\"{BridgeLogPath}\", operationsPath=\"{OperationsLogPath}\", crashPath=\"{CrashLogPath}\"");
    }

    public OperationScope BeginOperation(string name, string details = "")
    {
        string operationId = Guid.NewGuid().ToString("N");
        OperationContext? previous = CurrentOperation.Value;
        CurrentOperation.Value = new OperationContext(operationId, name, previous);
        string suffix = string.IsNullOrWhiteSpace(details) ? string.Empty : $" {details.Trim()}";
        Info($"Operation started. name=\"{name}\"{suffix}");
        return new OperationScope(this, previous, operationId, name);
    }

    public void Info(string message)
    {
        Write(InstallerLogChannel.Ui, "INFO", "InstallerUI", message);
    }

    public void Warning(string message, Exception? exception = null)
    {
        Write(InstallerLogChannel.Ui, "WARNING", "InstallerUI", message, exception);
    }

    public void Error(string message, Exception? exception = null)
    {
        Write(InstallerLogChannel.Ui, "ERROR", "InstallerUI", message, exception);
    }

    public void BridgeInfo(string message)
    {
        Write(InstallerLogChannel.Bridge, "INFO", "InstallerBridge", message);
    }

    public void BridgeWarning(string message, Exception? exception = null)
    {
        Write(InstallerLogChannel.Bridge, "WARNING", "InstallerBridge", message, exception);
    }

    public void BridgeError(string message, Exception? exception = null)
    {
        Write(InstallerLogChannel.Bridge, "ERROR", "InstallerBridge", message, exception);
    }

    public void OperationInfo(string message)
    {
        Write(InstallerLogChannel.Operations, "INFO", "InstallerOperation", message);
    }

    public void OperationError(string message, Exception? exception = null)
    {
        Write(InstallerLogChannel.Operations, "ERROR", "InstallerOperation", message, exception);
    }

    public void CrashError(string message, Exception? exception = null)
    {
        Write(InstallerLogChannel.Crash, "ERROR", "InstallerCrash", message, exception);
    }

    public void Dispose()
    {
        if (!initialized)
        {
            return;
        }

        Info("Installer UI logger shut down.");
        initialized = false;
    }

    private void Write(
        InstallerLogChannel channel,
        string level,
        string category,
        string message,
        Exception? exception = null)
    {
        if (!initialized)
        {
            return;
        }

        string operationId = CurrentOperationId;
        string line = $"{DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss.fff zzz} [{level}] [{FormatChannel(channel)}] [{category}] [tid={Environment.CurrentManagedThreadId}]";
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
            if (channel != InstallerLogChannel.Operations && !string.IsNullOrWhiteSpace(operationId))
            {
                AppendLine(OperationsLogPath, line);
            }
        }

        Trace.WriteLine(line);
    }

    private string PathForChannel(InstallerLogChannel channel)
    {
        return channel switch
        {
            InstallerLogChannel.Bridge => BridgeLogPath,
            InstallerLogChannel.Operations => OperationsLogPath,
            InstallerLogChannel.Crash => CrashLogPath,
            _ => UiLogPath
        };
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

    private static string FormatChannel(InstallerLogChannel channel)
    {
        return channel switch
        {
            InstallerLogChannel.Bridge => "Bridge",
            InstallerLogChannel.Operations => "Operations",
            InstallerLogChannel.Crash => "Crash",
            _ => "UI"
        };
    }

    private static string ResolveLogDirectory()
    {
        string appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        string directory = string.IsNullOrWhiteSpace(appData)
            ? Path.Combine(Path.GetTempPath(), "Fluxora", "logs")
            : Path.Combine(appData, "Fluxora", "logs");

        Directory.CreateDirectory(directory);
        return directory;
    }

    internal sealed record OperationContext(string OperationId, string Name, OperationContext? Parent);

    public sealed class OperationScope : IDisposable
    {
        private readonly InstallerLogService owner;
        private readonly OperationContext? previous;
        private readonly string name;
        private readonly Stopwatch stopwatch = Stopwatch.StartNew();
        private bool finished;

        internal OperationScope(
            InstallerLogService owner,
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
            owner.OperationInfo($"Operation completed. name=\"{name}\", elapsedMs={stopwatch.ElapsedMilliseconds}{suffix}");
        }

        public void Fail(Exception exception, string message = "")
        {
            if (finished)
            {
                return;
            }

            finished = true;
            string suffix = string.IsNullOrWhiteSpace(message) ? string.Empty : $" {message.Trim()}";
            owner.OperationError($"Operation failed. name=\"{name}\", elapsedMs={stopwatch.ElapsedMilliseconds}{suffix}", exception);
        }

        public void Dispose()
        {
            CurrentOperation.Value = previous;
        }
    }
}
