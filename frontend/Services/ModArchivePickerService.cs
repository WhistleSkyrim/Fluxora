using System.IO;
using WpfOpenFileDialog = Microsoft.Win32.OpenFileDialog;

namespace Fluxora.App.Services;

public sealed class ModArchivePickerService : IModArchivePickerService
{
    public string? PickArchive(string selectedDirectory)
    {
        WpfOpenFileDialog dialog = new()
        {
            Title = "Добавить загрузку",
            Filter = "Mod archives (*.zip;*.7z;*.rar;*.fomod;*.omod;*.tar;*.tgz;*.gz;*.bz2;*.xz;*.zst;*.ba2;*.bsa)|*.zip;*.7z;*.rar;*.fomod;*.omod;*.tar;*.tgz;*.gz;*.bz2;*.xz;*.zst;*.ba2;*.bsa|All files (*.*)|*.*",
            CheckFileExists = true,
            Multiselect = false,
            InitialDirectory = Directory.Exists(selectedDirectory) ? selectedDirectory : string.Empty
        };

        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }
}
