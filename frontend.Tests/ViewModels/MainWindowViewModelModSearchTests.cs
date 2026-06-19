using Fluxora.App.Models;
using Fluxora.App.Services;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class MainWindowViewModelModSearchTests
{
    [Fact]
    public void ResolveVisibleModsForSearch_EmptyQuery_UsesCollapsedVisibilityAndPreservesOrder()
    {
        SeparatorCollapseService collapseService = new();
        List<ModEntry> rows =
        [
            Separator("visuals", "Визуал"),
            Mod("skyui", "SkyUI"),
            Mod("textures", "Texture Pack"),
            Separator("gameplay", "Геймплей"),
            Mod("ordinator", "Ordinator")
        ];

        collapseService.Toggle(rows[0]);
        collapseService.Apply(rows);

        IReadOnlyList<ModEntry> visible = MainWindowViewModel.ResolveVisibleModsForSearch(rows, " ");

        Assert.Equal(["Визуал", "Геймплей", "Ordinator"], VisibleNames(visible));
    }

    [Fact]
    public void ResolveVisibleModsForSearch_MatchesModNameCaseInsensitively()
    {
        List<ModEntry> rows = [Mod("skyui", "Dragonborn UI - SkyUI Reskin")];

        IReadOnlyList<ModEntry> visible = MainWindowViewModel.ResolveVisibleModsForSearch(rows, "skyui");

        Assert.Equal(["Dragonborn UI - SkyUI Reskin"], VisibleNames(visible));
    }

    [Fact]
    public void ResolveVisibleModsForSearch_MatchesSeparatorTitleAndIncludesSectionRows()
    {
        List<ModEntry> rows =
        [
            Separator("animation", "Фиксы Анимаций"),
            Mod("efps", "eFPS"),
            Mod("lightened", "Lightened Skyrim"),
            Separator("patches", "Патчи"),
            Mod("elsopa", "ElSopa Patch")
        ];

        IReadOnlyList<ModEntry> visible = MainWindowViewModel.ResolveVisibleModsForSearch(rows, "анимаций");

        Assert.Equal(["Фиксы Анимаций", "eFPS", "Lightened Skyrim"], VisibleNames(visible));
    }

    [Fact]
    public void ResolveVisibleModsForSearch_ModMatchIncludesNearestSeparatorContext()
    {
        List<ModEntry> rows =
        [
            Separator("output", "Папки для выхода файлов"),
            Mod("grass", "No Grass In Objects"),
            Mod("pandora", "Pandora Output")
        ];

        IReadOnlyList<ModEntry> visible = MainWindowViewModel.ResolveVisibleModsForSearch(rows, "pandora");

        Assert.Equal(["Папки для выхода файлов", "Pandora Output"], VisibleNames(visible));
    }

    [Fact]
    public void ResolveVisibleModsForSearch_MatchingCollapsedChildIsFindable()
    {
        SeparatorCollapseService collapseService = new();
        List<ModEntry> rows =
        [
            Separator("optimization", "Оптимизация"),
            Mod("priority", "Skyrim Priority SE AE")
        ];
        collapseService.Toggle(rows[0]);
        collapseService.Apply(rows);

        IReadOnlyList<ModEntry> visible = MainWindowViewModel.ResolveVisibleModsForSearch(rows, "priority");

        Assert.Equal(["Оптимизация", "Skyrim Priority SE AE"], VisibleNames(visible));
    }

    [Fact]
    public void ResolveVisibleModsForSearch_UsesAllSearchTerms()
    {
        List<ModEntry> rows =
        [
            Mod("skyui", "Dragonborn UI - SkyUI Reskin - Widescreen 32x9"),
            Mod("address", "Address Library")
        ];

        IReadOnlyList<ModEntry> visible = MainWindowViewModel.ResolveVisibleModsForSearch(rows, "skyui 32x9");

        Assert.Equal(["Dragonborn UI - SkyUI Reskin - Widescreen 32x9"], VisibleNames(visible));
    }

    [Fact]
    public void ResolveVisibleModsForSearch_NoMatchesReturnsEmpty()
    {
        List<ModEntry> rows =
        [
            Separator("visuals", "Визуал"),
            Mod("skyui", "SkyUI")
        ];

        IReadOnlyList<ModEntry> visible = MainWindowViewModel.ResolveVisibleModsForSearch(rows, "nemesis");

        Assert.Empty(visible);
    }

    private static ModEntry Separator(string id, string title)
    {
        return new ModEntry
        {
            Id = id,
            Kind = "separator",
            Name = title,
            SeparatorTitle = title
        };
    }

    private static ModEntry Mod(string id, string name)
    {
        return new ModEntry
        {
            Id = id,
            Name = name
        };
    }

    private static string[] VisibleNames(IReadOnlyList<ModEntry> rows)
    {
        return rows.Select(row => row.DisplayName).ToArray();
    }
}
