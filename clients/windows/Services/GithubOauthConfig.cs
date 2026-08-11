using System.Text.Json;
using System.Text.Json.Nodes;

namespace XScope.Services;

internal static class GithubOauthConfig
{
    public static string ConfigPath =>
        System.IO.Path.Combine(AppPaths.DataRoot, "global", "github_oauth.json");

    public static string ReadClientId()
    {
        try
        {
            var env = Environment.GetEnvironmentVariable("XSCOPE_GITHUB_OAUTH_CLIENT_ID");
            if (!string.IsNullOrWhiteSpace(env))
            {
                return env.Trim();
            }

            if (!System.IO.File.Exists(ConfigPath))
            {
                return "";
            }

            using var doc = JsonDocument.Parse(System.IO.File.ReadAllText(ConfigPath));
            return doc.RootElement.TryGetProperty("client_id", out var id)
                ? id.GetString()?.Trim() ?? ""
                : "";
        }
        catch
        {
            return "";
        }
    }

    public static void SaveClientId(string clientId, string scope = "read:user")
    {
        var id = (clientId ?? "").Trim();
        if (string.IsNullOrEmpty(id))
        {
            throw new InvalidOperationException("Client ID is empty.");
        }

        System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(ConfigPath)!);

        JsonObject root;
        if (System.IO.File.Exists(ConfigPath))
        {
            root = JsonNode.Parse(System.IO.File.ReadAllText(ConfigPath)) as JsonObject ?? new JsonObject();
        }
        else
        {
            root = new JsonObject();
        }

        root["client_id"] = id;
        if (root["scope"] is null || string.IsNullOrWhiteSpace(root["scope"]?.ToString()))
        {
            root["scope"] = scope;
        }

        if (root["secret_id"] is null)
        {
            root["secret_id"] = "github.oauth";
        }

        var json = root.ToJsonString(new JsonSerializerOptions { WriteIndented = true });
        System.IO.File.WriteAllText(ConfigPath, json);
    }
}
