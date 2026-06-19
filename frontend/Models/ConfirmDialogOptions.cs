using System.Windows;
using System.Windows.Media;

namespace Fluxora.App.Models;

/// <summary>
/// One labelled fact shown inside a confirmation dialog (for example the build folder or the
/// config path). Purely presentational data — the icon is resolved from the shared icon set.
/// </summary>
public sealed class ConfirmDialogDetail
{
    public required Geometry? Icon { get; init; }
    public required string Label { get; init; }
    public required string Value { get; init; }

    /// <summary>Resolves a geometry from the merged <c>Icons.xaml</c> resource dictionary.</summary>
    public static Geometry? IconFromResource(string key)
    {
        return System.Windows.Application.Current?.TryFindResource(key) as Geometry;
    }
}

/// <summary>
/// Describes the content of a <see cref="ConfirmDialogWindow"/>. Holds only display text and the
/// facts to surface; it carries no behaviour, so the same window can back any confirmation.
/// </summary>
public sealed class ConfirmDialogOptions
{
    public required string Heading { get; init; }

    /// <summary>Body sentence. The <see cref="Highlight"/> substring is emphasised when present.</summary>
    public required string Message { get; init; }

    /// <summary>Optional substring of <see cref="Message"/> to render bold (e.g. the build name).</summary>
    public string? Highlight { get; init; }

    public IReadOnlyList<ConfirmDialogDetail> Details { get; init; } = Array.Empty<ConfirmDialogDetail>();

    public required string ConfirmText { get; init; }

    public string CancelText { get; init; } = "Отмена";

    /// <summary>When true the dialog uses the destructive (red) accent for the confirm action.</summary>
    public bool IsDestructive { get; init; }

    /// <summary>Confirmation for permanently deleting a build from disk.</summary>
    public static ConfirmDialogOptions DeleteBuild(ModProject project)
    {
        List<ConfirmDialogDetail> details = new()
        {
            new ConfirmDialogDetail
            {
                Icon = ConfirmDialogDetail.IconFromResource("Icon.Folder"),
                Label = "Папка сборки",
                Value = project.ProjectDirectory
            }
        };

        return new ConfirmDialogOptions
        {
            Heading = "Удалить сборку?",
            Message = $"«{project.Name}» и все её файлы будут безвозвратно удалены с диска. Отменить это действие нельзя.",
            Highlight = $"«{project.Name}»",
            Details = details,
            ConfirmText = "Удалить сборку",
            IsDestructive = true
        };
    }

    public static ConfirmDialogOptions IncludeGeneratedFluxPackAssets(ModProject project)
    {
        List<ConfirmDialogDetail> details = new()
        {
            new ConfirmDialogDetail
            {
                Icon = ConfirmDialogDetail.IconFromResource("Icon.FileText"),
                Label = "FluxPack",
                Value = "Source archives stay as links; generated assets are optional."
            },
            new ConfirmDialogDetail
            {
                Icon = ConfirmDialogDetail.IconFromResource("Icon.Folder"),
                Label = "Сборка",
                Value = project.ProjectDirectory
            }
        };

        return new ConfirmDialogOptions
        {
            Heading = "Включить generated assets?",
            Message = $"Для «{project.Name}» можно добавить манифест generated assets: LODGen, Synthesis, Nemesis, BodySlide, Pandora и похожие результаты. Это не добавит исходные архивы модов, но отметит эти файлы как разрешённые пользователем.",
            Highlight = $"«{project.Name}»",
            Details = details,
            ConfirmText = "Включить",
            CancelText = "Только рецепт"
        };
    }
}
