using Fluxora.App.Models;

namespace Fluxora.App.Services;

/// <summary>
/// Shows a modal confirmation dialog and reports whether the user confirmed. Replaces ad-hoc
/// <c>MessageBox</c> calls so the app keeps a single, themed confirmation surface.
/// </summary>
public interface IConfirmDialogService
{
    bool Confirm(ConfirmDialogOptions options);
}
