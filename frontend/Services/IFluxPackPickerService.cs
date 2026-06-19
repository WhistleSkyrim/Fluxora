namespace Fluxora.App.Services;

public interface IFluxPackPickerService
{
    string? PickFluxPack(string selectedDirectory);

    string? PickFluxPackSavePath(string selectedDirectory, string suggestedFileName);
}
