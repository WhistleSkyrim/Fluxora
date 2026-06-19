using System.IO;
using WpfOpenFileDialog = Microsoft.Win32.OpenFileDialog;
using WpfSaveFileDialog = Microsoft.Win32.SaveFileDialog;

namespace Fluxora.App.Services;

public sealed class FluxPackPickerService : IFluxPackPickerService
{
    private const string FluxPackFilter = "FluxPack recipes (*.fluxpack)|*.fluxpack|JSON manifests (*.json)|*.json|All files (*.*)|*.*";

    public string? PickFluxPack(string selectedDirectory)
    {
        WpfOpenFileDialog dialog = new()
        {
            Title = "Установить сборку",
            Filter = FluxPackFilter,
            CheckFileExists = true,
            Multiselect = false,
            InitialDirectory = Directory.Exists(selectedDirectory) ? selectedDirectory : string.Empty
        };

        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }

    public string? PickFluxPackSavePath(string selectedDirectory, string suggestedFileName)
    {
        WpfSaveFileDialog dialog = new()
        {
            Title = "Упаковать сборку",
            Filter = FluxPackFilter,
            AddExtension = true,
            DefaultExt = ".fluxpack",
            OverwritePrompt = true,
            FileName = SanitizeFileName(suggestedFileName),
            InitialDirectory = Directory.Exists(selectedDirectory) ? selectedDirectory : string.Empty
        };

        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }

    private static string SanitizeFileName(string fileName)
    {
        string fallback = string.IsNullOrWhiteSpace(fileName) ? "build.fluxpack" : fileName;
        foreach (char character in Path.GetInvalidFileNameChars())
        {
            fallback = fallback.Replace(character, '-');
        }

        return fallback.EndsWith(".fluxpack", StringComparison.OrdinalIgnoreCase)
            ? fallback
            : fallback + ".fluxpack";
    }
}
