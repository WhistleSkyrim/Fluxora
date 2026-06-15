using Fluxora.App.Models;

namespace Fluxora.App.Services;

public sealed class ExecutableManagerDialogService : IExecutableManagerDialogService
{
    private readonly CoreBridgeService coreBridgeService;

    public ExecutableManagerDialogService(CoreBridgeService coreBridgeService)
    {
        this.coreBridgeService = coreBridgeService;
    }

    public IReadOnlyList<GameExecutableEntry>? EditExecutables(
        IReadOnlyList<GameExecutableEntry> executables,
        string gamePath,
        string projectDirectory)
    {
        ExecutableManagerWindow dialog = new(
            executables,
            gamePath,
            projectDirectory,
            coreBridgeService.ResolveExecutableIconPath)
        {
            Owner = System.Windows.Application.Current?.MainWindow
        };

        return dialog.ShowDialog() == true ? dialog.ResultExecutables : null;
    }
}
