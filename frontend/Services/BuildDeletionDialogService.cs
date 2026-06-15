using Fluxora.App.Models;
using Fluxora.App.Views;

namespace Fluxora.App.Services;

public sealed class BuildDeletionDialogService : IBuildDeletionDialogService
{
    public bool Confirm(ConfirmDialogOptions options)
    {
        ConfirmDialogWindow dialog = new(options)
        {
            Owner = System.Windows.Application.Current?.MainWindow
        };

        return dialog.ShowDialog() == true;
    }
}
