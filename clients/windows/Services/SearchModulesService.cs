using System.Text.Json;
using XScope.Native;

namespace XScope.Services;

internal sealed class SearchModulesService : IDisposable
{
    private readonly NativeWorkspace _workspace;

    public SearchModulesService(string? dataRoot = null)
    {
        _workspace = new NativeWorkspace(dataRoot ?? AppPaths.DataRoot);
    }

    public IReadOnlyList<SearchModuleInfo> List()
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_search_modules_list(_workspace.Handle));
        var root = doc.RootElement;
        EnsureOk(root);
        var list = new List<SearchModuleInfo>();
        if (!root.TryGetProperty("modules", out var arr) || arr.ValueKind != JsonValueKind.Array)
        {
            return list;
        }

        foreach (var m in arr.EnumerateArray())
        {
            list.Add(SearchModuleInfo.FromJson(m));
        }

        return list;
    }

    public void SetEnabled(string id, bool enabled)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_search_module_set_enabled(_workspace.Handle, id, enabled ? 1 : 0));
        EnsureOk(doc.RootElement);
    }

    public void SetApiKey(string id, string apiKey)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_search_module_set_api_key(_workspace.Handle, id, apiKey));
        EnsureOk(doc.RootElement);
    }

    public void Dispose() => _workspace.Dispose();

    private static void EnsureOk(JsonElement root)
    {
        if (root.TryGetProperty("ok", out var ok) && ok.GetBoolean())
        {
            return;
        }

        var err = root.TryGetProperty("error", out var e)
            ? e.GetString() ?? "unknown error"
            : XScopeNative.LastError() ?? "unknown error";
        throw new InvalidOperationException(err);
    }
}

internal sealed record SearchModuleInfo(
    string Id,
    string Name,
    string Description,
    bool Enabled,
    bool RequiresApiKey,
    string AuthType,
    string SecretId,
    bool SecretConfigured)
{
    public bool SupportsApiKeyEntry =>
        RequiresApiKey &&
        (string.Equals(AuthType, "bearer", StringComparison.OrdinalIgnoreCase) ||
         string.Equals(AuthType, "api_key", StringComparison.OrdinalIgnoreCase));

    public static SearchModuleInfo FromJson(JsonElement root) =>
        new(
            root.TryGetProperty("id", out var id) ? id.GetString() ?? "" : "",
            root.TryGetProperty("name", out var name) ? name.GetString() ?? "" : "",
            root.TryGetProperty("description", out var desc) ? desc.GetString() ?? "" : "",
            root.TryGetProperty("enabled", out var en) && en.GetBoolean(),
            root.TryGetProperty("requires_api_key", out var req) && req.GetBoolean(),
            root.TryGetProperty("auth_type", out var at) ? at.GetString() ?? "" : "",
            root.TryGetProperty("secret_id", out var sid) ? sid.GetString() ?? "" : "",
            root.TryGetProperty("secret_configured", out var sc) && sc.GetBoolean());
}
