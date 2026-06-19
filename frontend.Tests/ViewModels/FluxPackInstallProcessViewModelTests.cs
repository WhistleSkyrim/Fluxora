using Fluxora.App.Models;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class FluxPackInstallProcessViewModelTests
{
    [Fact]
    public void Start_ShowsRunningSplashAndInitializesPath()
    {
        FluxPackInstallProcessViewModel viewModel = new();

        viewModel.Start(@" C:\Builds\Foundation.fluxpack ");

        Assert.True(viewModel.IsVisible);
        Assert.True(viewModel.IsRunning);
        Assert.False(viewModel.IsCompleted);
        Assert.False(viewModel.HasError);
        Assert.False(viewModel.CanClose);
        Assert.Equal("Foundation", viewModel.BuildName);
        Assert.Equal(@"C:\Builds\Foundation.fluxpack", viewModel.FluxPackPath);
        Assert.Empty(viewModel.Providers);
    }

    [Fact]
    public void ApplyProgress_SyncsProviderBarsAndSummary()
    {
        FluxPackInstallProcessViewModel viewModel = new();
        viewModel.Start(@"C:\Builds\Foundation.fluxpack");

        viewModel.ApplyProgress(new FluxPackInstallProgress
        {
            CurrentStep = "Скачиваем источники",
            CurrentItem = "SkyUI",
            StatusMessage = "Источник: Nexus Mods",
            OverallPercent = 42,
            TotalSourceCount = 6,
            InstalledSourceCount = 1,
            PendingSourceCount = 2,
            FailedSourceCount = 1,
            Providers =
            {
                new FluxPackInstallProviderProgress
                {
                    ProviderId = "nexus",
                    DisplayName = "Nexus Mods",
                    TotalCount = 4,
                    CompletedCount = 1,
                    PendingCount = 1,
                    FailedCount = 1,
                    ProgressPercent = 25,
                    CurrentItem = "SkyUI",
                    StatusText = "Ошибка загрузки"
                }
            }
        });

        Assert.Equal(42, viewModel.OverallPercent);
        Assert.Equal("Скачиваем источники", viewModel.CurrentStep);
        Assert.Equal("Источник: Nexus Mods", viewModel.StatusText);
        Assert.Contains("осталось 2", viewModel.SourceSummaryText);
        Assert.Contains("ошибок 3", viewModel.SourceSummaryText);
        Assert.DoesNotContain("pending", viewModel.SourceSummaryText, StringComparison.OrdinalIgnoreCase);
        FluxPackInstallProviderViewModel provider = Assert.Single(viewModel.Providers);
        Assert.Equal("nexus", provider.ProviderId);
        Assert.Equal("#F97316", provider.AccentBrush);
        Assert.Equal("N", provider.IconText);
        Assert.Equal("1/4 установлено, осталось 1, ошибок 2", provider.CountText);
    }

    [Fact]
    public void Complete_WithWarnings_AllowsCloseAndFormatsResult()
    {
        FluxPackInstallProcessViewModel viewModel = new();
        viewModel.Start(@"C:\Builds\Foundation.fluxpack");

        viewModel.Complete(new FluxPackInstallResult
        {
            BuildName = "Foundation Edition",
            TotalSourceCount = 10,
            InstalledSourceCount = 8,
            PendingSourceCount = 1,
            FailedSourceCount = 1,
            AppliedConfigCount = 3,
            AppliedProfileOrderItemCount = 9,
            HasWarnings = true
        });

        Assert.True(viewModel.IsVisible);
        Assert.False(viewModel.IsRunning);
        Assert.True(viewModel.IsCompleted);
        Assert.True(viewModel.CanClose);
        Assert.Equal(100, viewModel.OverallPercent);
        Assert.Equal("Установка завершена с предупреждениями", viewModel.CurrentStep);
        Assert.Equal("Часть источников не была установлена", viewModel.StatusText);
        Assert.Contains("ошибок 2", viewModel.SourceSummaryText);
        Assert.DoesNotContain("pending", viewModel.SourceSummaryText, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("Конфиги: 3", viewModel.ResultText);
    }

    [Fact]
    public void Fail_ShowsErrorAndCloseResetsState()
    {
        FluxPackInstallProcessViewModel viewModel = new();
        viewModel.Start(@"C:\Builds\Foundation.fluxpack");

        viewModel.Fail("Нет gamePath");

        Assert.True(viewModel.HasError);
        Assert.True(viewModel.CanClose);
        Assert.Equal("Нет gamePath", viewModel.ErrorMessage);

        viewModel.Close();

        Assert.False(viewModel.IsVisible);
        Assert.False(viewModel.HasError);
        Assert.Equal(0, viewModel.OverallPercent);
    }
}
