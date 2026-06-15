using System.IO;
using WpfOpenFileDialog = Microsoft.Win32.OpenFileDialog;

namespace Fluxora.App.Services;

public sealed class BuildConfigPickerService : IBuildConfigPickerService
{
    public string? PickBuildConfig(string selectedDirectory)
    {
        WpfOpenFileDialog dialog = new()
        {
            Title = "Открыть конфиг сборки",
            Filter = "Fluxora build configs (*.json)|*.json|All files (*.*)|*.*",
            CheckFileExists = true,
            Multiselect = false,
            InitialDirectory = Directory.Exists(selectedDirectory) ? selectedDirectory : string.Empty
        };

        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }
}
