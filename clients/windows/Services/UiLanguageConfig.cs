using System.IO;
using System.Text.Json;

namespace XScope.Services;

internal enum AppLanguage
{
    English,
    ChineseSimplified,
}

/// <summary>Persisted UI prefs under data_root/global/ui.json (language + last AI provider).</summary>
internal static class UiLanguageConfig
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    public static string ConfigPath =>
        Path.Combine(AppPaths.DataRoot, "global", "ui.json");

    public static AppLanguage Default => AppLanguage.English;

    public static AppLanguage Read()
    {
        try
        {
            if (!File.Exists(ConfigPath))
            {
                return Default;
            }

            using var doc = JsonDocument.Parse(File.ReadAllText(ConfigPath));
            if (doc.RootElement.TryGetProperty("language", out var lang))
            {
                return Parse(lang.GetString());
            }
        }
        catch
        {
            // fall through
        }

        return Default;
    }

    public static AppThemeMode ReadTheme()
    {
        try
        {
            if (!File.Exists(ConfigPath))
            {
                return AppThemeMode.Auto;
            }

            using var doc = JsonDocument.Parse(File.ReadAllText(ConfigPath));
            if (doc.RootElement.TryGetProperty("theme", out var theme))
            {
                return ThemeService.Parse(theme.GetString());
            }
        }
        catch
        {
        }

        return AppThemeMode.Auto;
    }

    public static string? ReadPreferredProviderId()
    {
        try
        {
            if (!File.Exists(ConfigPath))
            {
                return null;
            }

            using var doc = JsonDocument.Parse(File.ReadAllText(ConfigPath));
            if (doc.RootElement.TryGetProperty("preferred_provider_id", out var id))
            {
                var s = id.GetString();
                return string.IsNullOrWhiteSpace(s) ? null : s.Trim();
            }
        }
        catch
        {
        }

        return null;
    }

    public static void Save(AppLanguage language)
    {
        SaveAll(language, ReadPreferredProviderId(), ReadTheme());
    }

    public static void SavePreferredProviderId(string? providerId)
    {
        SaveAll(Read(), string.IsNullOrWhiteSpace(providerId) ? null : providerId.Trim(), ReadTheme());
    }

    public static void SaveTheme(AppThemeMode theme)
    {
        SaveAll(Read(), ReadPreferredProviderId(), theme);
    }

    public static void Save(AppLanguage language, string? preferredProviderId)
    {
        SaveAll(language, preferredProviderId, ReadTheme());
    }

    private static void SaveAll(AppLanguage language, string? preferredProviderId, AppThemeMode theme)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(ConfigPath)!);
        var payload = new Dictionary<string, object?>
        {
            ["language"] = ToCode(language),
            ["theme"] = ThemeService.ToCode(theme),
        };
        if (!string.IsNullOrWhiteSpace(preferredProviderId))
        {
            payload["preferred_provider_id"] = preferredProviderId;
        }

        File.WriteAllText(ConfigPath, JsonSerializer.Serialize(payload, JsonOptions));
    }

    public static string ToCode(AppLanguage language) => language switch
    {
        AppLanguage.ChineseSimplified => "zh-Hans",
        _ => "en",
    };

    public static AppLanguage Parse(string? code) => code?.Trim().ToLowerInvariant() switch
    {
        "zh" or "zh-cn" or "zh-hans" or "zh_hans" or "cn" => AppLanguage.ChineseSimplified,
        _ => AppLanguage.English,
    };
}
