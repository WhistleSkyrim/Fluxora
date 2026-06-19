using Fluxora.App.Models;
using Fluxora.App.ViewModels;

namespace Fluxora.App.Tests.ViewModels;

public sealed class MainWindowViewModelGameTemplateSearchTests
{
    [Fact]
    public void ResolveVisibleTemplatesForSearch_EmptyQuery_PreservesOrder()
    {
        List<GameTemplateOption> templates =
        [
            Template("skyrim", "Skyrim Special Edition"),
            Template("fallout4", "Fallout 4")
        ];

        IReadOnlyList<GameTemplateOption> visible = MainWindowViewModel.ResolveVisibleTemplatesForSearch(templates, " ");

        Assert.Equal(["Skyrim Special Edition", "Fallout 4"], visible.Select(template => template.DisplayName).ToArray());
    }

    [Fact]
    public void ResolveVisibleTemplatesForSearch_MatchesDisplayNameCaseInsensitively()
    {
        List<GameTemplateOption> templates =
        [
            Template("skyrim", "Skyrim Special Edition"),
            Template("fallout4", "Fallout 4")
        ];

        IReadOnlyList<GameTemplateOption> visible = MainWindowViewModel.ResolveVisibleTemplatesForSearch(templates, "special");

        Assert.Equal(["Skyrim Special Edition"], visible.Select(template => template.DisplayName).ToArray());
    }

    [Fact]
    public void ResolveVisibleTemplatesForSearch_UsesAllTermsAcrossTemplateFields()
    {
        List<GameTemplateOption> templates =
        [
            Template(
                "skyrim",
                "Skyrim Special Edition",
                summary: "Плагины, профили, INI-твики и SKSE64.",
                archiveExtensions: [".bsa", ".ba2"],
                requiredFiles: ["SkyrimSE.exe"]),
            Template(
                "fallout4",
                "Fallout 4",
                summary: "Plugins and BA2 archives.",
                archiveExtensions: [".ba2"],
                requiredFiles: ["Fallout4.exe"])
        ];

        IReadOnlyList<GameTemplateOption> visible = MainWindowViewModel.ResolveVisibleTemplatesForSearch(templates, "skse64 skyrimse");

        Assert.Equal(["Skyrim Special Edition"], visible.Select(template => template.DisplayName).ToArray());
    }

    [Fact]
    public void ResolveVisibleTemplatesForSearch_NoMatches_ReturnsEmpty()
    {
        List<GameTemplateOption> templates =
        [
            Template("skyrim", "Skyrim Special Edition")
        ];

        IReadOnlyList<GameTemplateOption> visible = MainWindowViewModel.ResolveVisibleTemplatesForSearch(templates, "witcher");

        Assert.Empty(visible);
    }

    private static GameTemplateOption Template(
        string id,
        string displayName,
        string summary = "",
        IReadOnlyList<string>? archiveExtensions = null,
        IReadOnlyList<string>? requiredFiles = null)
    {
        return new GameTemplateOption
        {
            Id = id,
            DisplayName = displayName,
            GameName = displayName,
            Summary = summary,
            UiTemplateId = id,
            ArchiveExtensions = archiveExtensions?.ToList() ?? [],
            RequiredFiles = requiredFiles?.ToList() ?? []
        };
    }
}
