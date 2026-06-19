using Fluxora.App.Models;

namespace Fluxora.App.Services;

public interface IModInstallDialogService
{
    string? PickModName(string suggestedName, ContentLayoutPreview? layoutPreview = null);
    ExistingModInstallMode? PickExistingModInstallMode(string modName);
    IReadOnlyList<string>? PickFomodSelections(FomodInstallerInfo installer);
    string? PickSeparatorName(string suggestedName);
    string? PickProjectName(string suggestedName);
}
