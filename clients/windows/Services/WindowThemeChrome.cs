using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using XScope.Services;

namespace XScope;

/// <summary>Enables Windows immersive dark title bars / border colors to match in-app theme.</summary>
internal static class WindowThemeChrome
{
    private const int DwmwaUseImmersiveDarkMode = 20;
    private const int DwmwaUseImmersiveDarkModeBefore20H1 = 19;
    private const int DwmwaBorderColor = 34;
    private const int DwmwaCaptionColor = 35;
    private const uint DwmwaColorNone = 0xFFFFFFFE;

    [DllImport("dwmapi.dll", PreserveSig = true)]
    private static extern int DwmSetWindowAttribute(
        IntPtr hwnd, int attr, ref int attrValue, int attrSize);

    [DllImport("dwmapi.dll", PreserveSig = true)]
    private static extern int DwmSetWindowAttribute(
        IntPtr hwnd, int attr, ref uint attrValue, int attrSize);

    public static void Attach(Window window)
    {
        void Apply() => ApplyTo(window, ThemeService.IsDarkEffective);

        if (window.IsLoaded)
        {
            Apply();
        }
        else
        {
            window.SourceInitialized += (_, _) => Apply();
        }

        void OnThemeChanged()
        {
            if (!window.Dispatcher.CheckAccess())
            {
                window.Dispatcher.BeginInvoke(Apply);
                return;
            }

            Apply();
        }

        ThemeService.ThemeChanged += OnThemeChanged;
        window.Closed += (_, _) => ThemeService.ThemeChanged -= OnThemeChanged;
    }

    private static void ApplyTo(Window window, bool dark)
    {
        var helper = new WindowInteropHelper(window);
        var hwnd = helper.Handle;
        if (hwnd == IntPtr.Zero)
        {
            return;
        }

        var useDark = dark ? 1 : 0;
        if (DwmSetWindowAttribute(hwnd, DwmwaUseImmersiveDarkMode, ref useDark, sizeof(int)) != 0)
        {
            DwmSetWindowAttribute(hwnd, DwmwaUseImmersiveDarkModeBefore20H1, ref useDark, sizeof(int));
        }

        // Win11: replace the default pure-black window outline with our surface color.
        // COLORREF is 0x00BBGGRR.
        var border = ToColorRef(ResolveColor(
            dark ? "XScopeSurfaceAlt" : "XScopeWindowBg",
            dark ? Color.FromRgb(0x1E, 0x20, 0x24) : Colors.White));
        var caption = ToColorRef(ResolveColor(
            dark ? "XScopeSurface" : "XScopeWindowBg",
            dark ? Color.FromRgb(0x16, 0x18, 0x1C) : Colors.White));

        if (DwmSetWindowAttribute(hwnd, DwmwaBorderColor, ref border, sizeof(uint)) != 0)
        {
            var none = DwmwaColorNone;
            DwmSetWindowAttribute(hwnd, DwmwaBorderColor, ref none, sizeof(uint));
        }

        DwmSetWindowAttribute(hwnd, DwmwaCaptionColor, ref caption, sizeof(uint));
    }

    private static Color ResolveColor(string key, Color fallback)
    {
        if (Application.Current?.TryFindResource(key) is SolidColorBrush brush)
        {
            return brush.Color;
        }

        return fallback;
    }

    private static uint ToColorRef(Color c) =>
        (uint)(c.R | (c.G << 8) | (c.B << 16));
}
