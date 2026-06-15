using Fluxora.App.Models;

namespace Fluxora.App.Services;

public sealed class PluginCatalogService : IAppService
{
    private readonly CoreBridgeService coreBridgeService;

    public PluginCatalogService(CoreBridgeService coreBridgeService)
    {
        this.coreBridgeService = coreBridgeService;
    }

    public Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.CompletedTask;
    }

    public Task<IReadOnlyList<PluginEntry>> GetPluginsAsync(
        ModProject project,
        string profileName,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.GetPluginsAsync(
            project.ProjectDirectory,
            project.TemplateId,
            profileName,
            cancellationToken);
    }

    public Task<IReadOnlyList<PluginEntry>> MovePluginAsync(
        ModProject project,
        string profileName,
        PluginEntry plugin,
        int targetIndex,
        CancellationToken cancellationToken = default)
    {
        string orderItemId = string.IsNullOrWhiteSpace(plugin.OrderId) ? plugin.Id : plugin.OrderId;
        if (string.IsNullOrWhiteSpace(orderItemId))
        {
            throw new InvalidOperationException("Plugin order item id is required.");
        }

        return coreBridgeService.MovePluginAsync(
            project.ProjectDirectory,
            project.TemplateId,
            profileName,
            orderItemId,
            targetIndex,
            cancellationToken);
    }

    public Task<IReadOnlyList<PluginEntry>> CreatePluginSeparatorAsync(
        ModProject project,
        string profileName,
        string title,
        int targetIndex,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.CreatePluginSeparatorAsync(
            project.ProjectDirectory,
            project.TemplateId,
            profileName,
            title,
            targetIndex,
            cancellationToken);
    }

    public Task<IReadOnlyList<PluginEntry>> DeletePluginSeparatorAsync(
        ModProject project,
        string profileName,
        PluginEntry separator,
        CancellationToken cancellationToken = default)
    {
        if (!separator.IsSeparator)
        {
            throw new InvalidOperationException("Only separators can be removed from the plugin order.");
        }

        return coreBridgeService.DeletePluginSeparatorAsync(
            project.ProjectDirectory,
            project.TemplateId,
            profileName,
            string.IsNullOrWhiteSpace(separator.OrderId) ? separator.Id : separator.OrderId,
            cancellationToken);
    }

    public Task<IReadOnlyList<PluginEntry>> SetPluginEnabledAsync(
        ModProject project,
        string profileName,
        PluginEntry plugin,
        bool isEnabled,
        CancellationToken cancellationToken = default)
    {
        return coreBridgeService.SetPluginEnabledAsync(
            project.ProjectDirectory,
            project.TemplateId,
            profileName,
            plugin.Name,
            isEnabled,
            cancellationToken);
    }
}
