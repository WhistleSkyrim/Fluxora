using System.IO;
using Forms = System.Windows.Forms;

namespace Fluxora.App.Services;

public sealed class FolderPickerService : IFolderPickerService
{
    public string? PickFolder(string title, string selectedPath)
    {
        using Forms.FolderBrowserDialog dialog = new()
        {
            Description = title,
            UseDescriptionForTitle = true,
            SelectedPath = Directory.Exists(selectedPath) ? selectedPath : string.Empty
        };

        return dialog.ShowDialog() == Forms.DialogResult.OK ? dialog.SelectedPath : null;
    }
}
