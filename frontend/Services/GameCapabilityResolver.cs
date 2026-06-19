using Fluxora.App.Models;

namespace Fluxora.App.Services;

internal readonly record struct UiCapabilityState(
    bool SupportsPlugins,
    bool SupportsLoadOrder,
    bool SupportsIniProfiles,
    bool SupportsSaveProfiles,
    bool SupportsScriptExtender,
    bool SupportsRootFiles,
    bool SupportsExecutablePanel,
    bool SupportsContentLayoutReview,
    bool SupportsHealthDiagnostics)
{
    public bool SupportsPluginSection => SupportsPlugins || SupportsLoadOrder;
}

internal static class GameCapabilityResolver
{
    public static UiCapabilityState ForProject(ModProject? project)
    {
        if (project is null)
        {
            return default;
        }

        ResolvedTemplate? template = project.Template;
        GameCapabilities projectCapabilities = project.GameCapabilities ?? new GameCapabilities();
        GameCapabilities templateCapabilities = template?.GameCapabilities ?? new GameCapabilities();

        bool supportsPlugins = HasCapability(
            projectCapabilities,
            templateCapabilities,
            template,
            capability => capability.SupportsPlugins,
            "plugins");
        bool supportsLoadOrder = HasCapability(
            projectCapabilities,
            templateCapabilities,
            template,
            capability => capability.SupportsLoadOrder,
            "load-order",
            "loadOrder");
        bool supportsIniProfiles = HasCapability(
            projectCapabilities,
            templateCapabilities,
            template,
            capability => capability.SupportsIniProfiles,
            "ini",
            "ini-tweaks",
            "ini-profiles");
        bool supportsSaveProfiles = HasCapability(
            projectCapabilities,
            templateCapabilities,
            template,
            capability => capability.SupportsSaveProfiles,
            "save",
            "saves",
            "save-games",
            "save-profiles");
        bool supportsScriptExtender = HasCapability(
            projectCapabilities,
            templateCapabilities,
            template,
            capability => capability.SupportsScriptExtender,
            "script-extender");
        bool supportsRootFiles = HasCapability(
            projectCapabilities,
            templateCapabilities,
            template,
            capability => capability.SupportsRootFiles,
            "root-files");
        bool supportsContentLayoutReview = HasCapability(
            projectCapabilities,
            templateCapabilities,
            template,
            capability => capability.SupportsContentLayoutRules,
            "content-layout",
            "content-layout-rules") ||
            project.ContentLayoutSummary?.Supported == true ||
            template?.ContentLayoutSummary?.Supported == true ||
            HasContentLayoutDiagnostics(project.ContentLayoutSummary) ||
            HasContentLayoutDiagnostics(template?.ContentLayoutSummary);
        bool supportsExecutablePanel = project.Executables.Count > 0 ||
            template?.Executables.Count > 0 ||
            template?.ExecutableDisplayMetadata.Count > 0;
        bool supportsHealthDiagnostics = HasHealthDiagnostics(project.GameHealthSummary);

        return new UiCapabilityState(
            supportsPlugins,
            supportsLoadOrder,
            supportsIniProfiles,
            supportsSaveProfiles,
            supportsScriptExtender,
            supportsRootFiles,
            supportsExecutablePanel,
            supportsContentLayoutReview,
            supportsHealthDiagnostics);
    }

    private static bool HasCapability(
        GameCapabilities projectCapabilities,
        GameCapabilities templateCapabilities,
        ResolvedTemplate? template,
        Func<GameCapabilities, bool> read,
        params string[] compatibilityIds)
    {
        return read(projectCapabilities) ||
            read(templateCapabilities) ||
            HasEnabledCapability(projectCapabilities, compatibilityIds) ||
            HasEnabledCapability(templateCapabilities, compatibilityIds) ||
            HasTemplateCapability(template, compatibilityIds);
    }

    private static bool HasEnabledCapability(GameCapabilities capabilities, IReadOnlyCollection<string> ids)
    {
        return capabilities.Enabled?.Any(enabled =>
            ids.Any(id => string.Equals(id, enabled, StringComparison.OrdinalIgnoreCase))) == true;
    }

    private static bool HasTemplateCapability(ResolvedTemplate? template, IReadOnlyCollection<string> ids)
    {
        return template?.Capabilities?.Any(capability =>
            ids.Any(id => string.Equals(id, capability.Id, StringComparison.OrdinalIgnoreCase))) == true;
    }

    private static bool HasHealthDiagnostics(GameHealthSummary? health)
    {
        return health is not null &&
            (!string.IsNullOrWhiteSpace(health.Status) && !string.Equals(health.Status, "unknown", StringComparison.OrdinalIgnoreCase) ||
             !string.IsNullOrWhiteSpace(health.Summary) ||
             health.HasBlockers ||
             health.MatchedFiles?.Count > 0 ||
             health.MissingFiles?.Count > 0 ||
             health.Warnings?.Count > 0 ||
             health.Findings?.Count > 0);
    }

    private static bool HasContentLayoutDiagnostics(ContentLayoutSummary? summary)
    {
        return summary is not null &&
            (summary.HasWarnings ||
             summary.HasBlockers ||
             summary.Warnings?.Count > 0 ||
             summary.Blockers?.Count > 0);
    }
}
