using Fluxora.App.Models;

namespace Fluxora.App.Services;

public interface IBuildSettingsDialogService
{
    BuildSettingsResult? EditBuildPaths(ModProject project);
}
