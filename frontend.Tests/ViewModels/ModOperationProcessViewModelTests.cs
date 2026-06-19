using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class ModOperationProcessViewModelTests
{
    [Fact]
    public void Start_ShowsRunningSplashAndBlocksClose()
    {
        ModOperationProcessViewModel viewModel = new();

        viewModel.Start("Установка мода", "Подготовка", "Готовлю архив");

        Assert.True(viewModel.IsVisible);
        Assert.True(viewModel.IsRunning);
        Assert.False(viewModel.CanClose);
        Assert.Equal("Установка мода", viewModel.Title);
        Assert.Equal("Подготовка", viewModel.CurrentStep);
        Assert.Equal("Готовлю архив", viewModel.StatusText);
        Assert.Equal(0, viewModel.OverallPercent);
    }

    [Fact]
    public void ApplyProgress_ClampsAndDoesNotMoveBackward()
    {
        ModOperationProcessViewModel viewModel = new();
        viewModel.Start("Установка мода", "Подготовка", "Готовлю архив");

        viewModel.ApplyProgress("Устанавливаю мод", "Распаковка", 65);
        viewModel.ApplyProgress("Регресс", "Не должен откатить процент", 20);
        viewModel.ApplyProgress("Финал", "Почти готово", 150);

        Assert.Equal("Финал", viewModel.CurrentStep);
        Assert.Equal("Почти готово", viewModel.StatusText);
        Assert.Equal(100, viewModel.OverallPercent);
    }

    [Fact]
    public void Complete_AllowsCloseAndCloseResetsState()
    {
        ModOperationProcessViewModel viewModel = new();
        viewModel.Start("Удаление мода", "Удаляю файлы", "SkyUI");

        viewModel.Complete("Мод удалён", "SkyUI");
        viewModel.Close();

        Assert.False(viewModel.IsVisible);
        Assert.False(viewModel.IsRunning);
        Assert.False(viewModel.IsCompleted);
        Assert.False(viewModel.HasError);
        Assert.Equal(0, viewModel.OverallPercent);
    }

    [Fact]
    public void Fail_ShowsErrorAndAllowsClose()
    {
        ModOperationProcessViewModel viewModel = new();
        viewModel.Start("Установка мода", "Устанавливаю мод", "SkyUI");

        viewModel.Fail("Нет доступа к файлу");

        Assert.True(viewModel.IsVisible);
        Assert.False(viewModel.IsRunning);
        Assert.True(viewModel.HasError);
        Assert.True(viewModel.CanClose);
        Assert.Equal("Ошибка", viewModel.CurrentStep);
        Assert.Equal("Нет доступа к файлу", viewModel.ErrorMessage);
    }
}
