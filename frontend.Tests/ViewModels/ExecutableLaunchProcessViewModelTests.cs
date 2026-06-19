using Fluxora.App.Models;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class ExecutableLaunchProcessViewModelTests
{
    [Fact]
    public void Start_ForGenericExecutable_UsesProcessNameWithoutGameCopy()
    {
        ExecutableLaunchProcessViewModel viewModel = new();

        viewModel.Start(
            @"C:\Tools\CreationKit.exe",
            "Creation Kit",
            "Foundation Edition",
            usesExpectedChildProcessTracking: false);

        Assert.True(viewModel.IsVisible);
        Assert.True(viewModel.IsStarting);
        Assert.True(viewModel.IsSpinnerActive);
        Assert.False(viewModel.CanClose);
        Assert.Equal("Запускается: CreationKit.exe", viewModel.CurrentStep);
        Assert.Equal("CreationKit.exe", viewModel.StatusText);
        Assert.Equal("Закрыть приложение", viewModel.CloseButtonText);
    }

    [Fact]
    public void Start_ForExpectedChildProcessTracking_ShowsHandoffDisplayName()
    {
        ExecutableLaunchProcessViewModel viewModel = new();

        viewModel.Start(
            @"C:\Games\Example\handoff_loader.exe",
            "Script Loader",
            "Foundation Edition",
            usesExpectedChildProcessTracking: true,
            handoffDisplayName: "Example Game");

        Assert.Equal("Запуск передаётся приложению", viewModel.CurrentStep);
        Assert.Equal("Foundation Edition · Example Game", viewModel.StatusText);
    }

    [Fact]
    public void MarkLaunched_EnablesCloseForTrackedProcess()
    {
        ExecutableLaunchProcessViewModel viewModel = new();
        ExecutableLaunchSession session = new()
        {
            ProcessId = 4242,
            ProcessName = "tool.exe",
            DisplayName = "Tool",
            ResolvedExecutablePath = @"C:\Tools\tool.exe"
        };

        viewModel.MarkLaunched(session, recovered: false);

        Assert.True(viewModel.IsVisible);
        Assert.False(viewModel.IsStarting);
        Assert.True(viewModel.IsProcessRunning);
        Assert.False(viewModel.IsSpinnerActive);
        Assert.True(viewModel.CanClose);
        Assert.Equal("Процесс запущен", viewModel.CurrentStep);
        Assert.Equal("tool.exe · PID 4242", viewModel.StatusText);
    }

    [Fact]
    public void MarkLaunched_ForRecoveredSession_UsesRecoveredTitle()
    {
        ExecutableLaunchProcessViewModel viewModel = new();
        ExecutableLaunchSession session = new()
        {
            ProcessId = 7,
            ProcessName = "handoff_loader.exe",
            DisplayName = "Script Loader",
            ResolvedExecutablePath = @"C:\Games\Example\handoff_loader.exe",
            ProjectName = "Foundation Edition",
            LaunchTrackingKind = "expectedChildProcess",
            HandoffDisplayName = "Example Game",
            ExpectedChildProcessNames = ["ExampleGame.exe"]
        };

        viewModel.MarkLaunched(session, recovered: true);

        Assert.Equal("Активный процесс восстановлен", viewModel.Title);
        Assert.Equal("Foundation Edition · Example Game", viewModel.StatusText);
    }

    [Fact]
    public void MarkWaitingForChildProcess_KeepsSplashRunningForGenericHandoff()
    {
        ExecutableLaunchProcessViewModel viewModel = new();
        ExecutableLaunchSession session = new()
        {
            ProcessId = 7,
            ProcessName = "handoff_loader.exe",
            ResolvedExecutablePath = @"C:\Games\Example\handoff_loader.exe",
            ProjectName = "Foundation Edition",
            LaunchTrackingKind = "expectedChildProcess",
            HandoffDisplayName = "Example Game",
            ExpectedChildProcessNames = ["ExampleGame.exe"]
        };

        viewModel.MarkWaitingForChildProcess(session);

        Assert.True(viewModel.IsVisible);
        Assert.True(viewModel.IsProcessRunning);
        Assert.False(viewModel.IsCompleted);
        Assert.Equal("Процесс запущен", viewModel.CurrentStep);
        Assert.Equal("Foundation Edition · ожидаю Example Game", viewModel.StatusText);
    }

    [Fact]
    public void MarkExited_KeepsSplashClosable()
    {
        ExecutableLaunchProcessViewModel viewModel = new();
        viewModel.MarkLaunched(
            new ExecutableLaunchSession
            {
                ProcessId = 7,
                ProcessName = "tool.exe",
                ResolvedExecutablePath = @"C:\Tools\tool.exe"
            },
            recovered: false);

        viewModel.MarkExited("tool.exe · код выхода 0");

        Assert.True(viewModel.IsVisible);
        Assert.False(viewModel.IsProcessRunning);
        Assert.True(viewModel.IsCompleted);
        Assert.True(viewModel.CanClose);
        Assert.Equal("Процесс закрыт", viewModel.CurrentStep);
        Assert.Equal("tool.exe · код выхода 0", viewModel.StatusText);
    }

    [Fact]
    public void Fail_StopsStartingAndAllowsClose()
    {
        ExecutableLaunchProcessViewModel viewModel = new();
        viewModel.Start("tool.exe", "Tool", string.Empty, usesExpectedChildProcessTracking: false);

        viewModel.Fail("boom");

        Assert.True(viewModel.IsVisible);
        Assert.False(viewModel.IsStarting);
        Assert.True(viewModel.HasError);
        Assert.True(viewModel.CanClose);
        Assert.Equal("Ошибка запуска", viewModel.CurrentStep);
        Assert.Equal("boom", viewModel.ErrorMessage);
    }

    [Fact]
    public void Close_WhileStarting_DoesNotHideSplash()
    {
        ExecutableLaunchProcessViewModel viewModel = new();
        viewModel.Start("tool.exe", "Tool", string.Empty, usesExpectedChildProcessTracking: false);

        viewModel.Close();

        Assert.True(viewModel.IsVisible);
        Assert.True(viewModel.IsStarting);
    }

    [Fact]
    public void ResolveProcessName_FallsBackToDisplayName()
    {
        Assert.Equal(
            "Tool",
            ExecutableLaunchProcessViewModel.ResolveProcessName(string.Empty, "Tool"));
    }

    [Fact]
    public void ResolveLaunchTrackingMetadata_UsesLaunchResultMetadata()
    {
        ModProject project = new()
        {
            Id = "foundation",
            Name = "Foundation Edition",
            GameName = "Example Game",
            GamePath = @"C:\Games\Example",
            InstallRootDirectory = @"C:\Fluxora",
            ProjectDirectory = @"C:\Fluxora\Foundation",
            TemplateId = "example",
            Executables =
            [
                new GameExecutableEntry
                {
                    Id = "handoff",
                    DisplayName = "Script Loader",
                    ExecutablePath = @"C:\Games\Example\handoff_loader.exe"
                }
            ]
        };
        GameExecutableLaunchResult result = new()
        {
            DisplayName = "Script Loader",
            ResolvedExecutablePath = @"C:\Games\Example\handoff_loader.exe",
            LaunchTrackingKind = "expectedChildProcess",
            ExpectedChildProcessNames = ["ExampleGame.exe"],
            HandoffDisplayName = "Example Game",
            HandoffTimeoutMs = 12000
        };

        LaunchTrackingMetadata metadata = MainWindowViewModel.ResolveLaunchTrackingMetadata(
            result,
            project,
            project.Executables[0]);

        Assert.Equal("expectedChildProcess", metadata.Kind);
        Assert.Equal(12000, metadata.HandoffTimeoutMs);
        Assert.Equal("Example Game", metadata.HandoffDisplayName);
        Assert.Contains("ExampleGame.exe", metadata.ExpectedChildProcessNames);
    }

    [Fact]
    public void ResolveLaunchTrackingMetadata_PrefersNestedBridgeMetadata()
    {
        ModProject project = new()
        {
            Id = "foundation",
            Name = "Foundation Edition",
            GameName = "Example Game",
            GamePath = @"C:\Games\Example",
            InstallRootDirectory = @"C:\Fluxora",
            ProjectDirectory = @"C:\Fluxora\Foundation",
            TemplateId = "example",
            Executables =
            [
                new GameExecutableEntry
                {
                    Id = "handoff",
                    DisplayName = "Script Loader",
                    ExecutablePath = @"C:\Games\Example\handoff_loader.exe"
                }
            ]
        };
        GameExecutableLaunchResult result = new()
        {
            DisplayName = "Script Loader",
            ResolvedExecutablePath = @"C:\Games\Example\handoff_loader.exe",
            LaunchTrackingKind = "directProcess",
            ExpectedChildProcessNames = ["LegacyGame.exe"],
            HandoffDisplayName = "Legacy Game",
            HandoffTimeoutMs = 12000,
            LaunchTrackingMetadata = new LaunchTrackingMetadata
            {
                Kind = "expectedChildProcess",
                ExpectedChildProcessNames = ["NestedGame.exe"],
                HandoffDisplayName = "Nested Game",
                HandoffTimeoutMs = 30000
            }
        };

        LaunchTrackingMetadata metadata = MainWindowViewModel.ResolveLaunchTrackingMetadata(
            result,
            project,
            project.Executables[0]);

        Assert.Equal("expectedChildProcess", metadata.Kind);
        Assert.Equal(30000, metadata.HandoffTimeoutMs);
        Assert.Equal("Nested Game", metadata.HandoffDisplayName);
        Assert.Equal(["NestedGame.exe"], metadata.ExpectedChildProcessNames);
    }
}
