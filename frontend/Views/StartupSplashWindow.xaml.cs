using System.Windows;
using System.Windows.Interop;
using System.Runtime.InteropServices;

namespace Fluxora.App.Views;

public partial class StartupSplashWindow : Window
{
    private const int DwmWindowCornerPreference = 33;
    private const int DwmCornerRound = 2;

    public StartupSplashWindow()
    {
        InitializeComponent();
    }

    private void OnSourceInitialized(object? sender, EventArgs e)
    {
        TryUseSystemWindowCorners();
    }

    private void TryUseSystemWindowCorners()
    {
        try
        {
            IntPtr handle = new WindowInteropHelper(this).Handle;
            int preference = DwmCornerRound;
            _ = DwmSetWindowAttribute(
                handle,
                DwmWindowCornerPreference,
                ref preference,
                Marshal.SizeOf<int>());
        }
        catch (DllNotFoundException)
        {
        }
        catch (EntryPointNotFoundException)
        {
        }
        catch (COMException)
        {
        }
    }

    [DllImport("dwmapi.dll", PreserveSig = true)]
    private static extern int DwmSetWindowAttribute(
        IntPtr hwnd,
        int attribute,
        ref int pvAttribute,
        int cbAttribute);
}
