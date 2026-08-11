using System.Text;
using System.Text.Json;
using XScope.Native;

namespace XScope.Services;

internal sealed class ProjectsService : IDisposable
{
    public const int MaxTitleChars = 32;

    private readonly NativeWorkspace _workspace;

    public ProjectsService(string? dataRoot = null)
    {
        _workspace = new NativeWorkspace(dataRoot ?? AppPaths.DataRoot);
    }

    public IReadOnlyList<ProjectInfo> List()
    {
        using var doc = XScopeNative.ParseJson(XScopeNative.xscope_project_list(_workspace.Handle));
        var root = doc.RootElement;
        EnsureOk(root);
        var list = new List<ProjectInfo>();
        if (!root.TryGetProperty("projects", out var arr) || arr.ValueKind != JsonValueKind.Array)
        {
            return list;
        }

        foreach (var p in arr.EnumerateArray())
        {
            list.Add(ProjectInfo.FromJson(p));
        }

        return list;
    }

    public ProjectInfo Create(string title)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_project_create(_workspace.Handle, title));
        var root = doc.RootElement;
        EnsureOk(root);
        return ProjectInfo.FromJson(root);
    }

    public ProjectInfo Rename(string id, string title)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_project_rename(_workspace.Handle, id, title));
        var root = doc.RootElement;
        EnsureOk(root);
        return ProjectInfo.FromJson(root);
    }

    public ProjectInfo SetPinned(string id, bool pinned)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_project_set_pinned(_workspace.Handle, id, pinned ? 1 : 0));
        var root = doc.RootElement;
        EnsureOk(root);
        return ProjectInfo.FromJson(root);
    }

    public void Delete(string id)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_project_delete(_workspace.Handle, id));
        EnsureOk(doc.RootElement);
    }

    /// <summary>Trim + rune-aware truncate; overflow uses trailing "..." within MaxTitleChars.</summary>
    public static string TitleFromSearch(string query)
    {
        var trimmed = string.Join(
            ' ',
            query.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries));
        if (trimmed.Length == 0)
        {
            return "Untitled";
        }

        var runes = trimmed.EnumerateRunes().ToList();
        if (runes.Count <= MaxTitleChars)
        {
            return string.Concat(runes);
        }

        const string ellipsis = "...";
        var keep = Math.Max(1, MaxTitleChars - ellipsis.Length);
        var sb = new StringBuilder();
        for (var i = 0; i < keep; i++)
        {
            sb.Append(runes[i].ToString());
        }

        sb.Append(ellipsis);
        return sb.ToString();
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

internal sealed record ProjectInfo(
    string Id,
    string Title,
    long CreatedAt,
    long UpdatedAt,
    string PathRel,
    bool Pinned)
{
    public static ProjectInfo FromJson(JsonElement root) =>
        new(
            root.TryGetProperty("id", out var id) ? id.GetString() ?? "" : "",
            root.TryGetProperty("title", out var title) ? title.GetString() ?? "" : "",
            root.TryGetProperty("created_at", out var c) ? c.GetInt64() : 0,
            root.TryGetProperty("updated_at", out var u) ? u.GetInt64() : 0,
            root.TryGetProperty("path_rel", out var path) ? path.GetString() ?? "" : "",
            root.TryGetProperty("pinned", out var pin) && pin.GetBoolean());
}
