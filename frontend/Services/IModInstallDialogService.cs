namespace Fluxora.App.Services;

public interface IModInstallDialogService
{
    string? PickModName(string suggestedName);
    string? PickSeparatorName(string suggestedName);
    string? PickProjectName(string suggestedName);
}
