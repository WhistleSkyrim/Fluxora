using System.IO;
using System.Text.Json;
using Fluxora.App.Models;

namespace Fluxora.App.Services;

public sealed class ExecutableLaunchSessionStore
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true
    };

    private readonly ApplicationLogService? logService;

    public ExecutableLaunchSessionStore(
        ApplicationLogService? logService = null,
        string? sessionPath = null)
    {
        this.logService = logService;
        SessionPath = string.IsNullOrWhiteSpace(sessionPath)
            ? Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "Fluxora",
                "launch-session.json")
            : sessionPath;
    }

    public string SessionPath { get; }

    public ExecutableLaunchSession? Load()
    {
        if (!File.Exists(SessionPath))
        {
            return null;
        }

        try
        {
            using FileStream stream = new(SessionPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            ExecutableLaunchSession? session = JsonSerializer.Deserialize<ExecutableLaunchSession>(stream, JsonOptions);
            if (session?.ProcessId > 0)
            {
                return session;
            }
        }
        catch (Exception exception)
        {
            logService?.OperationWarning("Launch", $"Launch session could not be read. path=\"{SessionPath}\"", exception);
        }

        return null;
    }

    public void Save(ExecutableLaunchSession session)
    {
        ArgumentNullException.ThrowIfNull(session);

        string? directory = Path.GetDirectoryName(SessionPath);
        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        string tempPath = SessionPath + ".tmp";
        try
        {
            using (FileStream stream = new(
                tempPath,
                FileMode.Create,
                FileAccess.Write,
                FileShare.Read,
                4096,
                FileOptions.WriteThrough))
            {
                JsonSerializer.Serialize(stream, session, JsonOptions);
                stream.Flush(flushToDisk: true);
            }

            if (File.Exists(SessionPath))
            {
                File.Replace(tempPath, SessionPath, null, ignoreMetadataErrors: true);
            }
            else
            {
                File.Move(tempPath, SessionPath);
            }

            logService?.OperationInfo(
                "Launch",
                $"Launch session saved. pid={session.ProcessId}, process=\"{session.ProcessName}\", path=\"{SessionPath}\"");
        }
        catch (Exception exception)
        {
            TryDelete(tempPath);
            logService?.OperationError("Launch", $"Launch session could not be saved. path=\"{SessionPath}\"", exception);
            throw;
        }
    }

    public void Clear()
    {
        TryDelete(SessionPath);
    }

    private static void TryDelete(string path)
    {
        try
        {
            if (File.Exists(path))
            {
                File.Delete(path);
            }
        }
        catch
        {
        }
    }
}
