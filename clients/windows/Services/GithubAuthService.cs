using System.Text.Json;
using XScope.Native;

namespace XScope.Services;

internal sealed class GithubAuthService : IDisposable
{
    private readonly NativeWorkspace _workspace;

    public GithubAuthService(string? dataRoot = null)
    {
        _workspace = new NativeWorkspace(dataRoot ?? AppPaths.DataRoot);
    }

    public string DataRoot => AppPaths.DataRoot;

    public GithubStatus Status()
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_github_oauth_status(_workspace.Handle));
        return GithubStatus.FromJson(doc.RootElement);
    }

    public GithubDeviceStart Start(string? scope = null, bool openBrowser = true)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_github_oauth_start(_workspace.Handle, scope, openBrowser ? 1 : 0));
        var root = doc.RootElement;
        if (!root.TryGetProperty("ok", out var ok) || !ok.GetBoolean())
        {
            throw new InvalidOperationException(ReadError(root));
        }

        return new GithubDeviceStart(
            root.GetProperty("device_code").GetString() ?? "",
            root.GetProperty("user_code").GetString() ?? "",
            root.GetProperty("verification_uri").GetString() ?? "https://github.com/login/device",
            root.TryGetProperty("verification_uri_complete", out var complete)
                ? complete.GetString() ?? ""
                : "",
            root.TryGetProperty("expires_in", out var exp) ? exp.GetInt32() : 900,
            root.TryGetProperty("interval", out var interval) ? Math.Max(5, interval.GetInt32()) : 5);
    }

    public GithubPollResult Poll(string deviceCode)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_github_oauth_poll(_workspace.Handle, deviceCode));
        var root = doc.RootElement;
        var status = root.TryGetProperty("status", out var s) ? s.GetString() ?? "error" : "error";
        var error = root.TryGetProperty("error", out var e) ? e.GetString() ?? "" : "";
        var desc = root.TryGetProperty("error_description", out var d) ? d.GetString() ?? "" : "";
        var interval = root.TryGetProperty("interval", out var i) ? i.GetInt32() : 0;
        GithubStatus? connection = null;
        if (root.TryGetProperty("connection", out var c) && c.ValueKind == JsonValueKind.Object)
        {
            connection = GithubStatus.FromJson(c);
        }

        return new GithubPollResult(status, error, desc, interval, connection);
    }

    public GithubStatus SetPat(string token, string? scope = null)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_github_oauth_set_pat(_workspace.Handle, token, scope));
        var root = doc.RootElement;
        if (!root.TryGetProperty("ok", out var ok) || !ok.GetBoolean())
        {
            throw new InvalidOperationException(ReadError(root));
        }

        return GithubStatus.FromJson(root);
    }

    public GithubStatus Disconnect()
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_github_oauth_disconnect(_workspace.Handle));
        return GithubStatus.FromJson(doc.RootElement);
    }

    public void Dispose() => _workspace.Dispose();

    private static string ReadError(JsonElement root) =>
        root.TryGetProperty("error", out var err)
            ? err.GetString() ?? "unknown error"
            : XScopeNative.LastError() ?? "unknown error";
}

internal sealed record GithubStatus(
    bool Ok,
    bool Connected,
    bool ClientIdConfigured,
    string AccountLogin,
    string Scope,
    string SecretId,
    string DefaultScope)
{
    public static GithubStatus FromJson(JsonElement root) =>
        new(
            !root.TryGetProperty("ok", out var ok) || ok.GetBoolean(),
            root.TryGetProperty("connected", out var c) && c.GetBoolean(),
            root.TryGetProperty("client_id_configured", out var cid) && cid.GetBoolean(),
            root.TryGetProperty("account_login", out var login) ? login.GetString() ?? "" : "",
            root.TryGetProperty("scope", out var scope) ? scope.GetString() ?? "" : "",
            root.TryGetProperty("secret_id", out var sid) ? sid.GetString() ?? "github.oauth" : "github.oauth",
            root.TryGetProperty("default_scope", out var ds) ? ds.GetString() ?? "read:user" : "read:user");
}

internal sealed record GithubDeviceStart(
    string DeviceCode,
    string UserCode,
    string VerificationUri,
    string VerificationUriComplete,
    int ExpiresIn,
    int Interval);

internal sealed record GithubPollResult(
    string Status,
    string Error,
    string ErrorDescription,
    int Interval,
    GithubStatus? Connection);
