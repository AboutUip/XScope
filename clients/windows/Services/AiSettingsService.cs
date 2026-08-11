using System.Text.Json;
using XScope.Native;

namespace XScope.Services;

internal sealed class AiSettingsService : IDisposable
{
    private readonly NativeWorkspace _workspace;

    public AiSettingsService(string? dataRoot = null)
    {
        _workspace = new NativeWorkspace(dataRoot ?? AppPaths.DataRoot);
    }

    public IReadOnlyList<AiProviderStatus> ListProviders()
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_ai_provider_status(_workspace.Handle));
        var root = doc.RootElement;
        EnsureOk(root);
        var list = new List<AiProviderStatus>();
        if (!root.TryGetProperty("providers", out var arr) || arr.ValueKind != JsonValueKind.Array)
        {
            return list;
        }

        foreach (var p in arr.EnumerateArray())
        {
            list.Add(AiProviderStatus.FromJson(p));
        }

        return list;
    }

    public AiModelsResult SetApiKey(string providerId, string apiKey)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_ai_set_api_key(_workspace.Handle, providerId, apiKey));
        var root = doc.RootElement;
        EnsureOk(root);
        return AiModelsResult.FromJson(root);
    }

    public AiModelsResult RefreshModels(string providerId)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_ai_refresh_models(_workspace.Handle, providerId));
        var root = doc.RootElement;
        EnsureOk(root);
        return AiModelsResult.FromJson(root);
    }

    public void SetPreferredModel(string providerId, string modelId)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_ai_set_preferred_model(_workspace.Handle, providerId, modelId));
        EnsureOk(doc.RootElement);
    }

    public IReadOnlyList<string> SetModelCapabilities(string providerId, IEnumerable<string> capabilities)
    {
        var json = JsonSerializer.Serialize(capabilities.ToArray());
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_ai_set_model_capabilities(_workspace.Handle, providerId, json));
        var root = doc.RootElement;
        EnsureOk(root);
        return ReadStringArray(root, "model_capabilities");
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

    internal static IReadOnlyList<string> ReadStringArray(JsonElement root, string name)
    {
        var list = new List<string>();
        if (!root.TryGetProperty(name, out var arr) || arr.ValueKind != JsonValueKind.Array)
        {
            return list;
        }

        foreach (var item in arr.EnumerateArray())
        {
            if (item.ValueKind == JsonValueKind.String)
            {
                var s = item.GetString();
                if (!string.IsNullOrWhiteSpace(s))
                {
                    list.Add(s);
                }
            }
        }

        return list;
    }
}

internal static class AiModelCapabilities
{
    public const string Chat = "chat";
    public const string ImageInput = "image_input";
    public const string VideoInput = "video_input";

    public static bool Has(IEnumerable<string> caps, string name) =>
        caps.Any(c => string.Equals(c, name, StringComparison.OrdinalIgnoreCase));
}

internal sealed record AiModelInfo(
    string Id,
    string Model,
    string Name,
    bool Enabled,
    IReadOnlyList<string> Capabilities)
{
    public static AiModelInfo FromJson(JsonElement root) =>
        new(
            root.TryGetProperty("id", out var id) ? id.GetString() ?? "" : "",
            root.TryGetProperty("model", out var model) ? model.GetString() ?? "" : "",
            root.TryGetProperty("name", out var name) ? name.GetString() ?? "" : "",
            !root.TryGetProperty("enabled", out var en) || en.GetBoolean(),
            AiSettingsService.ReadStringArray(root, "capabilities"));
}

internal sealed record AiModelsResult(
    string ProviderId,
    string PreferredModelId,
    IReadOnlyList<string> ModelCapabilities,
    IReadOnlyList<AiModelInfo> Models)
{
    public static AiModelsResult FromJson(JsonElement root)
    {
        var models = new List<AiModelInfo>();
        if (root.TryGetProperty("models", out var arr) && arr.ValueKind == JsonValueKind.Array)
        {
            foreach (var m in arr.EnumerateArray())
            {
                models.Add(AiModelInfo.FromJson(m));
            }
        }

        return new AiModelsResult(
            root.TryGetProperty("provider_id", out var pid) ? pid.GetString() ?? "" : "",
            root.TryGetProperty("preferred_model_id", out var pref) ? pref.GetString() ?? "" : "",
            AiSettingsService.ReadStringArray(root, "model_capabilities"),
            models);
    }
}

internal sealed record AiProviderStatus(
    string Id,
    string Name,
    string Description,
    bool Enabled,
    string BaseUrl,
    string SecretId,
    bool SecretPresent,
    string PreferredModelId,
    IReadOnlyList<string> ModelCapabilities,
    IReadOnlyList<AiModelInfo> Models)
{
    public static AiProviderStatus FromJson(JsonElement root)
    {
        var models = new List<AiModelInfo>();
        if (root.TryGetProperty("models", out var arr) && arr.ValueKind == JsonValueKind.Array)
        {
            foreach (var m in arr.EnumerateArray())
            {
                models.Add(AiModelInfo.FromJson(m));
            }
        }

        return new AiProviderStatus(
            root.TryGetProperty("id", out var id) ? id.GetString() ?? "" : "",
            root.TryGetProperty("name", out var name) ? name.GetString() ?? "" : "",
            root.TryGetProperty("description", out var desc) ? desc.GetString() ?? "" : "",
            !root.TryGetProperty("enabled", out var en) || en.GetBoolean(),
            root.TryGetProperty("base_url", out var url) ? url.GetString() ?? "" : "",
            root.TryGetProperty("secret_id", out var sid) ? sid.GetString() ?? "" : "",
            root.TryGetProperty("secret_present", out var sp) && sp.GetBoolean(),
            root.TryGetProperty("preferred_model_id", out var pref) ? pref.GetString() ?? "" : "",
            AiSettingsService.ReadStringArray(root, "model_capabilities"),
            models);
    }
}
