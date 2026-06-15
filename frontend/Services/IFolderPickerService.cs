namespace Fluxora.App.Services;

public interface IFolderPickerService
{
    string? PickFolder(string title, string selectedPath);
}
