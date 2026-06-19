using Fluxora.App.Models;

namespace Fluxora.App.Services;

public sealed class ModInstallDialogService : IModInstallDialogService
{
    public string? PickModName(string suggestedName, ContentLayoutPreview? layoutPreview = null)
    {
        InstallModWindow dialog = new(suggestedName, layoutPreview)
        {
            Owner = System.Windows.Application.Current?.MainWindow
        };

        return dialog.ShowDialog() == true ? dialog.ModName : null;
    }

    public ExistingModInstallMode? PickExistingModInstallMode(string modName)
    {
        InstallModWindow dialog = InstallModWindow.CreateConflictResolutionDialog(modName);
        dialog.Owner = System.Windows.Application.Current?.MainWindow;

        return dialog.ShowDialog() == true ? dialog.ExistingModMode : null;
    }

    public IReadOnlyList<string>? PickFomodSelections(FomodInstallerInfo installer)
    {
        FomodInstallerWindow dialog = new(installer)
        {
            Owner = System.Windows.Application.Current?.MainWindow
        };

        return dialog.ShowDialog() == true ? dialog.SelectedOptionIds : null;
    }

    public string? PickSeparatorName(string suggestedName)
    {
        InstallModWindow dialog = new(
            suggestedName,
            "Новый разделитель",
            "Укажите название разделителя для визуального порядка модов.",
            "Создать")
        {
            Owner = System.Windows.Application.Current?.MainWindow
        };

        return dialog.ShowDialog() == true ? dialog.ModName : null;
    }

    public string? PickProjectName(string suggestedName)
    {
        InstallModWindow dialog = new(
            suggestedName,
            "Переименование сборки",
            "Укажите новое название сборки. Fluxora обновит папку и manifest.",
            "Переименовать")
        {
            Owner = System.Windows.Application.Current?.MainWindow
        };

        return dialog.ShowDialog() == true ? dialog.ModName : null;
    }
}
