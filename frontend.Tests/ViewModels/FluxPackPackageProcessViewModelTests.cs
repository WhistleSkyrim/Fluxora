using Fluxora.App.Models;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class FluxPackPackageProcessViewModelTests
{
    [Fact]
    public void Start_ShowsRunningProcessAndInitializesPackageDetails()
    {
        FluxPackPackageProcessViewModel viewModel = new();

        viewModel.Start(" Foundation Edition ", @" C:\Builds\Foundation.fluxpack ", includeGeneratedAssets: true);
        viewModel.Close();

        Assert.True(viewModel.IsVisible);
        Assert.True(viewModel.IsRunning);
        Assert.False(viewModel.IsCompleted);
        Assert.False(viewModel.HasError);
        Assert.False(viewModel.CanClose);
        Assert.Equal(0, viewModel.OverallPercent);
        Assert.Equal("Foundation Edition", viewModel.BuildName);
        Assert.Equal(@"C:\Builds\Foundation.fluxpack", viewModel.OutputPath);
        Assert.True(viewModel.IncludeGeneratedAssets);
        Assert.Contains("войдут", viewModel.CompositionText);
        Assert.Equal(FluxPackPackageProcessViewModel.ActiveStepState, viewModel.PrepareStepState);
        Assert.Equal(FluxPackPackageProcessViewModel.PendingStepState, viewModel.CompositionStepState);
        Assert.Equal(FluxPackPackageProcessViewModel.PendingStepState, viewModel.ExportStepState);
        Assert.Equal(FluxPackPackageProcessViewModel.PendingStepState, viewModel.SummaryStepState);
    }

    [Fact]
    public void SetStage_AdvancesStepsAndKeepsProgressMonotonic()
    {
        FluxPackPackageProcessViewModel viewModel = new();
        viewModel.Start("Foundation Edition", @"C:\Builds\Foundation.fluxpack", includeGeneratedAssets: false);

        viewModel.SetStage(FluxPackPackageProcessStage.Composition, 28, "Собираем состав", "Проверяем ассеты");
        viewModel.SetStage(FluxPackPackageProcessStage.Export, 72, "Записываем FluxPack", "C++ core пишет манифест");
        viewModel.SetStage(FluxPackPackageProcessStage.Composition, 10, "Регресс", "Не должен откатить шаг");

        Assert.Equal(72, viewModel.OverallPercent);
        Assert.Equal("Записываем FluxPack", viewModel.CurrentStep);
        Assert.Equal("C++ core пишет манифест", viewModel.StatusText);
        Assert.Equal(FluxPackPackageProcessViewModel.CompleteStepState, viewModel.PrepareStepState);
        Assert.Equal(FluxPackPackageProcessViewModel.CompleteStepState, viewModel.CompositionStepState);
        Assert.Equal(FluxPackPackageProcessViewModel.ActiveStepState, viewModel.ExportStepState);
        Assert.Equal(FluxPackPackageProcessViewModel.PendingStepState, viewModel.SummaryStepState);

        viewModel.SetStage(FluxPackPackageProcessStage.Summary, 94);

        Assert.Equal("Проверяем результат", viewModel.CurrentStep);
        Assert.Equal(FluxPackPackageProcessViewModel.CompleteStepState, viewModel.ExportStepState);
        Assert.Equal(FluxPackPackageProcessViewModel.ActiveStepState, viewModel.SummaryStepState);
    }

    [Fact]
    public void Complete_FormatsSummaryAndAllowsClose()
    {
        FluxPackPackageProcessViewModel viewModel = new();
        viewModel.Start("Foundation Edition", @"C:\Builds\Foundation.fluxpack", includeGeneratedAssets: true);
        viewModel.SetStage(FluxPackPackageProcessStage.Export, 72);

        viewModel.Complete(new FluxPackSummary
        {
            OutputPath = @"C:\Builds\Foundation.fluxpack",
            BuildName = "Foundation Edition",
            ManifestBytes = 2048,
            SourceArchiveCount = 12,
            GeneratedAssetCount = 3,
            CustomPatchCount = 2,
            CustomConfigCount = 4,
            GeneratedAssetsIncluded = true
        });

        Assert.True(viewModel.IsVisible);
        Assert.False(viewModel.IsRunning);
        Assert.True(viewModel.IsCompleted);
        Assert.False(viewModel.HasError);
        Assert.True(viewModel.CanClose);
        Assert.Equal(100, viewModel.OverallPercent);
        Assert.Equal("FluxPack готов", viewModel.CurrentStep);
        Assert.Equal("Сохранено: Foundation.fluxpack", viewModel.StatusText);
        Assert.Contains("Источники: 12", viewModel.SummaryText);
        Assert.Contains("манифест: 2 КБ", viewModel.SummaryText);
        Assert.Equal(FluxPackPackageProcessViewModel.CompleteStepState, viewModel.PrepareStepState);
        Assert.Equal(FluxPackPackageProcessViewModel.CompleteStepState, viewModel.CompositionStepState);
        Assert.Equal(FluxPackPackageProcessViewModel.CompleteStepState, viewModel.ExportStepState);
        Assert.Equal(FluxPackPackageProcessViewModel.CompleteStepState, viewModel.SummaryStepState);

        viewModel.Close();

        Assert.False(viewModel.IsVisible);
        Assert.False(viewModel.IsCompleted);
        Assert.False(viewModel.HasError);
        Assert.Equal(0, viewModel.OverallPercent);
    }

    [Fact]
    public void Fail_ShowsErrorAndAllowsClose()
    {
        FluxPackPackageProcessViewModel viewModel = new();
        viewModel.Start("Foundation Edition", @"C:\Builds\Foundation.fluxpack", includeGeneratedAssets: false);
        viewModel.SetStage(FluxPackPackageProcessStage.Export, 70, "Записываем FluxPack", "C++ core пишет манифест");

        viewModel.Fail("Нет доступа к файлу");

        Assert.True(viewModel.IsVisible);
        Assert.False(viewModel.IsRunning);
        Assert.False(viewModel.IsCompleted);
        Assert.True(viewModel.HasError);
        Assert.True(viewModel.CanClose);
        Assert.Equal("Не удалось упаковать сборку", viewModel.CurrentStep);
        Assert.Equal("Упаковка остановлена", viewModel.StatusText);
        Assert.Equal("Нет доступа к файлу", viewModel.ErrorMessage);
        Assert.Equal(FluxPackPackageProcessViewModel.ActiveStepState, viewModel.ExportStepState);
    }

    [Theory]
    [InlineData(0, 1, false, FluxPackPackageProcessViewModel.CompleteStepState)]
    [InlineData(1, 1, false, FluxPackPackageProcessViewModel.ActiveStepState)]
    [InlineData(2, 1, false, FluxPackPackageProcessViewModel.PendingStepState)]
    [InlineData(2, 1, true, FluxPackPackageProcessViewModel.CompleteStepState)]
    public void ResolveStepState_ComputesStepperState(int stepIndex, int activeStepIndex, bool isCompleted, string expectedState)
    {
        string state = FluxPackPackageProcessViewModel.ResolveStepState(stepIndex, activeStepIndex, isCompleted);

        Assert.Equal(expectedState, state);
    }
}
