using System.Collections.Generic;

namespace Fluxora.App.Models;

/// <summary>
/// The fully resolved build template produced by the C++ core: the base
/// template with a game template overlaid on top. It describes both the build
/// structure on disk and the functional modules the build exposes. The WPF layer
/// only renders this — none of the resolution logic lives here.
/// </summary>
public sealed class ResolvedTemplate
{
    public string Id { get; set; } = string.Empty;
    public string DisplayName { get; set; } = string.Empty;
    public string GameName { get; set; } = string.Empty;
    public string Summary { get; set; } = string.Empty;
    public string BaseTemplateId { get; set; } = string.Empty;
    public string DefaultProfile { get; set; } = string.Empty;
    public string DataDirectory { get; set; } = string.Empty;
    public string NexusDomain { get; set; } = string.Empty;

    public List<string> Folders { get; set; } = new();
    public List<string> ProfileFiles { get; set; } = new();
    public List<string> BasePlugins { get; set; } = new();
    public List<string> PluginExtensions { get; set; } = new();
    public List<string> Executables { get; set; } = new();
    public List<TemplateCapability> Capabilities { get; set; } = new();
    public ScriptExtenderInfo? ScriptExtender { get; set; }

    public bool HasBasePlugins => BasePlugins.Count > 0;
    public bool HasScriptExtender => ScriptExtender is not null;
    public string PluginExtensionsText => string.Join("  ·  ", PluginExtensions);
}

/// <summary>A functional module exposed by a build (interface + functionality).</summary>
public sealed class TemplateCapability
{
    public string Id { get; set; } = string.Empty;
    public string DisplayName { get; set; } = string.Empty;
    public string Description { get; set; } = string.Empty;

    /// <summary>
    /// Segoe MDL2 Assets glyph used to illustrate the capability. This is pure
    /// presentation, mapped from the capability id the core supplies.
    /// </summary>
    public string Glyph => char.ConvertFromUtf32(GlyphCode);

    private int GlyphCode => Id switch
    {
        "mod-list" => 0xE71D,        // AllApps
        "profiles" => 0xE716,        // People
        "downloads" => 0xE896,       // Download
        "overwrite" => 0xE8C8,       // Copy
        "plugins" => 0xE950,         // Component
        "load-order" => 0xE8CB,      // Sort
        "ini-tweaks" => 0xE713,      // Settings
        "save-games" => 0xE74E,      // Save
        "script-extender" => 0xE756, // CommandPrompt
        _ => 0xE700                  // GlobalNavButton
    };
}

/// <summary>Optional script-extender metadata (SKSE, F4SE, ...).</summary>
public sealed class ScriptExtenderInfo
{
    public string Name { get; set; } = string.Empty;
    public string LoaderExecutable { get; set; } = string.Empty;
    public string Website { get; set; } = string.Empty;
}
