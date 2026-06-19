using System.IO;
using Fluxora.App.Models;
using Fluxora.App.Services;

namespace Fluxora.App.Tests.Services;

public sealed class ExecutableLaunchSessionStoreTests
{
    [Fact]
    public void SaveAndLoad_RoundTripsLaunchSession()
    {
        string directory = CreateTempDirectory();
        string path = Path.Combine(directory, "launch-session.json");
        ExecutableLaunchSessionStore store = new(sessionPath: path);
        ExecutableLaunchSession session = new()
        {
            SessionId = "session",
            ProcessId = 1234,
            ProcessName = "tool.exe",
            LauncherProcessId = 1234,
            LauncherProcessName = "tool.exe",
            DisplayName = "Tool",
            ExecutableId = "tool",
            ResolvedExecutablePath = @"C:\Tools\tool.exe",
            ResolvedWorkingDirectory = @"C:\Tools",
            ProjectName = "Foundation Edition",
            ConfigPath = @"C:\Fluxora\build.json",
            LaunchTrackingKind = "expectedChildProcess",
            HandoffDisplayName = "Example Game",
            HandoffTimeoutMs = 12000,
            ExpectedChildProcessNames = ["ExampleGame.exe"],
            StartedAtUtc = new DateTimeOffset(2026, 6, 17, 10, 0, 0, TimeSpan.Zero),
            ProcessStartTimeUtc = new DateTimeOffset(2026, 6, 17, 10, 0, 1, TimeSpan.Zero)
        };

        try
        {
            store.Save(session);

            ExecutableLaunchSession? loaded = store.Load();

            Assert.NotNull(loaded);
            Assert.Equal(session.ProcessId, loaded.ProcessId);
            Assert.Equal(session.ProcessName, loaded.ProcessName);
            Assert.Equal(session.LauncherProcessId, loaded.LauncherProcessId);
            Assert.Equal(session.ProjectName, loaded.ProjectName);
            Assert.Equal(session.LaunchTrackingKind, loaded.LaunchTrackingKind);
            Assert.Equal(session.HandoffDisplayName, loaded.HandoffDisplayName);
            Assert.Equal(session.HandoffTimeoutMs, loaded.HandoffTimeoutMs);
            Assert.Equal(session.ExpectedChildProcessNames, loaded.ExpectedChildProcessNames);
            Assert.Equal(session.ProcessStartTimeUtc, loaded.ProcessStartTimeUtc);
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Load_IgnoresSessionWithoutPid()
    {
        string directory = CreateTempDirectory();
        string path = Path.Combine(directory, "launch-session.json");
        ExecutableLaunchSessionStore store = new(sessionPath: path);

        try
        {
            File.WriteAllText(path, "{\"processId\":0,\"processName\":\"tool.exe\"}");

            Assert.Null(store.Load());
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Load_ReturnsNullForCorruptedJson()
    {
        string directory = CreateTempDirectory();
        string path = Path.Combine(directory, "launch-session.json");
        ExecutableLaunchSessionStore store = new(sessionPath: path);

        try
        {
            File.WriteAllText(path, "{bad json");

            Assert.Null(store.Load());
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    [Fact]
    public void Clear_RemovesSessionFile()
    {
        string directory = CreateTempDirectory();
        string path = Path.Combine(directory, "launch-session.json");
        ExecutableLaunchSessionStore store = new(sessionPath: path);

        try
        {
            File.WriteAllText(path, "{}");

            store.Clear();

            Assert.False(File.Exists(path));
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    private static string CreateTempDirectory()
    {
        string directory = Path.Combine(Path.GetTempPath(), "FluxoraLaunchSessionStore-" + Guid.NewGuid());
        Directory.CreateDirectory(directory);
        return directory;
    }
}
