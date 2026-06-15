using Fluxora.App.Models;

namespace Fluxora.App.Services;

public sealed class ModCatalogService : IAppService
{
    private readonly CoreBridgeService coreBridgeService;

    public ModCatalogService(CoreBridgeService coreBridgeService)
    {
        this.coreBridgeService = coreBridgeService;
    }

    public Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.CompletedTask;
    }

    public Task<IReadOnlyList<ModEntry>> GetInstalledModsAsync(
        ModProject project,
        string profileName,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.GetModOrderAsync(project.ProjectDirectory, profileName, cancellationToken);
    }

    public Task<IReadOnlyList<ModEntry>> CreateModSeparatorAsync(
        ModProject project,
        string profileName,
        string title,
        int targetIndex,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.CreateModSeparatorAsync(
            project.ProjectDirectory,
            profileName,
            title,
            targetIndex,
            cancellationToken);
    }

    public Task<IReadOnlyList<ModEntry>> DeleteModSeparatorAsync(
        ModProject project,
        string profileName,
        ModEntry separator,
        CancellationToken cancellationToken = default)
    {
        if (!separator.IsSeparator)
        {
            throw new InvalidOperationException("Only separators can be removed from the profile order.");
        }

        return coreBridgeService.DeleteModSeparatorAsync(
            project.ProjectDirectory,
            profileName,
            string.IsNullOrWhiteSpace(separator.OrderId) ? separator.Id : separator.OrderId,
            cancellationToken);
    }

    public Task<IReadOnlyList<ModEntry>> MoveModOrderItemAsync(
        ModProject project,
        string profileName,
        ModEntry mod,
        int targetIndex,
        CancellationToken cancellationToken = default)
    {
        string orderItemId = string.IsNullOrWhiteSpace(mod.OrderId) ? mod.Id : mod.OrderId;
        if (string.IsNullOrWhiteSpace(orderItemId))
        {
            throw new InvalidOperationException("Profile order item id is required.");
        }

        return coreBridgeService.MoveModOrderItemAsync(
            project.ProjectDirectory,
            profileName,
            orderItemId,
            targetIndex,
            cancellationToken);
    }

    public Task DeleteInstalledModAsync(
        ModProject project,
        ModEntry mod,
        CancellationToken cancellationToken = default)
    {
        if (!mod.IsMod)
        {
            throw new InvalidOperationException("Only mods can be deleted from disk.");
        }

        return coreBridgeService.DeleteInstalledModAsync(project.ProjectDirectory, mod.Id, cancellationToken);
    }

    public Task SetInstalledModEnabledAsync(
        ModProject project,
        ModEntry mod,
        bool isEnabled,
        CancellationToken cancellationToken = default)
    {
        if (!mod.IsMod)
        {
            throw new InvalidOperationException("Only mods can be enabled or disabled.");
        }

        return coreBridgeService.SetInstalledModEnabledAsync(project.ProjectDirectory, mod.Id, isEnabled, cancellationToken);
    }

    public Task SetAllInstalledModsEnabledAsync(
        ModProject project,
        bool isEnabled,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.SetAllInstalledModsEnabledAsync(project.ProjectDirectory, isEnabled, cancellationToken);
    }

    public Task<IReadOnlyList<ModEntry>> CheckModUpdatesAsync(
        ModProject project,
        string profileName,
        CancellationToken cancellationToken = default)
    {
        return CheckModUpdatesAndReadOrderAsync(project, profileName, cancellationToken);
    }

    public Task<IReadOnlyList<ModFileTreeEntry>> GetModFileTreeAsync(
        ModProject project,
        ModEntry mod,
        string relativeDirectory,
        CancellationToken cancellationToken = default)
    {
        if (!mod.IsMod)
        {
            return Task.FromResult<IReadOnlyList<ModFileTreeEntry>>(Array.Empty<ModFileTreeEntry>());
        }

        return coreBridgeService.GetModFileTreeAsync(
            project.ProjectDirectory,
            mod.Id,
            relativeDirectory,
            cancellationToken);
    }

    private async Task<IReadOnlyList<ModEntry>> CheckModUpdatesAndReadOrderAsync(
        ModProject project,
        string profileName,
        CancellationToken cancellationToken)
    {
        await coreBridgeService.CheckModUpdatesAsync(project.ProjectDirectory, cancellationToken);
        return await GetInstalledModsAsync(project, profileName, cancellationToken);
    }
}
