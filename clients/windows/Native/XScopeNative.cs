using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

namespace XScope.Native;

internal static class XScopeNative
{
    private const string Dll = "xscope_capi";

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void xscope_string_free(IntPtr s);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern int xscope_last_error(byte[]? buf, int bufLen);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_workspace_open(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string dataRootUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void xscope_workspace_close(IntPtr ws);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_github_oauth_status(IntPtr ws);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_github_oauth_start(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? scopeUtf8,
        int openBrowser);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_github_oauth_poll(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string deviceCodeUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_github_oauth_set_pat(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string tokenUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? scopeUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_github_oauth_disconnect(IntPtr ws);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_secret_put(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string idUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? providerUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string plaintextUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_secret_has(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string idUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_secret_remove(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string idUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_ai_provider_status(IntPtr ws);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_ai_set_api_key(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string providerIdUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string apiKeyUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_ai_refresh_models(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string providerIdUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_ai_set_preferred_model(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string providerIdUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string modelIdUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_ai_set_model_capabilities(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string providerIdUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string capabilitiesJsonUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_search_modules_list(IntPtr ws);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_search_module_set_enabled(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string idUtf8,
        int enabled);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_search_module_set_api_key(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string idUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string apiKeyUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_project_list(IntPtr ws);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_project_create(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string titleUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_project_rename(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string idUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string titleUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_project_set_pinned(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string idUtf8,
        int pinned);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_project_delete(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string idUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_research_start(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string projectIdUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string queryUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? modelIdUtf8,
        int precision);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_research_continue(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string runIdUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? userReplyUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_research_cancel(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string runIdUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_research_poll_xaiop(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string runIdUtf8,
        int waitMs);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_research_status(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string runIdUtf8);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr xscope_research_evidence_list(
        IntPtr ws,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string projectIdUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string runIdUtf8);

    public static string ConsumeUtf8(IntPtr ptr)
    {
        if (ptr == IntPtr.Zero)
        {
            throw new InvalidOperationException(LastError() ?? "native returned null");
        }

        try
        {
            return Marshal.PtrToStringUTF8(ptr) ?? string.Empty;
        }
        finally
        {
            xscope_string_free(ptr);
        }
    }

    public static string? LastError()
    {
        var len = xscope_last_error(null, 0);
        if (len <= 0)
        {
            return null;
        }

        var buf = new byte[len + 1];
        xscope_last_error(buf, buf.Length);
        return Encoding.UTF8.GetString(buf, 0, len);
    }

    public static JsonDocument ParseJson(IntPtr ptr) =>
        JsonDocument.Parse(ConsumeUtf8(ptr));
}

internal sealed class NativeWorkspace : IDisposable
{
    private IntPtr _handle;

    public NativeWorkspace(string dataRoot)
    {
        _handle = XScopeNative.xscope_workspace_open(dataRoot);
        if (_handle == IntPtr.Zero)
        {
            throw new InvalidOperationException(
                XScopeNative.LastError() ?? "xscope_workspace_open failed");
        }
    }

    public IntPtr Handle => _handle;

    public void Dispose()
    {
        if (_handle != IntPtr.Zero)
        {
            XScopeNative.xscope_workspace_close(_handle);
            _handle = IntPtr.Zero;
        }
    }
}
