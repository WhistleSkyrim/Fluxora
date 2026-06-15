using Fluxora.App.Models;

namespace Fluxora.App.Services;

public sealed class BuildSettingsDialogService : IBuildSettingsDialogService
{
    private readonly CoreBridgeService coreBridgeService;
    private readonly IFolderPickerService folderPickerService;

    public BuildSettingsDialogService(
        CoreBridgeService coreBridgeService,
        IFolderPickerService folderPickerService)
    {
        this.coreBridgeService = coreBridgeService;
        this.folderPickerService = folderPickerService;
    }

    public BuildPathSettings? EditBuildPaths(ModProject project)
    {
        BuildSettingsWindow dialog = new(
            coreBridgeService,
            folderPickerService,
            project)
        {
            Owner = System.Windows.Application.Current?.MainWindow
        };

        return dialog.ShowDialog() == true ? dialog.SavedSettings : null;
    }
}
