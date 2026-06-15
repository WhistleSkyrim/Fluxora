using System.IO;
using Fluxora.App.Models;

namespace Fluxora.App.Services;

public sealed class ProjectOpenService : IAppService
{
    private const string BuildConfigFileName = "fluxora.build.json";

    private readonly ProjectCatalogService projectCatalogService;
    private readonly CoreBridgeService coreBridgeService;

    public ProjectOpenService(
        ProjectCatalogService projectCatalogService,
        CoreBridgeService coreBridgeService)
    {
        this.projectCatalogService = projectCatalogService;
        this.coreBridgeService = coreBridgeService;
    }

    public bool CanOpenProjects => coreBridgeService.CanOpenProjectsNatively;

    public Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.CompletedTask;
    }

    public Task<ModProject> OpenFromConfigAsync(
        string configPath,
        CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(configPath))
        {
            throw new ArgumentException("Build config path is required.", nameof(configPath));
        }

        return projectCatalogService.OpenProjectFromConfigAsync(configPath, cancellationToken);
    }

    public Task<ModProject> OpenProjectAsync(
        ModProject project,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);

        return OpenFromConfigAsync(ResolveBuildConfigPath(project), cancellationToken);
    }

    private static string ResolveBuildConfigPath(ModProject project)
    {
        if (!string.IsNullOrWhiteSpace(project.ConfigPath))
        {
            return project.ConfigPath;
        }

        if (!string.IsNullOrWhiteSpace(project.ProjectDirectory))
        {
            return Path.Combine(project.ProjectDirectory, BuildConfigFileName);
        }

        throw new InvalidOperationException("Build config path could not be resolved.");
    }
}
