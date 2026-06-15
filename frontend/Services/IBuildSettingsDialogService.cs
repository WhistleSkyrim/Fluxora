using Fluxora.App.Models;

namespace Fluxora.App.Services;

public interface IBuildSettingsDialogService
{
    BuildPathSettings? EditBuildPaths(ModProject project);
}
