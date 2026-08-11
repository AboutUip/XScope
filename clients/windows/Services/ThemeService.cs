using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;
using MaterialDesignThemes.Wpf;
using Microsoft.Win32;

namespace XScope.Services;

internal enum AppThemeMode
{
    Auto = 0,
    Light = 1,
    Dark = 2,
}

/// <summary>UI theme preference + MaterialDesign / semantic brush application (X-style dark).</summary>
internal static class ThemeService
{
    private static readonly object Gate = new();
    private static bool _listening;
    private static AppThemeMode _preference = AppThemeMode.Auto;
    private static bool _effectiveDark;

    public static event Action? ThemeChanged;

    public static AppThemeMode Preference
    {
        get
        {
            lock (Gate)
            {
                return _preference;
            }
        }
    }

    public static bool IsDarkEffective
    {
        get
        {
            lock (Gate)
            {
                return _effectiveDark;
            }
        }
    }

    public static void Initialize()
    {
        _preference = UiLanguageConfig.ReadTheme();
        Apply(_preference, save: false);
        EnsureSystemListener();
    }

    public static void SetPreference(AppThemeMode mode)
    {
        Apply(mode, save: true);
    }

    public static void Apply(AppThemeMode mode, bool save)
    {
        lock (Gate)
        {
            _preference = mode;
            _effectiveDark = mode switch
            {
                AppThemeMode.Light => false,
                AppThemeMode.Dark => true,
                _ => !IsSystemLightTheme(),
            };
        }

        if (save)
        {
            UiLanguageConfig.SaveTheme(mode);
        }

        var app = Application.Current;
        if (app is null)
        {
            return;
        }

        void ApplyOnUi()
        {
            ApplyMaterialDesign(IsDarkEffective);
            ApplySemanticBrushes(IsDarkEffective);
            ThemeChanged?.Invoke();
        }

        if (app.Dispatcher.CheckAccess())
        {
            ApplyOnUi();
        }
        else
        {
            app.Dispatcher.Invoke(ApplyOnUi);
        }
    }

    private static void EnsureSystemListener()
    {
        if (_listening)
        {
            return;
        }

        _listening = true;
        SystemEvents.UserPreferenceChanged += (_, e) =>
        {
            if (e.Category != UserPreferenceCategory.General &&
                e.Category != UserPreferenceCategory.Color)
            {
                return;
            }

            if (Preference != AppThemeMode.Auto)
            {
                return;
            }

            Apply(AppThemeMode.Auto, save: false);
        };
    }

    private static bool IsSystemLightTheme()
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(
                @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
            var value = key?.GetValue("AppsUseLightTheme");
            if (value is int i)
            {
                return i != 0;
            }
        }
        catch
        {
            // fall through
        }

        return true;
    }

    private static void ApplyMaterialDesign(bool dark)
    {
        try
        {
            var helper = new PaletteHelper();
            var theme = helper.GetTheme();
            theme.SetBaseTheme(dark ? BaseTheme.Dark : BaseTheme.Light);
            // X accent blue
            theme.SetPrimaryColor(Color.FromRgb(0x1D, 0x9B, 0xF0));
            theme.SetSecondaryColor(Color.FromRgb(0x71, 0x76, 0x7B));
            helper.SetTheme(theme);
        }
        catch
        {
            // Designer / early boot may lack MD dictionaries.
        }
    }

    private static void ApplySemanticBrushes(bool dark)
    {
        var app = Application.Current;
        if (app?.Resources is null)
        {
            return;
        }

        // X-style dark: near-black canvas, elevated charcoal, readable secondary ink, #1D9BF0 accent.
        if (dark)
        {
            SetBrush(app, "XScopeWindowBg", Color.FromRgb(0x0F, 0x14, 0x19));
            SetBrush(app, "XScopeSurface", Color.FromRgb(0x16, 0x18, 0x1C));
            SetBrush(app, "XScopeSurfaceAlt", Color.FromRgb(0x1E, 0x20, 0x24));
            SetBrush(app, "XScopeInputBg", Color.FromRgb(0x22, 0x26, 0x2A));
            SetBrush(app, "XScopeHover", Color.FromRgb(0x2F, 0x33, 0x38));
            SetBrush(app, "XScopeBorder", Color.FromRgb(0x38, 0x44, 0x4D));
            SetBrush(app, "XScopeTextPrimary", Color.FromRgb(0xE7, 0xE9, 0xEA));
            // Keep secondary/muted above ~4.5:1 on Surface so settings captions stay readable.
            SetBrush(app, "XScopeTextSecondary", Color.FromRgb(0xB0, 0xB8, 0xC0));
            SetBrush(app, "XScopeTextMuted", Color.FromRgb(0x8B, 0x98, 0xA5));
            SetBrush(app, "XScopeAccent", Color.FromRgb(0x1D, 0x9B, 0xF0));
            SetBrush(app, "XScopeAccentSoft", Color.FromRgb(0x00, 0x2A, 0x43));
            SetBrush(app, "XScopeAccentText", Color.FromRgb(0x1D, 0x9B, 0xF0));
            SetBrush(app, "XScopeScrollThumb", Color.FromRgb(0x53, 0x56, 0x5B));
            SetBrush(app, "XScopeScrollThumbHover", Color.FromRgb(0x71, 0x76, 0x7B));
            SetBrush(app, "SplashBgBrush", Color.FromRgb(0x0F, 0x14, 0x19));
            SetBrush(app, "SplashInkBrush", Color.FromRgb(0xE7, 0xE9, 0xEA));
        }
        else
        {
            SetBrush(app, "XScopeWindowBg", Color.FromRgb(0xFF, 0xFF, 0xFF));
            SetBrush(app, "XScopeSurface", Color.FromRgb(0xF8, 0xF9, 0xFA));
            SetBrush(app, "XScopeSurfaceAlt", Color.FromRgb(0xFF, 0xFF, 0xFF));
            SetBrush(app, "XScopeInputBg", Color.FromRgb(0xFF, 0xFF, 0xFF));
            SetBrush(app, "XScopeHover", Color.FromRgb(0xF1, 0xF3, 0xF4));
            SetBrush(app, "XScopeBorder", Color.FromRgb(0xE8, 0xEA, 0xED));
            SetBrush(app, "XScopeTextPrimary", Color.FromRgb(0x20, 0x21, 0x24));
            SetBrush(app, "XScopeTextSecondary", Color.FromRgb(0x5F, 0x63, 0x68));
            SetBrush(app, "XScopeTextMuted", Color.FromRgb(0x9A, 0xA0, 0xA6));
            SetBrush(app, "XScopeAccent", Color.FromRgb(0x1A, 0x73, 0xE8));
            SetBrush(app, "XScopeAccentSoft", Color.FromRgb(0xE8, 0xF0, 0xFE));
            SetBrush(app, "XScopeAccentText", Color.FromRgb(0x19, 0x67, 0xD2));
            SetBrush(app, "XScopeScrollThumb", Color.FromRgb(0xC5, 0xCA, 0xD1));
            SetBrush(app, "XScopeScrollThumbHover", Color.FromRgb(0x9A, 0xA0, 0xA6));
            SetBrush(app, "SplashBgBrush", Color.FromRgb(0xEE, 0xF4, 0xF7));
            SetBrush(app, "SplashInkBrush", Color.FromRgb(0x0F, 0x2E, 0x3A));
        }
    }

    private static void SetBrush(Application app, string key, Color color)
    {
        var brush = new SolidColorBrush(color);
        brush.Freeze();
        app.Resources[key] = brush;
    }

    public static string ToCode(AppThemeMode mode) => mode switch
    {
        AppThemeMode.Light => "light",
        AppThemeMode.Dark => "dark",
        _ => "auto",
    };

    public static AppThemeMode Parse(string? code) => code?.Trim().ToLowerInvariant() switch
    {
        "light" or "day" => AppThemeMode.Light,
        "dark" or "night" or "x" => AppThemeMode.Dark,
        _ => AppThemeMode.Auto,
    };
}
