using Fluxora.App.Models;

namespace Fluxora.App.Services;

public interface IExecutableManagerDialogService
{
    IReadOnlyList<GameExecutableEntry>? EditExecutables(
        IReadOnlyList<GameExecutableEntry> executables,
        string gamePath,
        string projectDirectory);
}
