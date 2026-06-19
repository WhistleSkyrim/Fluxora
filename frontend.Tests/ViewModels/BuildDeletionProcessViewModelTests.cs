using Fluxora.App.Models;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class BuildDeletionProcessViewModelTests
{
    [Fact]
    public void Start_ShowsRunningProcessAndPreventsClose()
    {
        BuildDeletionProcessViewModel viewModel = new();

        viewModel.Start();
        viewModel.Close();

        Assert.True(viewModel.IsVisible);
        Assert.True(viewModel.IsRunning);
        Assert.False(viewModel.CanClose);
        Assert.False(viewModel.IsCompleted);
        Assert.False(viewModel.HasError);
        Assert.Equal(0, viewModel.OverallPercent);
    }

    [Fact]
    public void ApplyProgress_IgnoresHiddenAndTerminalStates()
    {
        BuildDeletionProcessViewModel viewModel = new();

        viewModel.ApplyProgress(new BuildDeletionProgress { OverallPercent = 70, CurrentStep = "Удаляю папку" });
        Assert.Equal(0, viewModel.OverallPercent);

        viewModel.Start();
        viewModel.Complete();
        viewModel.ApplyProgress(new BuildDeletionProgress { OverallPercent = 20, CurrentStep = "Удаляю папку" });

        Assert.Equal(100, viewModel.OverallPercent);
        Assert.Equal("Сборка удалена", viewModel.CurrentStep);
    }

    [Fact]
    public void ApplyProgress_ProjectsCoreProgressIntoUiText()
    {
        BuildDeletionProcessViewModel viewModel = new();
        viewModel.Start();

        viewModel.ApplyProgress(new BuildDeletionProgress
        {
            CurrentStep = "Удаляю папку сборки",
            OverallPercent = 45,
            DeletedBytes = 1024,
            TotalBytes = 2048,
            DeletedEntries = 2,
            TotalEntries = 5
        });

        Assert.Equal("Удаляю файлы сборки", viewModel.CurrentStep);
        Assert.Equal(45, viewModel.OverallPercent);
        Assert.Equal("1 КБ из 2 КБ", viewModel.BytesText);
        Assert.Equal("2 / 5 элементов", viewModel.EntriesText);
        Assert.Equal("Удалено 1 КБ", viewModel.StatusText);

        viewModel.ApplyProgress(new BuildDeletionProgress { CurrentStep = "Регресс", OverallPercent = 5 });

        Assert.Equal(45, viewModel.OverallPercent);
    }

    [Fact]
    public void Complete_AllowsCloseAndResetsTerminalStateOnClose()
    {
        BuildDeletionProcessViewModel viewModel = new();
        viewModel.Start();

        viewModel.Complete();

        Assert.True(viewModel.CanClose);
        Assert.True(viewModel.IsCompleted);
        Assert.False(viewModel.IsRunning);
        Assert.Equal(100, viewModel.OverallPercent);

        viewModel.Close();

        Assert.False(viewModel.IsVisible);
        Assert.False(viewModel.IsCompleted);
        Assert.False(viewModel.HasError);
    }

    [Theory]
    [InlineData("access is denied", "Нет доступа к одному из файлов сборки.")]
    [InlineData("locked by another process", "Один из файлов занят другим процессом.")]
    [InlineData("some unexpected native failure", "Удаление остановлено.")]
    public void Fail_LocalizesNativeBridgeErrors(string bridgeError, string expectedPrefix)
    {
        BuildDeletionProcessViewModel viewModel = new();
        viewModel.Start();

        viewModel.Fail(bridgeError);

        Assert.True(viewModel.HasError);
        Assert.False(viewModel.IsRunning);
        Assert.StartsWith(expectedPrefix, viewModel.ErrorMessage);
    }
}
