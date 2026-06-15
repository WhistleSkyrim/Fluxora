using Fluxora.App.Models;

namespace Fluxora.App.Services;

public sealed class BuildSettingsDialogService : IBuildSettingsDialogService
{
    private readonly CoreBridgeService coreBridgeService;
    private readonly IFolderPickerService folderPickerService;
    private readonly IExecutablePickerService executablePickerService;

    public BuildSettingsDialogService(
        CoreBridgeService coreBridgeService,
        IFolderPickerService folderPickerService,
        IExecutablePickerService executablePickerService)
    {
        this.coreBridgeService = coreBridgeService;
        this.folderPickerService = folderPickerService;
        this.executablePickerService = executablePickerService;
    }

    public BuildSettingsResult? EditBuildPaths(ModProject project)
    {
        BuildSettingsWindow dialog = new(
            coreBridgeService,
            folderPickerService,
            executablePickerService,
            project)
        {
            Owner = System.Windows.Application.Current?.MainWindow
        };

        return dialog.ShowDialog() == true ? dialog.SavedResult : null;
    }
}
