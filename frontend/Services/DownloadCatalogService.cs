using Fluxora.App.Models;

namespace Fluxora.App.Services;

public sealed class DownloadCatalogService : IAppService
{
    private readonly CoreBridgeService coreBridgeService;

    public DownloadCatalogService(CoreBridgeService coreBridgeService)
    {
        this.coreBridgeService = coreBridgeService;
    }

    public Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.CompletedTask;
    }

    public Task<IReadOnlyList<DownloadEntry>> GetDownloadsAsync(
        ModProject project,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.GetDownloadsAsync(project.ProjectDirectory, cancellationToken);
    }

    public Task<IReadOnlyList<DownloadEntry>> CaptureNxmLinksAsync(
        ModProject? project,
        IEnumerable<string> nxmLinks,
        CancellationToken cancellationToken = default)
    {
        string projectDirectory = project?.ProjectDirectory ?? string.Empty;
        return coreBridgeService.CaptureNxmLinksAsync(projectDirectory, nxmLinks, cancellationToken);
    }

    public Task<IReadOnlyList<DownloadEntry>> ImportPendingLinksAsync(
        ModProject project,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.ImportInboundDownloadsAsync(project.ProjectDirectory, cancellationToken);
    }

    public Task<DownloadEntry> ImportLocalFileAsync(
        ModProject project,
        string sourcePath,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.ImportDownloadFileAsync(project.ProjectDirectory, sourcePath, cancellationToken);
    }

    public Task DeleteDownloadAsync(
        ModProject project,
        DownloadEntry download,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.DeleteDownloadAsync(project.ProjectDirectory, download.LocalPath, cancellationToken);
    }

    public Task CancelDownloadAsync(
        ModProject project,
        DownloadEntry download,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.CancelDownloadAsync(project.ProjectDirectory, download.LocalPath, cancellationToken);
    }

    public Task<DownloadEntry> ResumeDownloadAsync(
        ModProject project,
        DownloadEntry download,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.ResumeDownloadAsync(project.ProjectDirectory, download.LocalPath, cancellationToken);
    }

    public Task<ModEntry> InstallDownloadAsync(
        ModProject project,
        DownloadEntry download,
        string modName,
        ExistingModInstallMode existingModMode,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.InstallDownloadAsync(
            project.ProjectDirectory,
            download.LocalPath,
            modName,
            existingModMode,
            cancellationToken);
    }

    public Task<ContentLayoutPreview> AnalyzeDownloadContentLayoutAsync(
        ModProject project,
        DownloadEntry download,
        ExistingModInstallMode existingModMode,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.AnalyzeDownloadContentLayoutAsync(
            project.ProjectDirectory,
            download.LocalPath,
            existingModMode,
            cancellationToken);
    }

    public Task<FomodInstallerInfo> AnalyzeFomodDownloadAsync(
        ModProject project,
        DownloadEntry download,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.AnalyzeFomodDownloadAsync(
            project.ProjectDirectory,
            download.LocalPath,
            cancellationToken);
    }

    public Task<ContentLayoutPreview> AnalyzeFomodDownloadContentLayoutAsync(
        ModProject project,
        DownloadEntry download,
        ExistingModInstallMode existingModMode,
        IReadOnlyList<string> selectedOptionIds,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.AnalyzeFomodDownloadContentLayoutAsync(
            project.ProjectDirectory,
            download.LocalPath,
            existingModMode,
            selectedOptionIds,
            cancellationToken);
    }

    public Task<ModEntry> InstallFomodDownloadAsync(
        ModProject project,
        DownloadEntry download,
        string modName,
        ExistingModInstallMode existingModMode,
        IReadOnlyList<string> selectedOptionIds,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.InstallFomodDownloadAsync(
            project.ProjectDirectory,
            download.LocalPath,
            modName,
            existingModMode,
            selectedOptionIds,
            cancellationToken);
    }
}
