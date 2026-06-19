using Fluxora.App.Models;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class StartupSplashViewModelTests
{
    [Fact]
    public void Report_UpdatesVisibleTextAndClampsProgress()
    {
        StartupSplashViewModel viewModel = new();
        List<string?> changedProperties = new();
        viewModel.PropertyChanged += (_, args) => changedProperties.Add(args.PropertyName);

        viewModel.Report(new StartupProgress("Почти готово", "Загружаю UI", 125));

        Assert.Equal("Почти готово", viewModel.Title);
        Assert.Equal("Загружаю UI", viewModel.Detail);
        Assert.Equal(100, viewModel.Progress);
        Assert.Equal("100%", viewModel.ProgressText);
        Assert.Contains(nameof(StartupSplashViewModel.Progress), changedProperties);
        Assert.Contains(nameof(StartupSplashViewModel.ProgressText), changedProperties);
    }

    [Fact]
    public void Report_KeepsExistingTextWhenCoreProgressHasNoText()
    {
        StartupSplashViewModel viewModel = new();
        viewModel.Report(new StartupProgress("Готовлю окно", "Проверяю окружение", 30));

        viewModel.Report(new StartupProgress(" ", string.Empty, -12));

        Assert.Equal("Готовлю окно", viewModel.Title);
        Assert.Equal("Проверяю окружение", viewModel.Detail);
        Assert.Equal(0, viewModel.Progress);
        Assert.Equal("0%", viewModel.ProgressText);
    }
}
