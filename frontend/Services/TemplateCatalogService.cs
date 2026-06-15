using Fluxora.App.Models;

namespace Fluxora.App.Services;

/// <summary>
/// Frontend gateway to the build-template catalog. All template knowledge lives
/// in the C++ core; this service is a thin, cached bridge so view models never
/// talk to the native layer (or embed game knowledge) directly.
/// </summary>
public sealed class TemplateCatalogService : IAppService
{
    private readonly CoreBridgeService coreBridgeService;
    private readonly List<GameTemplateOption> gameTemplates = new();
    private readonly Dictionary<string, ResolvedTemplate> resolvedCache = new();

    public TemplateCatalogService(CoreBridgeService coreBridgeService)
    {
        this.coreBridgeService = coreBridgeService;
    }

    public Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        gameTemplates.Clear();
        resolvedCache.Clear();
        gameTemplates.AddRange(coreBridgeService.GetGameTemplates());

        return Task.CompletedTask;
    }

    public IReadOnlyList<GameTemplateOption> GameTemplates => gameTemplates;

    public bool HasTemplates => gameTemplates.Count > 0;

    /// <summary>Resolve a template id into the base + game result, cached per id.</summary>
    public ResolvedTemplate? Resolve(string? templateId)
    {
        if (string.IsNullOrWhiteSpace(templateId))
        {
            return null;
        }

        if (resolvedCache.TryGetValue(templateId, out ResolvedTemplate? cached))
        {
            return cached;
        }

        ResolvedTemplate? resolved = coreBridgeService.ResolveTemplate(templateId);
        if (resolved is not null)
        {
            resolvedCache[templateId] = resolved;
        }

        return resolved;
    }
}
