namespace Fluxora.App.Services;

public interface IAppService
{
    Task InitializeAsync(CancellationToken cancellationToken = default);
}
