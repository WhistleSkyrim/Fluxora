using System.Diagnostics;
using System.IO;

namespace Fluxora.Installer.Services;

public sealed class InstallerLogService : IDisposable
{
    private readonly object syncRoot = new();
    private bool initialized;

    public string LogPath { get; private set; } = string.Empty;

    public void Initialize()
    {
        if (initialized)
        {
            return;
        }

        LogPath = ResolveLogPath();
        initialized = true;
        Info("Installer UI logger initialized.");
    }

    public void Info(string message)
    {
        Write("INFO", message);
    }

    public void Warning(string message, Exception? exception = null)
    {
        Write("WARNING", message, exception);
    }

    public void Error(string message, Exception? exception = null)
    {
        Write("ERROR", message, exception);
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

    private void Write(string level, string message, Exception? exception = null)
    {
        if (!initialized)
        {
            return;
        }

        string line = $"{DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss.fff zzz} [{level}] [InstallerUI] [tid={Environment.CurrentManagedThreadId}] {message}";
        if (exception is not null)
        {
            line += Environment.NewLine + exception;
        }

        lock (syncRoot)
        {
            try
            {
                File.AppendAllText(LogPath, line + Environment.NewLine);
            }
            catch
            {
            }
        }

        Trace.WriteLine(line);
    }

    private static string ResolveLogPath()
    {
        string appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        string directory = string.IsNullOrWhiteSpace(appData)
            ? Path.Combine(Path.GetTempPath(), "Fluxora", "logs")
            : Path.Combine(appData, "Fluxora", "logs");

        Directory.CreateDirectory(directory);
        return Path.Combine(directory, $"fluxora-installer-ui-{DateTime.Now:yyyyMMdd}.log");
    }
}
