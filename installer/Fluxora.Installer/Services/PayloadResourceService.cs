using System.IO;
using System.Reflection;

namespace Fluxora.Installer.Services;

public sealed class PayloadResourceService
{
    private const string PayloadResourceSuffix = ".FluxoraPayload.flxpkg";

    public string ExtractPayloadToTemp()
    {
        Assembly assembly = Assembly.GetExecutingAssembly();
        string? resourceName = assembly
            .GetManifestResourceNames()
            .FirstOrDefault(name => name.EndsWith(PayloadResourceSuffix, StringComparison.OrdinalIgnoreCase));

        if (resourceName is null)
        {
            throw new InvalidOperationException("Fluxora installer payload was not embedded. Run Build.ps1 to create output-installer.");
        }

        string directory = Path.Combine(Path.GetTempPath(), "Fluxora", "installer", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        string packagePath = Path.Combine(directory, "FluxoraPayload.flxpkg");

        using Stream? resource = assembly.GetManifestResourceStream(resourceName);
        if (resource is null)
        {
            throw new InvalidOperationException("Fluxora installer payload could not be opened.");
        }

        using FileStream output = new(packagePath, FileMode.CreateNew, FileAccess.Write, FileShare.Read);
        resource.CopyTo(output);
        return packagePath;
    }

    public void TryDeletePayload(string packagePath)
    {
        try
        {
            if (!string.IsNullOrWhiteSpace(packagePath) && File.Exists(packagePath))
            {
                string? directory = Path.GetDirectoryName(packagePath);
                File.Delete(packagePath);
                if (!string.IsNullOrWhiteSpace(directory) && Directory.Exists(directory))
                {
                    Directory.Delete(directory, recursive: true);
                }
            }
        }
        catch
        {
        }
    }
}
