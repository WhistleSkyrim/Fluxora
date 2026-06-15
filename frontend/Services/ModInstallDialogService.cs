namespace Fluxora.App.Services;

public sealed class ModInstallDialogService : IModInstallDialogService
{
    public string? PickModName(string suggestedName)
    {
        InstallModWindow dialog = new(suggestedName)
        {
            Owner = System.Windows.Application.Current?.MainWindow
        };

        return dialog.ShowDialog() == true ? dialog.ModName : null;
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
