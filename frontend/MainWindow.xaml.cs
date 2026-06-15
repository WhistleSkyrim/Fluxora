using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using Fluxora.App.Models;
using Fluxora.App.Services;
using Fluxora.App.ViewModels;

namespace Fluxora.App;

public partial class MainWindow : Window
{
    private readonly WindowChromeService windowChromeService;
    private readonly CoreBridgeService coreBridgeService;
    private readonly SettingsService settingsService;
    private readonly LanguageCatalogService languageCatalogService;
    private readonly IFolderPickerService folderPickerService;
    private readonly IExecutablePickerService executablePickerService;
    private readonly MainWindowViewModel viewModel;
    private readonly ModOrderDragDropService modOrderDragDropService;
    private readonly PluginOrderDragDropService pluginOrderDragDropService;
    private readonly List<string> pendingExternalNxmLinks = new();
    private readonly SemaphoreSlim externalNxmLinksGate = new(1, 1);
    private bool isViewModelInitialized;

    public MainWindow(
        CoreBridgeService coreBridgeService,
        SettingsService settingsService,
        LanguageCatalogService languageCatalogService)
    {
        InitializeComponent();

        windowChromeService = new WindowChromeService(this);
        windowChromeService.Attach();

        this.coreBridgeService = coreBridgeService;
        this.settingsService = settingsService;
        this.languageCatalogService = languageCatalogService;
        folderPickerService = new FolderPickerService();
        executablePickerService = new ExecutablePickerService();
        DownloadCatalogService downloadCatalogService = new(this.coreBridgeService);
        NxmProtocolService nxmProtocolService = new(this.coreBridgeService);
        ModCatalogService modCatalogService = new(this.coreBridgeService);
        PluginCatalogService pluginCatalogService = new(this.coreBridgeService);
        ProjectCatalogService projectCatalogService = new(this.coreBridgeService, this.settingsService);
        ProjectOpenService projectOpenService = new(projectCatalogService, this.coreBridgeService);
        ProjectWorkspaceLoadService projectWorkspaceLoadService = new(
            modCatalogService,
            pluginCatalogService,
            downloadCatalogService);

        viewModel = new MainWindowViewModel(
            projectCatalogService,
            projectOpenService,
            modCatalogService,
            pluginCatalogService,
            downloadCatalogService,
            projectWorkspaceLoadService,
            nxmProtocolService,
            new TemplateCatalogService(coreBridgeService),
            coreBridgeService,
            settingsService,
            languageCatalogService,
            folderPickerService,
            executablePickerService,
            new BuildConfigPickerService(),
            new ModArchivePickerService(),
            new ModInstallDialogService(),
            new ExecutableManagerDialogService(coreBridgeService),
            new BuildSettingsDialogService(coreBridgeService, folderPickerService, executablePickerService),
            new BuildDeletionDialogService());

        DataContext = viewModel;

        modOrderDragDropService = new ModOrderDragDropService(
            ModsGrid,
            mod => viewModel.CanMoveModOrderItem(mod),
            (mod, insertionIndex) => viewModel.MoveModToInsertionIndexAsync(mod, insertionIndex));
        modOrderDragDropService.Attach();

        pluginOrderDragDropService = new PluginOrderDragDropService(
            PluginsListBox,
            plugin => viewModel.CanMovePluginOrderItem(plugin),
            (plugin, insertionIndex) => viewModel.MovePluginToInsertionIndexAsync(plugin, insertionIndex));
        pluginOrderDragDropService.Attach();
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        await InitializeAsync(App.StartupNxmLinks);
    }

    public async Task InitializeAsync(
        IEnumerable<string>? startupNxmLinks = null,
        CancellationToken cancellationToken = default)
    {
        if (isViewModelInitialized)
        {
            await FlushPendingExternalNxmLinksAsync();
            return;
        }

        await viewModel.InitializeAsync(startupNxmLinks, cancellationToken);
        isViewModelInitialized = true;
        await FlushPendingExternalNxmLinksAsync();
    }

    public async void HandleExternalActivation(IReadOnlyList<string> nxmLinks)
    {
        BringToForeground();
        await QueueOrCaptureExternalNxmLinksAsync(nxmLinks);
    }

    private void BringToForeground()
    {
        if (WindowState == WindowState.Minimized)
        {
            WindowState = WindowState.Normal;
        }

        Show();
        Activate();
        Topmost = true;
        Topmost = false;
        Focus();
    }

    private async Task QueueOrCaptureExternalNxmLinksAsync(IEnumerable<string> nxmLinks)
    {
        IReadOnlyList<string> links = nxmLinks
            .Where(link => !string.IsNullOrWhiteSpace(link))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
        if (links.Count == 0)
        {
            return;
        }

        await externalNxmLinksGate.WaitAsync();
        try
        {
            if (!isViewModelInitialized)
            {
                AddPendingExternalNxmLinks(links);
                return;
            }

            await viewModel.CaptureNxmLinksAsync(links);
        }
        finally
        {
            externalNxmLinksGate.Release();
        }
    }

    private async Task FlushPendingExternalNxmLinksAsync()
    {
        IReadOnlyList<string> links;
        await externalNxmLinksGate.WaitAsync();
        try
        {
            links = pendingExternalNxmLinks
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .ToList();
            pendingExternalNxmLinks.Clear();
        }
        finally
        {
            externalNxmLinksGate.Release();
        }

        await QueueOrCaptureExternalNxmLinksAsync(links);
    }

    private void AddPendingExternalNxmLinks(IEnumerable<string> nxmLinks)
    {
        foreach (string link in nxmLinks)
        {
            if (!pendingExternalNxmLinks.Contains(link, StringComparer.OrdinalIgnoreCase))
            {
                pendingExternalNxmLinks.Add(link);
            }
        }
    }

    private void OnStateChanged(object? sender, EventArgs e)
    {
        WindowRootGrid.Margin = new Thickness(0);
    }

    private void OnMinimizeButtonClick(object sender, RoutedEventArgs e)
    {
        SystemCommands.MinimizeWindow(this);
    }

    private void OnMaximizeRestoreButtonClick(object sender, RoutedEventArgs e)
    {
        if (WindowState == WindowState.Maximized)
        {
            SystemCommands.RestoreWindow(this);
            return;
        }

        SystemCommands.MaximizeWindow(this);
    }

    private void OnCloseButtonClick(object sender, RoutedEventArgs e)
    {
        SystemCommands.CloseWindow(this);
    }

    private async void OnSettingsButtonClick(object sender, RoutedEventArgs e)
    {
        ModProject? currentProject = viewModel.IsProjectWorkspaceOpen ? viewModel.SelectedProject : null;
        SettingsWindow settingsWindow = new(
            coreBridgeService,
            settingsService,
            languageCatalogService,
            folderPickerService,
            currentProject,
            currentProject is not null)
        {
            Owner = this
        };

        bool importedProjectHandled = false;
        settingsWindow.ImportedProjectChanged += async project =>
        {
            if (importedProjectHandled)
            {
                return;
            }

            importedProjectHandled = true;
            await viewModel.HandleImportedProjectAsync(project);
        };

        settingsWindow.ShowDialog();

        if (settingsWindow.ShouldOpenTransferInMainWindow)
        {
            await viewModel.OpenTransferFlowAsync();
            return;
        }

        if (settingsWindow.ImportedProject is not null && !importedProjectHandled)
        {
            await viewModel.HandleImportedProjectAsync(settingsWindow.ImportedProject);
        }
    }

    private void OnModsGridPreviewMouseRightButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (sender is not DataGrid dataGrid ||
            FindVisualParent<DataGridRow>(e.OriginalSource as DependencyObject) is not { } row)
        {
            return;
        }

        row.IsSelected = true;
        dataGrid.SelectedItem = row.Item;
        dataGrid.Focus();
    }

    private void OnPluginsListPreviewMouseRightButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (sender is not System.Windows.Controls.ListBox listBox ||
            FindVisualParent<ListBoxItem>(e.OriginalSource as DependencyObject) is not { } item)
        {
            return;
        }

        item.IsSelected = true;
        listBox.SelectedItem = item.DataContext;
        listBox.Focus();
    }

    private void OnProjectActionsButtonClick(object sender, RoutedEventArgs e)
    {
        if (sender is not System.Windows.Controls.Button button || button.ContextMenu is null)
        {
            return;
        }

        button.ContextMenu.PlacementTarget = button;
        button.ContextMenu.IsOpen = true;
        e.Handled = true;
    }

    private async void OnModFileTreeItemExpanded(object sender, RoutedEventArgs e)
    {
        if (sender is TreeViewItem { DataContext: ModFileTreeNode node })
        {
            await viewModel.LoadModFileTreeNodeAsync(node);
            e.Handled = true;
        }
    }

    private static T? FindVisualParent<T>(DependencyObject? current) where T : DependencyObject
    {
        while (current is not null)
        {
            if (current is T match)
            {
                return match;
            }

            current = VisualTreeHelper.GetParent(current);
        }

        return null;
    }
}
