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
        ModProject project)
    {
        InitializeComponent();
        windowChromeService = new WindowChromeService(this);
        windowChromeService.Attach();
        viewModel = new BuildSettingsWindowViewModel(
            coreBridgeService,
            folderPickerService,
            project);
        DataContext = viewModel;
    }

    public BuildPathSettings? SavedSettings => viewModel.SavedSettings;

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
