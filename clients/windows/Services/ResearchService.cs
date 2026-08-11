using System.Text.Json;
using XScope.Native;

namespace XScope.Services;

internal sealed class ResearchService : IDisposable
{
    private readonly NativeWorkspace _workspace;

    public ResearchService(string? dataRoot = null)
    {
        _workspace = new NativeWorkspace(dataRoot ?? AppPaths.DataRoot);
    }

    public string Start(string projectId, string query, string modelId, int precision)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_research_start(_workspace.Handle, projectId, query, modelId, precision));
        var root = doc.RootElement;
        EnsureOk(root);
        return root.TryGetProperty("run_id", out var id) ? id.GetString() ?? "" : "";
    }

    public void Continue(string runId, string userReply)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_research_continue(_workspace.Handle, runId, userReply));
        EnsureOk(doc.RootElement);
    }

    public void Cancel(string runId)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_research_cancel(_workspace.Handle, runId));
        EnsureOk(doc.RootElement);
    }

    /// <summary>Poll next phase. Returns null when no event yet.</summary>
    public ResearchPollResult? Poll(string runId, int waitMs)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_research_poll_xaiop(_workspace.Handle, runId, waitMs));
        var root = doc.RootElement;
        EnsureOk(root);
        var has = root.TryGetProperty("has_event", out var he) && he.GetBoolean();
        if (!has || !root.TryGetProperty("doc", out var phaseDoc))
        {
            return null;
        }

        return ResearchPollResult.FromDoc(phaseDoc);
    }

    public ResearchRunStatus? Status(string runId)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_research_status(_workspace.Handle, runId));
        var root = doc.RootElement;
        EnsureOk(root);
        return ResearchRunStatus.FromJson(root);
    }

    public IReadOnlyList<ResearchEvidenceItem> ListEvidence(string projectId, string runId)
    {
        using var doc = XScopeNative.ParseJson(
            XScopeNative.xscope_research_evidence_list(_workspace.Handle, projectId, runId));
        var root = doc.RootElement;
        EnsureOk(root);
        var list = new List<ResearchEvidenceItem>();
        if (!root.TryGetProperty("items", out var arr) || arr.ValueKind != JsonValueKind.Array)
        {
            return list;
        }

        foreach (var e in arr.EnumerateArray())
        {
            list.Add(ResearchEvidenceItem.FromJson(e));
        }

        return list;
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

internal sealed record ResearchPollResult(
    string Phase,
    string Status,
    string RunId,
    string Summary,
    JsonElement Payload)
{
    public static ResearchPollResult FromDoc(JsonElement doc)
    {
        var phase = "";
        if (doc.TryGetProperty("meta", out var meta) && meta.TryGetProperty("phase", out var p))
        {
            phase = p.GetString() ?? "";
        }

        return new ResearchPollResult(
            phase,
            doc.TryGetProperty("status", out var st) ? st.GetString() ?? "" : "",
            doc.TryGetProperty("run_id", out var rid) ? rid.GetString() ?? "" : "",
            doc.TryGetProperty("summary", out var sum) ? sum.GetString() ?? "" : "",
            doc.TryGetProperty("payload", out var payload) ? payload.Clone() : default);
    }
}

internal sealed record ResearchRunStatus(
    string RunId,
    string ProjectId,
    string Status,
    int SearchRoundsDone,
    string Summary,
    string WaitingPrompt,
    string Error)
{
    public static ResearchRunStatus FromJson(JsonElement root) =>
        new(
            root.TryGetProperty("run_id", out var id) ? id.GetString() ?? "" : "",
            root.TryGetProperty("project_id", out var pid) ? pid.GetString() ?? "" : "",
            root.TryGetProperty("status", out var st) ? st.GetString() ?? "" : "",
            root.TryGetProperty("search_rounds_done", out var r) ? r.GetInt32() : 0,
            root.TryGetProperty("summary", out var sum) ? sum.GetString() ?? "" : "",
            root.TryGetProperty("waiting_prompt", out var wp) ? wp.GetString() ?? "" : "",
            root.TryGetProperty("error", out var err) ? err.GetString() ?? "" : "");
}

internal sealed record ResearchEvidenceItem(
    string Id,
    string Title,
    string Url,
    string Snippet,
    string ModuleId,
    int Round)
{
    public static ResearchEvidenceItem FromJson(JsonElement root) =>
        new(
            root.TryGetProperty("id", out var id) ? id.GetString() ?? "" : "",
            root.TryGetProperty("title", out var title) ? title.GetString() ?? "" : "",
            root.TryGetProperty("url", out var url) ? url.GetString() ?? ""
                : root.TryGetProperty("source_uri", out var uri) ? uri.GetString() ?? "" : "",
            root.TryGetProperty("snippet", out var sn) ? sn.GetString() ?? "" : "",
            root.TryGetProperty("module_id", out var mid) ? mid.GetString() ?? "" : "",
            root.TryGetProperty("round", out var r) ? r.GetInt32() : 0);
}
