using Fluxora.App.Models;

namespace Fluxora.App.Services;

public interface IBuildDeletionDialogService
{
    bool Confirm(ConfirmDialogOptions options);
}
