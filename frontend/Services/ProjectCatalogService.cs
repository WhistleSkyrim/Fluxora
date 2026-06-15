using System.IO;
using Fluxora.App.Models;

namespace Fluxora.App.Services;

public sealed class ProjectCatalogService : IAppService
{
    private const string BuildConfigFileName = "fluxora.build.json";

    private readonly CoreBridgeService coreBridgeService;
    private readonly SettingsService settingsService;
    private readonly List<ModProject> projects = new();

    public ProjectCatalogService(
        CoreBridgeService coreBridgeService,
        SettingsService settingsService)
    {
        this.coreBridgeService = coreBridgeService;
        this.settingsService = settingsService;
    }

    public async Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        projects.Clear();

        if (!coreBridgeService.CanOpenProjectsNatively)
        {
            return;
        }

        Directory.CreateDirectory(settingsService.BuildConfigsDirectory);
        try
        {
            IReadOnlyList<ModProject> catalog = await coreBridgeService.ListProjectConfigsAsync(
                settingsService.BuildConfigsDirectory,
                cancellationToken);
            foreach (ModProject project in catalog)
            {
                cancellationToken.ThrowIfCancellationRequested();
                UpsertProject(project);
            }

            return;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (EntryPointNotFoundException)
        {
            // Older native cores do not expose the fast catalog endpoint.
        }
        catch (Exception)
        {
            // Fall back to the full open path so the catalog still works if a
            // stale config trips the lightweight native reader.
            projects.Clear();
        }

        foreach (string configPath in Directory
            .EnumerateFiles(settingsService.BuildConfigsDirectory, "*.json", SearchOption.TopDirectoryOnly)
            .OrderByDescending(File.GetLastWriteTimeUtc))
        {
            cancellationToken.ThrowIfCancellationRequested();

            try
            {
                ModProject project = await coreBridgeService.OpenProjectFromConfigAsync(configPath, cancellationToken);
                UpsertProject(project);
            }
            catch (Exception)
            {
                // Ignore stale or unrelated JSON files in the user data folder.
            }
        }
    }

    public Task<IReadOnlyList<ModProject>> GetProjectsAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult<IReadOnlyList<ModProject>>(projects.AsReadOnly());
    }

    public string BuildProjectDirectoryPreview(string projectName, string installRootDirectory)
    {
        return coreBridgeService.BuildProjectDirectoryPreview(projectName, installRootDirectory);
    }

    public async Task<ModProject> CreateProjectAsync(
        string name,
        ResolvedTemplate template,
        string gamePath,
        string installRootDirectory,
        CancellationToken cancellationToken = default)
    {
        ModProject project = await coreBridgeService.CreateProjectAsync(
            name,
            template,
            gamePath,
            installRootDirectory,
            cancellationToken);

        UpsertProject(project);
        return project;
    }

    public async Task<ModProject> OpenProjectFromConfigAsync(
        string configPath,
        CancellationToken cancellationToken = default)
    {
        ModProject project = await coreBridgeService.OpenProjectFromConfigAsync(configPath, cancellationToken);
        UpsertProject(project);
        return project;
    }

    public async Task<ModProject> RenameProjectAsync(
        ModProject project,
        string newName,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);

        ModProject renamedProject = await coreBridgeService.RenameProjectAsync(
            ResolveConfigPath(project),
            newName,
            cancellationToken);
        ReplaceProject(project, renamedProject);
        return renamedProject;
    }

    public async Task DeleteProjectAsync(
        ModProject project,
        Action<BuildDeletionProgress>? progress = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);

        await coreBridgeService.DeleteProjectAsync(ResolveConfigPath(project), progress, cancellationToken);
        RemoveProject(project);
    }

    private void UpsertProject(ModProject project)
    {
        int existingIndex = projects.FindIndex(candidate =>
            IsSamePath(candidate.ConfigPath, project.ConfigPath) ||
            IsSamePath(candidate.ProjectDirectory, project.ProjectDirectory));

        if (existingIndex >= 0)
        {
            projects[existingIndex] = project;
            return;
        }

        projects.Add(project);
    }

    private void ReplaceProject(ModProject oldProject, ModProject newProject)
    {
        int existingIndex = projects.FindIndex(candidate =>
            IsSameProject(candidate, oldProject) ||
            IsSameProject(candidate, newProject));

        if (existingIndex >= 0)
        {
            projects[existingIndex] = newProject;
            return;
        }

        projects.Add(newProject);
    }

    private void RemoveProject(ModProject project)
    {
        int existingIndex = projects.FindIndex(candidate => IsSameProject(candidate, project));
        if (existingIndex >= 0)
        {
            projects.RemoveAt(existingIndex);
        }
    }

    private static string ResolveConfigPath(ModProject project)
    {
        if (!string.IsNullOrWhiteSpace(project.ConfigPath))
        {
            return project.ConfigPath;
        }

        return string.IsNullOrWhiteSpace(project.ProjectDirectory)
            ? string.Empty
            : Path.Combine(project.ProjectDirectory, BuildConfigFileName);
    }

    private static bool IsSameProject(ModProject left, ModProject right)
    {
        return IsSamePath(left.ConfigPath, right.ConfigPath) ||
            IsSamePath(left.ProjectDirectory, right.ProjectDirectory) ||
            (!string.IsNullOrWhiteSpace(left.Id) &&
             string.Equals(left.Id, right.Id, StringComparison.OrdinalIgnoreCase));
    }

    private static bool IsSamePath(string left, string right)
    {
        return !string.IsNullOrWhiteSpace(left) &&
            !string.IsNullOrWhiteSpace(right) &&
            string.Equals(
                Path.GetFullPath(left).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                Path.GetFullPath(right).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                StringComparison.OrdinalIgnoreCase);
    }
}
