using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class BuildCreationProcessViewModelTests
{
    [Fact]
    public void Start_ShowsRunningProcessAndInitializesProjectDetails()
    {
        BuildCreationProcessViewModel viewModel = new();

        viewModel.Start(" Foundation Edition ", " Skyrim Template ", @" C:\Builds\Foundation ");
        viewModel.Close();

        Assert.True(viewModel.IsVisible);
        Assert.True(viewModel.IsRunning);
        Assert.False(viewModel.IsCompleted);
        Assert.False(viewModel.HasError);
        Assert.False(viewModel.CanClose);
        Assert.True(viewModel.CanCancel);
        Assert.False(viewModel.IsCancellationRequested);
        Assert.Equal(0, viewModel.OverallPercent);
        Assert.Equal("Foundation Edition", viewModel.ProjectName);
        Assert.Equal("Skyrim Template", viewModel.TemplateName);
        Assert.Equal(@"C:\Builds\Foundation", viewModel.TargetDirectory);
        Assert.Equal(BuildCreationProcessViewModel.ActiveStepState, viewModel.DataStepState);
        Assert.Equal(BuildCreationProcessViewModel.PendingStepState, viewModel.TemplateStepState);
        Assert.Equal(BuildCreationProcessViewModel.PendingStepState, viewModel.FilesStepState);
        Assert.Equal(BuildCreationProcessViewModel.PendingStepState, viewModel.CatalogStepState);
    }

    [Fact]
    public void SetStage_AdvancesStepsAndKeepsProgressAndStateMonotonic()
    {
        BuildCreationProcessViewModel viewModel = new();
        viewModel.Start("Foundation Edition", "Skyrim Template", @"C:\Builds\Foundation");

        viewModel.SetStage(BuildCreationProcessStage.Template, 25, "Применяю шаблон", "Копирую основу");
        viewModel.SetStage(BuildCreationProcessStage.Files, 65, "Создаю файлы", "Записываю структуру");
        viewModel.SetStage(BuildCreationProcessStage.Template, 10, "Регресс", "Не должен откатить шаг");

        Assert.Equal(65, viewModel.OverallPercent);
        Assert.Equal("Создаю файлы", viewModel.CurrentStep);
        Assert.Equal("Записываю структуру", viewModel.StatusText);
        Assert.Equal(BuildCreationProcessViewModel.CompleteStepState, viewModel.DataStepState);
        Assert.Equal(BuildCreationProcessViewModel.CompleteStepState, viewModel.TemplateStepState);
        Assert.Equal(BuildCreationProcessViewModel.ActiveStepState, viewModel.FilesStepState);
        Assert.Equal(BuildCreationProcessViewModel.PendingStepState, viewModel.CatalogStepState);

        viewModel.SetStage(BuildCreationProcessStage.Catalog, 90);

        Assert.Equal("Каталог сборок", viewModel.CurrentStep);
        Assert.Equal(BuildCreationProcessViewModel.CompleteStepState, viewModel.FilesStepState);
        Assert.Equal(BuildCreationProcessViewModel.ActiveStepState, viewModel.CatalogStepState);
    }

    [Fact]
    public void RequestCancel_MarksActiveStepCancelingAndDisablesCancel()
    {
        BuildCreationProcessViewModel viewModel = new();
        viewModel.Start("Foundation Edition", "Skyrim Template", @"C:\Builds\Foundation");
        viewModel.SetStage(BuildCreationProcessStage.Files, 55, "Создаю файлы", "Записываю структуру");

        viewModel.RequestCancel();

        Assert.True(viewModel.IsVisible);
        Assert.True(viewModel.IsRunning);
        Assert.True(viewModel.IsCancellationRequested);
        Assert.False(viewModel.CanCancel);
        Assert.False(viewModel.CanClose);
        Assert.Equal("Отмена создания сборки", viewModel.CurrentStep);
        Assert.Equal("Дожидаюсь безопасной остановки", viewModel.StatusText);
        Assert.Equal(BuildCreationProcessViewModel.CompleteStepState, viewModel.TemplateStepState);
        Assert.Equal(BuildCreationProcessViewModel.CancelingStepState, viewModel.FilesStepState);
        Assert.Equal(BuildCreationProcessViewModel.PendingStepState, viewModel.CatalogStepState);
    }

    [Fact]
    public void Complete_AllowsCloseAndCloseResetsTerminalState()
    {
        BuildCreationProcessViewModel viewModel = new();
        viewModel.Start("Foundation Edition", "Skyrim Template", @"C:\Builds\Foundation");
        viewModel.SetStage(BuildCreationProcessStage.Catalog, 90, "Обновляю каталог", "Почти готово");

        viewModel.Complete("Foundation Edition");

        Assert.True(viewModel.IsVisible);
        Assert.False(viewModel.IsRunning);
        Assert.True(viewModel.IsCompleted);
        Assert.False(viewModel.IsCanceled);
        Assert.False(viewModel.HasError);
        Assert.True(viewModel.CanClose);
        Assert.False(viewModel.CanCancel);
        Assert.Equal(100, viewModel.OverallPercent);
        Assert.Equal("Сборка создана", viewModel.CurrentStep);
        Assert.Equal("Foundation Edition готова", viewModel.StatusText);
        Assert.Equal(BuildCreationProcessViewModel.CompleteStepState, viewModel.DataStepState);
        Assert.Equal(BuildCreationProcessViewModel.CompleteStepState, viewModel.TemplateStepState);
        Assert.Equal(BuildCreationProcessViewModel.CompleteStepState, viewModel.FilesStepState);
        Assert.Equal(BuildCreationProcessViewModel.CompleteStepState, viewModel.CatalogStepState);

        viewModel.Close();

        Assert.False(viewModel.IsVisible);
        Assert.False(viewModel.IsCompleted);
        Assert.False(viewModel.IsCanceled);
        Assert.False(viewModel.HasError);
        Assert.False(viewModel.IsCancellationRequested);
        Assert.Equal(0, viewModel.OverallPercent);
    }

    [Fact]
    public void Cancel_AllowsCloseWithoutError()
    {
        BuildCreationProcessViewModel viewModel = new();
        viewModel.Start("Foundation Edition", "Skyrim Template", @"C:\Builds\Foundation");
        viewModel.SetStage(BuildCreationProcessStage.Files, 55, "Создаю файлы", "Записываю структуру");
        viewModel.RequestCancel();

        viewModel.Cancel();

        Assert.True(viewModel.IsVisible);
        Assert.False(viewModel.IsRunning);
        Assert.False(viewModel.IsCompleted);
        Assert.True(viewModel.IsCanceled);
        Assert.False(viewModel.HasError);
        Assert.True(viewModel.CanClose);
        Assert.False(viewModel.CanCancel);
        Assert.Equal("Создание отменено", viewModel.CurrentStep);
        Assert.Equal("Операция остановлена", viewModel.StatusText);
        Assert.Equal(string.Empty, viewModel.ErrorMessage);
        Assert.Equal(BuildCreationProcessViewModel.CancelingStepState, viewModel.FilesStepState);

        viewModel.Close();

        Assert.False(viewModel.IsVisible);
        Assert.False(viewModel.IsCanceled);
    }

    [Fact]
    public void Fail_ShowsErrorAndAllowsClose()
    {
        BuildCreationProcessViewModel viewModel = new();
        viewModel.Start("Foundation Edition", "Skyrim Template", @"C:\Builds\Foundation");
        viewModel.SetStage(BuildCreationProcessStage.Template, 30, "Применяю шаблон", "Копирую основу");

        viewModel.Fail("Нет доступа к папке назначения");

        Assert.True(viewModel.IsVisible);
        Assert.False(viewModel.IsRunning);
        Assert.False(viewModel.IsCompleted);
        Assert.True(viewModel.HasError);
        Assert.True(viewModel.CanClose);
        Assert.False(viewModel.CanCancel);
        Assert.Equal("Не удалось создать сборку", viewModel.CurrentStep);
        Assert.Equal("Создание остановлено", viewModel.StatusText);
        Assert.Equal("Нет доступа к папке назначения", viewModel.ErrorMessage);
        Assert.Equal(BuildCreationProcessViewModel.ActiveStepState, viewModel.TemplateStepState);
    }

    [Theory]
    [InlineData(0, 1, false, false, BuildCreationProcessViewModel.CompleteStepState)]
    [InlineData(1, 1, false, false, BuildCreationProcessViewModel.ActiveStepState)]
    [InlineData(2, 1, false, false, BuildCreationProcessViewModel.PendingStepState)]
    [InlineData(1, 1, false, true, BuildCreationProcessViewModel.CancelingStepState)]
    [InlineData(2, 1, true, true, BuildCreationProcessViewModel.CompleteStepState)]
    public void ResolveStepState_ComputesStepperState(
        int stepIndex,
        int activeStepIndex,
        bool isCompleted,
        bool isCancellationRequested,
        string expectedState)
    {
        string state = BuildCreationProcessViewModel.ResolveStepState(
            stepIndex,
            activeStepIndex,
            isCompleted,
            isCancellationRequested);

        Assert.Equal(expectedState, state);
    }
}
