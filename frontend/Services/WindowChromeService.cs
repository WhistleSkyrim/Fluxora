using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;

namespace Fluxora.App.Services;

/// <summary>
/// Restores native Windows behaviour to a borderless (<c>WindowStyle="None"</c> + WindowChrome)
/// window. Such windows otherwise lose three things users expect:
///   * the minimize / maximize / restore animations,
///   * a maximized size that stops at the monitor work area (instead of spilling a few pixels
///     past every screen edge and over the taskbar),
///   * the Windows 11 rounded corners.
///
/// Attach one instance per window in its constructor; it hooks itself up on SourceInitialized.
/// </summary>
public sealed class WindowChromeService
{
    private readonly Window window;

    public WindowChromeService(Window window)
    {
        this.window = window ?? throw new ArgumentNullException(nameof(window));
    }

    /// <summary>Begins managing the window. Safe to call from the window constructor.</summary>
    public void Attach()
    {
        window.Loaded += OnLoaded;
        window.StateChanged += OnWindowStateChanged;

        if (window.IsInitialized)
        {
            OnSourceInitialized(window, EventArgs.Empty);
        }
        else
        {
            window.SourceInitialized += OnSourceInitialized;
        }
    }

    private void OnSourceInitialized(object? sender, EventArgs e)
    {
        IntPtr handle = new WindowInteropHelper(window).Handle;
        if (handle == IntPtr.Zero)
        {
            return;
        }

        EnableNativeWindowStyles(handle, window.ResizeMode);
        ApplyDwmTheme(handle);

        HwndSource? source = HwndSource.FromHwnd(handle);
        source?.AddHook(WindowProc);
        if (source is not null)
        {
            ApplyWindowBounds(handle, source);
        }
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        IntPtr handle = new WindowInteropHelper(window).Handle;
        HwndSource? source = handle == IntPtr.Zero ? null : HwndSource.FromHwnd(handle);
        if (source is not null)
        {
            ApplyWindowBounds(handle, source);
        }
    }

    private void OnWindowStateChanged(object? sender, EventArgs e)
    {
        ApplyWindowBounds();

        // WPF/WindowChrome may apply its own maximized placement after StateChanged. Reapply on
        // the dispatcher so the final native bounds still match the monitor work area.
        if (window.WindowState == WindowState.Maximized)
        {
            window.Dispatcher.BeginInvoke(ApplyWindowBounds, DispatcherPriority.Loaded);
        }
    }

    private void ApplyWindowBounds()
    {
        IntPtr handle = new WindowInteropHelper(window).Handle;
        HwndSource? source = handle == IntPtr.Zero ? null : HwndSource.FromHwnd(handle);
        if (source is not null)
        {
            ApplyWindowBounds(handle, source);
        }
    }

    private void ApplyWindowBounds(IntPtr handle, HwndSource source)
    {
        EnableNativeWindowStyles(handle, window.ResizeMode);
        ApplyDwmTheme(handle);

        if (window.WindowState == WindowState.Maximized)
        {
            ApplyMaximizedWorkArea(handle);
            return;
        }

        FitNormalWindowToWorkArea(handle, source);
    }

    /// <summary>
    /// Keep the native frame styles even though WPF draws the title bar itself. The shell uses
    /// these bits for minimize/maximize animations, Aero Snap and Windows 11 DWM corners.
    /// </summary>
    private static void EnableNativeWindowStyles(IntPtr handle, ResizeMode resizeMode)
    {
        int style = GetWindowLong(handle, GWL_STYLE);
        style &= ~WS_POPUP;
        style |= WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

        if (resizeMode is ResizeMode.CanResize or ResizeMode.CanResizeWithGrip)
        {
            style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
        }
        else
        {
            style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        }

        SetWindowLong(handle, GWL_STYLE, style);
        SetWindowPos(
            handle,
            IntPtr.Zero,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    private static void ApplyDwmTheme(IntPtr handle)
    {
        ApplyRoundedCorners(handle);
        ApplyDarkCaption(handle);
        ApplyCaptionColors(handle);
    }

    private static void ApplyRoundedCorners(IntPtr handle)
    {
        int preference = DwmWindowCornerPreferenceRound;
        _ = DwmSetWindowAttribute(handle, DwmWindowCornerPreferenceAttribute, ref preference, sizeof(int));
    }

    private static void ApplyDarkCaption(IntPtr handle)
    {
        int enabled = 1;
        int result = DwmSetWindowAttribute(
            handle,
            DwmUseImmersiveDarkModeAttribute,
            ref enabled,
            sizeof(int));

        if (result != 0)
        {
            _ = DwmSetWindowAttribute(
                handle,
                DwmUseImmersiveDarkModeBefore20H1Attribute,
                ref enabled,
                sizeof(int));
        }
    }

    private static void ApplyCaptionColors(IntPtr handle)
    {
        int borderColor = ToColorRef(0x21, 0x1C, 0x33);
        int captionColor = ToColorRef(0x0B, 0x09, 0x13);
        int textColor = ToColorRef(0xF7, 0xF4, 0xFF);

        _ = DwmSetWindowAttribute(handle, DwmWindowBorderColorAttribute, ref borderColor, sizeof(int));
        _ = DwmSetWindowAttribute(handle, DwmWindowCaptionColorAttribute, ref captionColor, sizeof(int));
        _ = DwmSetWindowAttribute(handle, DwmWindowTextColorAttribute, ref textColor, sizeof(int));
    }

    private static int ToColorRef(int red, int green, int blue)
    {
        return red | (green << 8) | (blue << 16);
    }

    private IntPtr WindowProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == WM_GETMINMAXINFO)
        {
            ConstrainMaximizedBounds(hwnd, lParam);
            handled = true;
        }

        return IntPtr.Zero;
    }

    /// <summary>
    /// Clamp the maximized placement to the work area of the monitor the window currently sits on
    /// so the window does not overflow the screen edges or cover the taskbar.
    /// </summary>
    private static void ConstrainMaximizedBounds(IntPtr hwnd, IntPtr lParam)
    {
        IntPtr monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (monitor == IntPtr.Zero)
        {
            return;
        }

        MONITORINFO monitorInfo = new() { cbSize = Marshal.SizeOf<MONITORINFO>() };
        if (!GetMonitorInfo(monitor, ref monitorInfo))
        {
            return;
        }

        RECT work = monitorInfo.rcWork;
        RECT bounds = monitorInfo.rcMonitor;

        MINMAXINFO info = Marshal.PtrToStructure<MINMAXINFO>(lParam);
        info.ptMaxPosition.X = work.Left - bounds.Left;
        info.ptMaxPosition.Y = work.Top - bounds.Top;
        info.ptMaxSize.X = work.Right - work.Left;
        info.ptMaxSize.Y = work.Bottom - work.Top;
        info.ptMaxTrackSize.X = work.Right - work.Left;
        info.ptMaxTrackSize.Y = work.Bottom - work.Top;
        Marshal.StructureToPtr(info, lParam, true);
    }

    private static void ApplyMaximizedWorkArea(IntPtr hwnd)
    {
        IntPtr monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (monitor == IntPtr.Zero)
        {
            return;
        }

        MONITORINFO monitorInfo = new() { cbSize = Marshal.SizeOf<MONITORINFO>() };
        if (!GetMonitorInfo(monitor, ref monitorInfo))
        {
            return;
        }

        RECT work = monitorInfo.rcWork;
        SetWindowPos(
            hwnd,
            IntPtr.Zero,
            work.Left,
            work.Top,
            work.Right - work.Left,
            work.Bottom - work.Top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    private void FitNormalWindowToWorkArea(IntPtr hwnd, HwndSource source)
    {
        if (window.WindowState != WindowState.Normal)
        {
            return;
        }

        IntPtr monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (monitor == IntPtr.Zero)
        {
            return;
        }

        MONITORINFO monitorInfo = new() { cbSize = Marshal.SizeOf<MONITORINFO>() };
        if (!GetMonitorInfo(monitor, ref monitorInfo))
        {
            return;
        }

        Matrix fromDevice = source.CompositionTarget.TransformFromDevice;
        RECT workPixels = monitorInfo.rcWork;
        System.Windows.Point workTopLeft = fromDevice.Transform(new System.Windows.Point(workPixels.Left, workPixels.Top));
        System.Windows.Point workBottomRight = fromDevice.Transform(new System.Windows.Point(workPixels.Right, workPixels.Bottom));
        double workWidth = Math.Max(0, workBottomRight.X - workTopLeft.X);
        double workHeight = Math.Max(0, workBottomRight.Y - workTopLeft.Y);

        if (workWidth <= 0 || workHeight <= 0)
        {
            return;
        }

        if (!double.IsNaN(window.Width) && workWidth >= window.MinWidth && window.Width > workWidth)
        {
            window.Width = workWidth;
        }

        if (!double.IsNaN(window.Height) && workHeight >= window.MinHeight && window.Height > workHeight)
        {
            window.Height = workHeight;
        }

        double width = window.ActualWidth > 0
            ? window.ActualWidth
            : double.IsNaN(window.Width) ? window.MinWidth : window.Width;
        double height = window.ActualHeight > 0
            ? window.ActualHeight
            : double.IsNaN(window.Height) ? window.MinHeight : window.Height;

        if (!double.IsNaN(window.Left))
        {
            if (window.Left + width > workBottomRight.X)
            {
                window.Left = Math.Max(workTopLeft.X, workBottomRight.X - width);
            }

            if (window.Left < workTopLeft.X)
            {
                window.Left = workTopLeft.X;
            }
        }

        if (!double.IsNaN(window.Top))
        {
            if (window.Top + height > workBottomRight.Y)
            {
                window.Top = Math.Max(workTopLeft.Y, workBottomRight.Y - height);
            }

            if (window.Top < workTopLeft.Y)
            {
                window.Top = workTopLeft.Y;
            }
        }
    }

    private const int GWL_STYLE = -16;
    private const int WS_POPUP = unchecked((int)0x80000000);
    private const int WS_CAPTION = 0x00C00000;
    private const int WS_SYSMENU = 0x00080000;
    private const int WS_THICKFRAME = 0x00040000;
    private const int WS_MINIMIZEBOX = 0x00020000;
    private const int WS_MAXIMIZEBOX = 0x00010000;
    private const int WM_GETMINMAXINFO = 0x0024;
    private const int MONITOR_DEFAULTTONEAREST = 0x00000002;
    private const int DwmUseImmersiveDarkModeBefore20H1Attribute = 19;
    private const int DwmUseImmersiveDarkModeAttribute = 20;
    private const int DwmWindowCornerPreferenceAttribute = 33;
    private const int DwmWindowBorderColorAttribute = 34;
    private const int DwmWindowCaptionColorAttribute = 35;
    private const int DwmWindowTextColorAttribute = 36;
    private const int DwmWindowCornerPreferenceRound = 2;
    private const uint SWP_NOSIZE = 0x0001;
    private const uint SWP_NOMOVE = 0x0002;
    private const uint SWP_NOZORDER = 0x0004;
    private const uint SWP_NOACTIVATE = 0x0010;
    private const uint SWP_FRAMECHANGED = 0x0020;

    [DllImport("user32.dll")]
    private static extern int GetWindowLong(IntPtr hWnd, int nIndex);

    [DllImport("user32.dll")]
    private static extern int SetWindowLong(IntPtr hWnd, int nIndex, int dwNewLong);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetWindowPos(
        IntPtr hWnd,
        IntPtr hWndInsertAfter,
        int X,
        int Y,
        int cx,
        int cy,
        uint uFlags);

    [DllImport("user32.dll")]
    private static extern IntPtr MonitorFromWindow(IntPtr hwnd, int dwFlags);

    [DllImport("user32.dll")]
    private static extern bool GetMonitorInfo(IntPtr hMonitor, ref MONITORINFO lpmi);

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int dwAttribute, ref int pvAttribute, int cbAttribute);

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT
    {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MINMAXINFO
    {
        public POINT ptReserved;
        public POINT ptMaxSize;
        public POINT ptMaxPosition;
        public POINT ptMinTrackSize;
        public POINT ptMaxTrackSize;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MONITORINFO
    {
        public int cbSize;
        public RECT rcMonitor;
        public RECT rcWork;
        public uint dwFlags;
    }
}
