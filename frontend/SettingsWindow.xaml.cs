using System.Windows;
using System.ComponentModel;
using Fluxora.App.Models;
using Fluxora.App.Services;
using Fluxora.App.ViewModels;

namespace Fluxora.App;

public partial class SettingsWindow : Window
{
    private readonly WindowChromeService windowChromeService;
    private readonly SettingsWindowViewModel viewModel;
    private bool importedProjectNotificationSent;

    public SettingsWindow(
        CoreBridgeService coreBridgeService,
        SettingsService settingsService,
        LanguageCatalogService languageCatalogService,
        IFolderPickerService folderPickerService,
        ModProject? currentProject,
        bool replaceCurrentProject)
    {
        InitializeComponent();
        windowChromeService = new WindowChromeService(this);
        windowChromeService.Attach();
        viewModel = new SettingsWindowViewModel(
            coreBridgeService,
            settingsService,
            languageCatalogService,
            folderPickerService,
            currentProject,
            replaceCurrentProject);
        viewModel.PropertyChanged += OnViewModelPropertyChanged;
        DataContext = viewModel;
    }

    public ModProject? ImportedProject => viewModel.ImportedProject;

    public bool ShouldOpenTransferInMainWindow { get; private set; }

    public event Action<ModProject>? ImportedProjectChanged;

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        await viewModel.InitializeAsync();
    }

    private void OnCloseButtonClick(object sender, RoutedEventArgs e)
    {
        if (viewModel.IsTransferRunning)
        {
            System.Windows.MessageBox.Show(
                "Дождитесь завершения переноса. Исходная сборка не удаляется, но окно прогресса нужно оставить открытым.",
                "Перенос выполняется",
                MessageBoxButton.OK,
                MessageBoxImage.Information);
            return;
        }

        Close();
    }

    private void OnTransferToMainButtonClick(object sender, RoutedEventArgs e)
    {
        ShouldOpenTransferInMainWindow = true;
        Close();
    }

    protected override void OnClosed(EventArgs e)
    {
        viewModel.PropertyChanged -= OnViewModelPropertyChanged;
        viewModel.Dispose();
        base.OnClosed(e);
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName != nameof(SettingsWindowViewModel.ImportedProject) ||
            viewModel.ImportedProject is null ||
            importedProjectNotificationSent)
        {
            return;
        }

        importedProjectNotificationSent = true;
        ImportedProjectChanged?.Invoke(viewModel.ImportedProject);
    }
}
