using System.Windows;
using Fluxora.App.Models;
using Fluxora.App.Services;
using Fluxora.App.ViewModels;

namespace Fluxora.App;

public partial class BuildSettingsWindow : Window
{
    private readonly WindowChromeService windowChromeService;
    private readonly BuildSettingsWindowViewModel viewModel;

    public BuildSettingsWindow(
        CoreBridgeService coreBridgeService,
        IFolderPickerService folderPickerService,
        IExecutablePickerService executablePickerService,
        ModProject project)
    {
        InitializeComponent();
        windowChromeService = new WindowChromeService(this);
        windowChromeService.Attach();
        viewModel = new BuildSettingsWindowViewModel(
            coreBridgeService,
            folderPickerService,
            executablePickerService,
            project);
        DataContext = viewModel;
    }

    public BuildSettingsResult? SavedResult => viewModel.SavedResult;

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        await viewModel.InitializeAsync();
    }

    private async void OnSaveButtonClick(object sender, RoutedEventArgs e)
    {
        if (await viewModel.SaveAsync())
        {
            DialogResult = true;
            Close();
        }
    }

    private void OnCloseButtonClick(object sender, RoutedEventArgs e)
    {
        Close();
    }
}
