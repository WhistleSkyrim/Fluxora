using System.IO;

namespace Fluxora.App.Services;

public sealed class SettingsService : IAppService
{
    public string ProjectsDirectory { get; private set; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "Fluxora",
        "Projects");

    public string ModsDirectory { get; private set; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "Fluxora",
        "Mods");

    public string BuildConfigsDirectory { get; private set; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "Fluxora",
        "Builds");

    public Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        Directory.CreateDirectory(ProjectsDirectory);
        Directory.CreateDirectory(ModsDirectory);
        Directory.CreateDirectory(BuildConfigsDirectory);
        return Task.CompletedTask;
    }
}
