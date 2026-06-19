using Fluxora.App.Models;

namespace Fluxora.App.Services;

public sealed class ProjectWorkspaceLoadService : IAppService
{
    private readonly ModCatalogService modCatalogService;
    private readonly PluginCatalogService pluginCatalogService;
    private readonly DownloadCatalogService downloadCatalogService;

    public ProjectWorkspaceLoadService(
        ModCatalogService modCatalogService,
        PluginCatalogService pluginCatalogService,
        DownloadCatalogService downloadCatalogService)
    {
        this.modCatalogService = modCatalogService;
        this.pluginCatalogService = pluginCatalogService;
        this.downloadCatalogService = downloadCatalogService;
    }

    public Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.CompletedTask;
    }

    public async Task<ProjectWorkspaceLoadResult> LoadAsync(
        ModProject project,
        string profileName,
        bool includeDownloads = true,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);
        cancellationToken.ThrowIfCancellationRequested();

        Task<ProjectWorkspaceLoadSection<DownloadEntry>>? downloadsTask = includeDownloads
            ? CaptureAsync(token => downloadCatalogService.GetDownloadsAsync(project, token), cancellationToken)
            : null;

        try
        {
            ProjectWorkspaceLoadSection<ModEntry> mods =
                await CaptureAsync(token => modCatalogService.GetInstalledModsAsync(project, profileName, token), cancellationToken);

            cancellationToken.ThrowIfCancellationRequested();

            ProjectWorkspaceLoadSection<PluginEntry> plugins = ShouldRequestPluginSection(project)
                ? await CaptureAsync(token => pluginCatalogService.GetPluginsAsync(project, profileName, token), cancellationToken)
                : ProjectWorkspaceLoadSection<PluginEntry>.Success(Array.Empty<PluginEntry>());

            ProjectWorkspaceLoadSection<DownloadEntry>? downloads = downloadsTask is null
                ? null
                : await downloadsTask;

            return new ProjectWorkspaceLoadResult(mods, plugins, downloads);
        }
        catch
        {
            ObserveIfFaulted(downloadsTask);
            throw;
        }
    }

    public async Task<ProjectWorkspaceProfileLoadResult> LoadProfileScopedAsync(
        ModProject project,
        string profileName,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);
        cancellationToken.ThrowIfCancellationRequested();

        ProjectWorkspaceLoadSection<ModEntry> mods =
            await CaptureAsync(token => modCatalogService.GetInstalledModsAsync(project, profileName, token), cancellationToken);

        cancellationToken.ThrowIfCancellationRequested();

        ProjectWorkspaceLoadSection<PluginEntry> plugins = ShouldRequestPluginSection(project)
            ? await CaptureAsync(token => pluginCatalogService.GetPluginsAsync(project, profileName, token), cancellationToken)
            : ProjectWorkspaceLoadSection<PluginEntry>.Success(Array.Empty<PluginEntry>());

        return new ProjectWorkspaceProfileLoadResult(mods, plugins);
    }

    internal static bool ShouldRequestPluginSection(ModProject project)
    {
        return GameCapabilityResolver.ForProject(project).SupportsPluginSection;
    }

    private static async Task<ProjectWorkspaceLoadSection<T>> CaptureAsync<T>(
        Func<CancellationToken, Task<IReadOnlyList<T>>> load,
        CancellationToken cancellationToken)
    {
        try
        {
            IReadOnlyList<T> items = await load(cancellationToken);
            return ProjectWorkspaceLoadSection<T>.Success(items);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (Exception exception)
        {
            return ProjectWorkspaceLoadSection<T>.Failure(exception);
        }
    }

    private static void ObserveIfFaulted<T>(Task<ProjectWorkspaceLoadSection<T>>? task)
    {
        if (task is null)
        {
            return;
        }

        _ = task.ContinueWith(
            completed => _ = completed.Exception,
            TaskContinuationOptions.OnlyOnFaulted);
    }
}

public sealed record ProjectWorkspaceLoadResult(
    ProjectWorkspaceLoadSection<ModEntry> Mods,
    ProjectWorkspaceLoadSection<PluginEntry> Plugins,
    ProjectWorkspaceLoadSection<DownloadEntry>? Downloads)
{
    public bool HasErrors => Mods.Error is not null ||
        Plugins.Error is not null ||
        Downloads?.Error is not null;
}

public sealed record ProjectWorkspaceProfileLoadResult(
    ProjectWorkspaceLoadSection<ModEntry> Mods,
    ProjectWorkspaceLoadSection<PluginEntry> Plugins)
{
    public bool HasErrors => Mods.Error is not null || Plugins.Error is not null;
}

public sealed record ProjectWorkspaceLoadSection<T>(
    IReadOnlyList<T> Items,
    Exception? Error)
{
    public static ProjectWorkspaceLoadSection<T> Success(IReadOnlyList<T> items)
    {
        return new ProjectWorkspaceLoadSection<T>(items, null);
    }

    public static ProjectWorkspaceLoadSection<T> Failure(Exception error)
    {
        return new ProjectWorkspaceLoadSection<T>(Array.Empty<T>(), error);
    }
}
