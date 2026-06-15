using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using Fluxora.Installer.Models;

namespace Fluxora.Installer.Services;

public sealed class InstallerNativeBridge
{
    private const int NativeBufferLength = 32768;

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    public bool IsAvailable()
    {
        try
        {
            return NativeMethods.IsAvailable() == 1;
        }
        catch (Exception exception) when (IsNativeLoadException(exception))
        {
            return false;
        }
    }

    public string ValidateInstallDirectory(string installDirectory)
    {
        StringBuilder message = new(NativeBufferLength);
        int result = NativeMethods.ValidateInstallDirectory(
            installDirectory,
            message,
            message.Capacity);
        if (result == NativeResult.Ok)
        {
            return message.ToString();
        }

        throw new InvalidOperationException(ReadLastError(result));
    }

    public Task<InstallerResult> InstallPackageAsync(
        string packagePath,
        string installDirectory,
        bool createDesktopShortcut,
        Action<InstallerProgress>? progress,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        return Task.Run(
            () =>
            {
                cancellationToken.ThrowIfCancellationRequested();

                NativeMethods.NativeProgressCallback? callback = null;
                if (progress is not null)
                {
                    callback = (json, _) =>
                    {
                        if (string.IsNullOrWhiteSpace(json))
                        {
                            return;
                        }

                        try
                        {
                            InstallerProgress? update =
                                JsonSerializer.Deserialize<InstallerProgress>(json, JsonOptions);
                            if (update is not null)
                            {
                                progress(update);
                            }
                        }
                        catch
                        {
                        }
                    };
                }

                StringBuilder jsonBuffer = new(NativeBufferLength);
                int result = NativeMethods.InstallPackage(
                    packagePath,
                    installDirectory,
                    createDesktopShortcut ? 1 : 0,
                    callback,
                    IntPtr.Zero,
                    jsonBuffer,
                    jsonBuffer.Capacity);
                GC.KeepAlive(callback);

                if (result != NativeResult.Ok)
                {
                    throw new InvalidOperationException(ReadLastError(result));
                }

                return JsonSerializer.Deserialize<InstallerResult>(jsonBuffer.ToString(), JsonOptions)
                    ?? throw new InvalidOperationException("Native installer returned an empty result.");
            },
            cancellationToken);
    }

    private static string ReadLastError(int result)
    {
        StringBuilder message = new(2048);
        int errorResult = NativeMethods.GetLastError(message, message.Capacity);
        if (errorResult == NativeResult.Ok && message.Length > 0)
        {
            return message.ToString();
        }

        return $"Native installer returned error code {result}.";
    }

    private static bool IsNativeLoadException(Exception exception)
    {
        return exception is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException;
    }

    private static class NativeResult
    {
        public const int Ok = 0;
    }

    private static class NativeMethods
    {
        [DllImport("FluxoraInstallerCore", EntryPoint = "fluxora_installer_is_available", CallingConvention = CallingConvention.Cdecl)]
        public static extern int IsAvailable();

        [DllImport("FluxoraInstallerCore", EntryPoint = "fluxora_installer_validate_install_directory", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int ValidateInstallDirectory(
            string installDirectory,
            StringBuilder messageBuffer,
            int messageBufferLength);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public delegate void NativeProgressCallback(
            [MarshalAs(UnmanagedType.LPWStr)] string? progressJson,
            IntPtr userData);

        [DllImport("FluxoraInstallerCore", EntryPoint = "fluxora_installer_install_package", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int InstallPackage(
            string packagePath,
            string installDirectory,
            int createDesktopShortcut,
            NativeProgressCallback? progressCallback,
            IntPtr progressUserData,
            StringBuilder jsonBuffer,
            int jsonBufferLength);

        [DllImport("FluxoraInstallerCore", EntryPoint = "fluxora_installer_get_last_error", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        public static extern int GetLastError(
            StringBuilder messageBuffer,
            int messageBufferLength);
    }
}
