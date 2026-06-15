using System.IO;
using WpfOpenFileDialog = Microsoft.Win32.OpenFileDialog;

namespace Fluxora.App.Services;

public sealed class ExecutablePickerService : IExecutablePickerService
{
    public string? PickExecutable(string title, string selectedPath)
    {
        WpfOpenFileDialog dialog = new()
        {
            Title = title,
            Filter = "Executable files (*.exe)|*.exe|All files (*.*)|*.*",
            CheckFileExists = true,
            Multiselect = false,
            InitialDirectory = ResolveInitialDirectory(selectedPath)
        };

        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }

    private static string ResolveInitialDirectory(string selectedPath)
    {
        if (File.Exists(selectedPath))
        {
            return Path.GetDirectoryName(selectedPath) ?? string.Empty;
        }

        return Directory.Exists(selectedPath) ? selectedPath : string.Empty;
    }
}
