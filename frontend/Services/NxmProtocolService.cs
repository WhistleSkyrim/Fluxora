namespace Fluxora.App.Services;

public sealed class NxmProtocolService : IAppService
{
    private const string ProtocolName = "nxm";

    private readonly CoreBridgeService coreBridgeService;

    public NxmProtocolService(CoreBridgeService coreBridgeService)
    {
        this.coreBridgeService = coreBridgeService;
    }

    public Task InitializeAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        string executablePath = Environment.ProcessPath ?? string.Empty;
        if (!string.IsNullOrWhiteSpace(executablePath))
        {
            coreBridgeService.RegisterNxmProtocol(executablePath);
        }

        return Task.CompletedTask;
    }

    public bool RegisterCurrentUserHandler()
    {
        string executablePath = Environment.ProcessPath ?? string.Empty;
        return !string.IsNullOrWhiteSpace(executablePath) &&
            coreBridgeService.RegisterNxmProtocol(executablePath);
    }

    public static IReadOnlyList<string> ExtractNxmLinks(IEnumerable<string> arguments)
    {
        return arguments
            .Where(argument => Uri.TryCreate(argument, UriKind.Absolute, out Uri? uri) &&
                string.Equals(uri.Scheme, ProtocolName, StringComparison.OrdinalIgnoreCase))
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();
    }
}
