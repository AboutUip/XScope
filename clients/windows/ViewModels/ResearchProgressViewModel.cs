using System.Collections.ObjectModel;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XScope.Services;

namespace XScope.ViewModels;

internal sealed partial class ResearchEvidenceRow : ObservableObject
{
    public required string Id { get; init; }

    [ObservableProperty]
    private int _round;

    [ObservableProperty]
    private string _keyword = "";

    [ObservableProperty]
    private string _title = "";

    [ObservableProperty]
    private string _url = "";

    [ObservableProperty]
    private string _snippet = "";

    [ObservableProperty]
    private string _moduleId = "";

    [ObservableProperty]
    private string _status = "";
}

internal sealed partial class ResearchChoiceOption : ObservableObject
{
    public required string Id { get; init; }
    public required string Label { get; init; }
    public string Hint { get; init; } = "";
    public string Badge { get; init; } = "";
}

/// <summary>One hit nested under a search tool card.</summary>
internal sealed partial class ResearchFeedHit : ObservableObject
{
    [ObservableProperty]
    private string _title = "";

    [ObservableProperty]
    private string _url = "";

    [ObservableProperty]
    private string _snippet = "";

    public bool HasUrl => !string.IsNullOrWhiteSpace(Url);
}

/// <summary>Realtime feed row: thinking / keyword / hit / tool / system.</summary>
internal sealed partial class ResearchFeedItem : ObservableObject
{
    public required string Kind { get; init; } // thinking | keyword | hit | tool | system

    [ObservableProperty]
    private string _title = "";

    [ObservableProperty]
    private string _body = "";

    [ObservableProperty]
    private string _meta = "";

    [ObservableProperty]
    private string _url = "";

    [ObservableProperty]
    private bool _isExpanded = true;

    /// <summary>True while tokens for this bubble are still arriving.</summary>
    [ObservableProperty]
    private bool _isStreaming;

    [ObservableProperty]
    private bool _hitsExpanded;

    public long TurnId { get; set; }

    public ObservableCollection<ResearchFeedHit> Hits { get; } = [];

    public ResearchFeedItem()
    {
        Hits.CollectionChanged += (_, _) =>
        {
            OnPropertyChanged(nameof(HasHits));
            OnPropertyChanged(nameof(HitsCountLabel));
        };
    }

    public bool IsThinking => Kind == "thinking";
    public bool IsSearch => Kind is "keyword" or "search";
    public bool IsHit => Kind == "hit";
    public bool IsTool => Kind == "tool";
    public bool IsUser => Kind == "user";
    public bool IsSystem => Kind == "system";
    public bool ShowExpander => Kind == "thinking" && !string.IsNullOrWhiteSpace(Body);
    public bool HasUrl => !string.IsNullOrWhiteSpace(Url);
    public bool HasHits => Hits.Count > 0;
    public string HitsCountLabel =>
        Hits.Count > 0 ? $"{Hits.Count} {Loc.Instance.T("research.feed.hits")}" : "";
}

internal partial class ResearchProgressViewModel : ObservableObject
{
    private readonly ResearchService _research;
    private CancellationTokenSource? _pollCts;
    private string _runId = "";
    private string _projectId = "";
    private string _lastClarifyPrompt = "";
    private bool _submittingReply;

    public ObservableCollection<ResearchFeedItem> Feed { get; } = [];
    public ObservableCollection<string> Keywords { get; } = [];
    public ObservableCollection<ResearchEvidenceRow> Evidence { get; } = [];
    public ObservableCollection<ResearchChoiceOption> Choices { get; } = [];

    [ObservableProperty]
    private string _phaseLabel = "";

    [ObservableProperty]
    private string _statusLabel = "";

    [ObservableProperty]
    private string _latestThinking = "";

    [ObservableProperty]
    private string _summaryText = "";

    [ObservableProperty]
    private string _clarifyPrompt = "";

    [ObservableProperty]
    private bool _isWaitingUser;

    [ObservableProperty]
    private bool _isAwaitingConfirm;

    [ObservableProperty]
    private bool _isRunning;

    [ObservableProperty]
    private bool _hasSummary;

    [ObservableProperty]
    private bool _requirementsLocked;

    [ObservableProperty]
    private bool _isFeedExpanded = true;

    [ObservableProperty]
    private string _userReply = "";

    [ObservableProperty]
    private string _reportMarkdown = "";

    [ObservableProperty]
    private bool _hasReport;

    [ObservableProperty]
    private string _knowledgeGraphJson = "";

    public string ProjectId => _projectId;

    /// <summary>Unused — discovery uses the chat stream, not a status strip.</summary>
    public bool ShowBusyStage => false;

    /// <summary>Model proposed a clear need — user must Confirm.</summary>
    public bool ShowConfirmStage => IsAwaitingConfirm && !RequirementsLocked;

    /// <summary>Multiple-choice clarify (not the lock-confirm card).</summary>
    public bool ShowClarifyStage => IsWaitingUser && !IsAwaitingConfirm && !RequirementsLocked;

    /// <summary>Cursor-style conversation stream from discovery through deep research.</summary>
    public bool ShowChatStream =>
        IsRunning || IsWaitingUser || IsAwaitingConfirm || RequirementsLocked || HasReport || Feed.Count > 0;

    /// <summary>Top search box — only before a run starts on this project view.</summary>
    public bool ShowTopCompose => !ShowChatStream;

    /// <summary>Bottom follow-up box — after a report finishes.</summary>
    public bool ShowFollowUpCompose =>
        HasReport && !IsRunning && !IsWaitingUser && !IsAwaitingConfirm;

    /// <summary>Alias kept for older bindings.</summary>
    public bool ShowTimeline => ShowChatStream;

    public bool HasKeywords => Keywords.Count > 0;
    public bool HasEvidence => Evidence.Count > 0;

    public event Action? Changed;
    public event Action? FeedUpdated;

    private readonly HashSet<string> _evidenceIds = new(StringComparer.Ordinal);
    private string _lastFeedFingerprint = "";

    public ResearchProgressViewModel(ResearchService research)
    {
        _research = research;
        Keywords.CollectionChanged += (_, _) => OnPropertyChanged(nameof(HasKeywords));
        Evidence.CollectionChanged += (_, _) => OnPropertyChanged(nameof(HasEvidence));
    }

    public async Task StartAsync(string projectId, string query, string modelId, int precision)
    {
        await StopPollingAsync(cancelRun: true);

        ResetUi();
        _projectId = projectId;
        IsRunning = true;
        IsFeedExpanded = true;
        StatusLabel = Loc.Instance.T("research.status.discovering");
        PhaseLabel = "start";
        PushFeed("system", Loc.Instance.T("research.phase.start"), query);

        try
        {
            _runId = await Task.Run(() => _research.Start(projectId, query, modelId, precision));
            _pollCts = new CancellationTokenSource();
            _ = PollLoopAsync(_pollCts.Token);
        }
        catch (Exception ex)
        {
            IsRunning = false;
            StatusLabel = ex.Message;
            RaiseChanged();
            throw;
        }
    }

    /// <summary>
    /// Load a historical project into the research UI.
    /// Lightweight: no full event-stream replay (that rebuilt dozens of nested Feed cards and froze UI).
    /// Snapshot ships report + run meta only; chat shows a short restore stub.
    /// </summary>
    public async Task<ResearchProjectSnapshot> LoadProjectAsync(string projectId)
    {
        await StopPollingAsync(cancelRun: false);

        ResearchProjectSnapshot snap;
        try
        {
            snap = await Task.Run(() => _research.GetProjectSnapshot(projectId));
        }
        catch (Exception ex)
        {
            await DispatchAsync(() =>
            {
                ResetUi();
                _projectId = projectId;
                StatusLabel = ex.Message;
                RaiseChanged();
            });
            throw;
        }

        // Frame 1: shell + short feed stub (no event replay, no Markdown yet).
        await DispatchAsync(() =>
        {
            ResetUi();
            _projectId = projectId;
            _runId = snap.RunId;
            IsRunning = false;
            IsWaitingUser = false;
            IsAwaitingConfirm = false;
            IsFeedExpanded = false;
            KnowledgeGraphJson = GraphJsonHasNodes(snap.KnowledgeGraphJson)
                ? snap.KnowledgeGraphJson
                : "";

            if (!string.IsNullOrWhiteSpace(snap.Query))
            {
                PushFeed("system", Loc.Instance.T("research.phase.start"), snap.Query);
            }

            if (!string.IsNullOrWhiteSpace(snap.Summary))
            {
                SummaryText = CleanLockedText(snap.Summary);
                HasSummary = !string.IsNullOrWhiteSpace(SummaryText);
                RequirementsLocked = true;
                if (HasSummary)
                {
                    PushFeed("system", Loc.Instance.T("research.feed.locked"), SummaryText);
                }
            }

            var steps = Math.Max(snap.EventCount, 0);
            var rounds = Math.Max(snap.EvidenceCount, 0);
            if (steps > 0 || rounds > 0)
            {
                PushFeed(
                    "system",
                    Loc.Instance.T("research.feed.next_step"),
                    string.Format(Loc.Instance.T("research.feed.restore_summary"), steps, rounds));
            }

            if (!string.IsNullOrWhiteSpace(snap.ReportMarkdown) || snap.Status is "completed")
            {
                HasReport = true;
                StatusLabel = Loc.Instance.T("research.status.completed");
            }
            else if (!string.IsNullOrWhiteSpace(snap.Status))
            {
                StatusLabel = MapStatus(snap.Status);
            }
            else
            {
                StatusLabel = Loc.Instance.T("main.research.ready");
            }

            RaiseChanged();
        });

        await DispatcherYieldAsync();

        // Frame 2 (idle): bind Markdown once — MdXaml is the remaining heavy paint.
        if (!string.IsNullOrWhiteSpace(snap.ReportMarkdown))
        {
            var md = snap.ReportMarkdown;
            var dispatcher = System.Windows.Application.Current?.Dispatcher;
            if (dispatcher is not null)
            {
                await dispatcher.InvokeAsync(() =>
                {
                    ReportMarkdown = md;
                    HasReport = true;
                    StatusLabel = Loc.Instance.T("research.status.completed");
                    RaiseChanged();
                    HistoryRestored?.Invoke();
                }, System.Windows.Threading.DispatcherPriority.ApplicationIdle).Task;
            }
            else
            {
                await DispatchAsync(() =>
                {
                    ReportMarkdown = md;
                    HasReport = true;
                    HistoryRestored?.Invoke();
                });
            }
        }
        else
        {
            await DispatchAsync(() => HistoryRestored?.Invoke());
        }

        return snap;
    }

    private static bool GraphJsonHasNodes(string? json)
    {
        if (string.IsNullOrWhiteSpace(json))
        {
            return false;
        }

        try
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;
            if (root.TryGetProperty("graph", out var nested) && nested.ValueKind == JsonValueKind.Object)
            {
                root = nested;
            }

            return root.TryGetProperty("nodes", out var nodes) &&
                   nodes.ValueKind == JsonValueKind.Array &&
                   nodes.GetArrayLength() > 0;
        }
        catch
        {
            return false;
        }
    }

    private static Task DispatcherYieldAsync()
    {
        var dispatcher = System.Windows.Application.Current?.Dispatcher;
        if (dispatcher is null)
        {
            return Task.CompletedTask;
        }

        return dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.Background).Task;
    }

    /// <summary>Fired after a historical project snapshot is applied to the UI.</summary>
    public event Action? HistoryRestored;

    [RelayCommand]
    private async Task CancelAsync()
    {
        if (string.IsNullOrEmpty(_runId))
        {
            return;
        }

        try
        {
            await Task.Run(() => _research.Cancel(_runId));
        }
        catch
        {
        }

        await StopPollingAsync(cancelRun: false);
        IsRunning = false;
        IsWaitingUser = false;
        IsAwaitingConfirm = false;
        StatusLabel = Loc.Instance.T("research.status.cancelled");
        RaiseChanged();
    }

    private bool CanContinue() =>
        !string.IsNullOrEmpty(_runId) && (IsWaitingUser || IsAwaitingConfirm) && !_submittingReply;

    [RelayCommand(CanExecute = nameof(CanContinue))]
    private async Task ContinueAsync()
    {
        if (string.IsNullOrEmpty(_runId) || (!IsWaitingUser && !IsAwaitingConfirm) || _submittingReply)
        {
            return;
        }

        var reply = UserReply.Trim();
        if (reply.Length == 0)
        {
            if (IsAwaitingConfirm)
            {
                StatusLabel = Loc.Instance.T("research.status.confirm_or_adjust");
            }
            else
            {
                StatusLabel = Loc.Instance.T("research.status.pick_or_type");
            }

            RaiseChanged();
            return;
        }

        await SubmitUserReplyAsync(reply);
    }

    [RelayCommand]
    private async Task ChooseAsync(ResearchChoiceOption? option)
    {
        if (option is null || _submittingReply || !IsWaitingUser || IsAwaitingConfirm)
        {
            return;
        }

        var reply = string.IsNullOrWhiteSpace(option.Hint)
            ? option.Label
            : $"{option.Id}: {option.Label} — {option.Hint}";
        await SubmitUserReplyAsync(reply);
    }

    [RelayCommand]
    private async Task ConfirmNeedAsync()
    {
        if (!IsAwaitingConfirm || _submittingReply || string.IsNullOrEmpty(_runId))
        {
            return;
        }

        await SubmitUserReplyAsync("__confirm__");
    }

    private async Task SubmitUserReplyAsync(string reply)
    {
        if (string.IsNullOrEmpty(_runId) || _submittingReply)
        {
            return;
        }

        _submittingReply = true;
        ContinueCommand.NotifyCanExecuteChanged();
        var runId = _runId;
        try
        {
            PushFeed("user", Loc.Instance.T("research.feed.user_reply"),
                reply == "__confirm__" ? Loc.Instance.T("research.confirm.accepted") : reply);
            IsWaitingUser = false;
            IsAwaitingConfirm = false;
            var promptSnapshot = ClarifyPrompt;
            ClarifyPrompt = "";
            Choices.Clear();
            UserReply = "";
            StatusLabel = Loc.Instance.T("research.status.discovering");
            RaiseChanged();

            await Task.Run(() => _research.Continue(runId, reply)).ConfigureAwait(true);
            _lastClarifyPrompt = promptSnapshot;
        }
        catch (Exception ex)
        {
            StatusLabel = ex.Message;
            IsWaitingUser = true;
            ClarifyPrompt = _lastClarifyPrompt;
            RaiseChanged();
        }
        finally
        {
            _submittingReply = false;
            ContinueCommand.NotifyCanExecuteChanged();
        }
    }

    partial void OnUserReplyChanged(string value) => ContinueCommand.NotifyCanExecuteChanged();

    partial void OnIsWaitingUserChanged(bool value)
    {
        ContinueCommand.NotifyCanExecuteChanged();
        NotifyStageFlags();
    }

    partial void OnIsAwaitingConfirmChanged(bool value) => NotifyStageFlags();

    partial void OnIsRunningChanged(bool value) => NotifyStageFlags();

    partial void OnHasReportChanged(bool value) => NotifyStageFlags();

    partial void OnRequirementsLockedChanged(bool value) => NotifyStageFlags();

    private void NotifyStageFlags()
    {
        OnPropertyChanged(nameof(ShowBusyStage));
        OnPropertyChanged(nameof(ShowConfirmStage));
        OnPropertyChanged(nameof(ShowClarifyStage));
        OnPropertyChanged(nameof(ShowChatStream));
        OnPropertyChanged(nameof(ShowTopCompose));
        OnPropertyChanged(nameof(ShowFollowUpCompose));
        OnPropertyChanged(nameof(ShowTimeline));
    }

    public async Task StopPollingAsync(bool cancelRun)
    {
        var cts = _pollCts;
        _pollCts = null;
        if (cts is not null)
        {
            try
            {
                cts.Cancel();
            }
            catch
            {
            }

            cts.Dispose();
        }

        if (cancelRun && !string.IsNullOrEmpty(_runId) && IsRunning)
        {
            try
            {
                await Task.Run(() => _research.Cancel(_runId));
            }
            catch
            {
            }
        }
    }

    private void ResetUi()
    {
        Feed.Clear();
        Keywords.Clear();
        Evidence.Clear();
        Choices.Clear();
        _evidenceIds.Clear();
        _lastFeedFingerprint = "";
        PhaseLabel = "";
        StatusLabel = "";
        LatestThinking = "";
        SummaryText = "";
        ClarifyPrompt = "";
        UserReply = "";
        IsWaitingUser = false;
        IsAwaitingConfirm = false;
        HasSummary = false;
        RequirementsLocked = false;
        HasReport = false;
        ReportMarkdown = "";
        KnowledgeGraphJson = "";
        IsFeedExpanded = true;
        _lastClarifyPrompt = "";
        _submittingReply = false;
        _runId = "";
        RaiseChanged();
    }

    private static string CleanLockedText(string? raw)
    {
        if (string.IsNullOrWhiteSpace(raw))
        {
            return "";
        }

        var s = raw.Trim();
        // Strip "需求已锁定：" prefix if engine still sends it.
        foreach (var prefix in new[] { "需求已锁定：", "需求已锁定:", "已锁定：", "已锁定:", "Locked: " })
        {
            if (s.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            {
                s = s[prefix.Length..].Trim();
            }
        }

        // "query | focus: xxx" or "query | user: id: label — hint"
        var pipe = s.LastIndexOf(" | ", StringComparison.Ordinal);
        if (pipe >= 0 && pipe + 3 < s.Length)
        {
            s = s[(pipe + 3)..].Trim();
        }

        foreach (var marker in new[] { "focus:", "user:", "Focus:", "User:" })
        {
            if (s.StartsWith(marker, StringComparison.OrdinalIgnoreCase))
            {
                s = s[marker.Length..].Trim();
            }
        }

        var colon = s.IndexOf(':');
        if (colon > 0 && colon < 36 && !s[..colon].Contains(' '))
        {
            s = s[(colon + 1)..].Trim();
        }

        var em = s.IndexOf(" — ", StringComparison.Ordinal);
        if (em > 0)
        {
            s = s[..em].Trim();
        }

        return s;
    }

    private async Task PollLoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested && !string.IsNullOrEmpty(_runId))
        {
            ResearchPollResult? evt = null;
            try
            {
                evt = await Task.Run(() => _research.Poll(_runId, 400), ct);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                await DispatchAsync(() =>
                {
                    StatusLabel = ex.Message;
                    RaiseChanged();
                });
                await Task.Delay(500, ct).ContinueWith(_ => { });
                continue;
            }

            if (evt is null)
            {
                continue;
            }

            var snapshot = evt;
            await DispatchAsync(() => ApplyEvent(snapshot));

            if (snapshot.Phase is "final" or "cancelled" or "error")
            {
                IsRunning = false;
                RaiseChanged();
                break;
            }
        }
    }

    private void ApplyEvent(ResearchPollResult evt)
    {
        PhaseLabel = evt.Phase;
        if (!string.IsNullOrWhiteSpace(evt.Status))
        {
            StatusLabel = MapStatus(evt.Status);
        }

        switch (evt.Phase)
        {
            case "thinking":
            {
                // Streamed thinking can be long; keep high fidelity in the chat bubble.
                var text = TruncateUi(ReadString(evt.Payload, "text") ?? "", 32000);
                if (string.IsNullOrWhiteSpace(text) || text is "…" or "...")
                {
                    break;
                }

                LatestThinking = text;
                var streaming = ReadStreamingFlag(evt.Payload, defaultIfMissing: false);
                var turnId = ReadInt64(evt.Payload, "turn_id");

                // Only update in place while THIS turn is still streaming.
                // Sealed bubbles stay in history — a new turn starts a new row (Cursor-like).
                if (Feed.Count > 0 && Feed[^1].IsThinking && Feed[^1].IsStreaming &&
                    (turnId == 0 || Feed[^1].TurnId == 0 || Feed[^1].TurnId == turnId))
                {
                    Feed[^1].Body = text;
                    Feed[^1].TurnId = turnId != 0 ? turnId : Feed[^1].TurnId;
                    Feed[^1].IsStreaming = streaming;
                    Feed[^1].IsExpanded = true;
                    FeedUpdated?.Invoke();
                }
                else if (Feed.Count > 0 && Feed[^1].IsThinking && turnId != 0 &&
                         Feed[^1].TurnId == turnId)
                {
                    // Same turn final polish (persist frame after live seal) — do not spawn a twin.
                    Feed[^1].Body = text;
                    Feed[^1].IsStreaming = false;
                    FeedUpdated?.Invoke();
                }
                else
                {
                    if (Feed.Count > 0 && Feed[^1].IsThinking)
                    {
                        Feed[^1].IsStreaming = false;
                        Feed[^1].IsExpanded = false;
                    }

                    PushFeed("thinking", Loc.Instance.T("research.feed.thinking"), text, expand: true);
                    Feed[^1].IsStreaming = streaming;
                    Feed[^1].TurnId = turnId;
                }

                StatusLabel = Loc.Instance.T("research.status.thinking");
                break;
            }
            case "plan":
            {
                // Capture knowledge graph snapshots emitted during research.
                if (evt.Payload.ValueKind == JsonValueKind.Object &&
                    evt.Payload.TryGetProperty("knowledge_graph", out var kg) &&
                    kg.ValueKind == JsonValueKind.Object)
                {
                    KnowledgeGraphJson = kg.GetRawText();
                }

                // Catalog dumps / search plans: avoid duplicate chat rows.
                // Real searches render via "searching"; only show non-search tool calls here.
                var mid = ReadString(evt.Payload, "module_id") ?? "";
                var ep = ReadString(evt.Payload, "endpoint") ?? "";
                var path = ReadString(evt.Payload, "path") ?? "";
                var q = ReadString(evt.Payload, "q") ?? ReadString(evt.Payload, "keyword") ?? "";
                var note = ReadString(evt.Payload, "note") ?? "";
                if (!string.IsNullOrWhiteSpace(note) &&
                    string.IsNullOrWhiteSpace(mid) && string.IsNullOrWhiteSpace(path))
                {
                    // Seal any open thinking so the next AI turn cannot overwrite it.
                    if (Feed.Count > 0 && Feed[^1].IsThinking)
                    {
                        Feed[^1].IsStreaming = false;
                        Feed[^1].IsExpanded = false;
                    }

                    PushFeed("system", Loc.Instance.T("research.feed.next_step"), note);
                    StatusLabel = Loc.Instance.T("research.status.researching");
                    break;
                }

                var isSearchPlan = !string.IsNullOrWhiteSpace(mid) &&
                                   string.IsNullOrWhiteSpace(path) &&
                                   (ep is "web-search" or "ai-search" or "repositories" or "code" or
                                    "issues" or "commits" or "users" or "topics" or "labels" or "");
                if (isSearchPlan)
                {
                    StatusLabel = Loc.Instance.T("research.status.searching");
                    break;
                }

                if (!string.IsNullOrWhiteSpace(path) || !string.IsNullOrWhiteSpace(note) ||
                    !string.IsNullOrWhiteSpace(mid))
                {
                    if (Feed.Count > 0 && Feed[^1].IsThinking)
                    {
                        Feed[^1].IsStreaming = false;
                        Feed[^1].IsExpanded = false;
                    }

                    var title = !string.IsNullOrWhiteSpace(path)
                        ? $"github_rest {path}"
                        : string.IsNullOrWhiteSpace(ep) ? mid : $"{mid}/{ep}";
                    var body = !string.IsNullOrWhiteSpace(q) ? q : note;
                    PushFeed("tool", title, body, mid, expand: false);
                }

                StatusLabel = Loc.Instance.T("research.status.researching");
                break;
            }
            case "keyword":
                // Legacy duplicate of searching — ignore to prevent double cards.
                StatusLabel = Loc.Instance.T("research.status.searching");
                break;
            case "searching":
            {
                var kw = TruncateUi(
                    ReadString(evt.Payload, "keyword")
                    ?? ReadString(evt.Payload, "q")
                    ?? "",
                    200);
                foreach (var prefix in new[]
                         {
                             "已根据对话与检索锁定需求：", "已根据对话与检索锁定需求:",
                             "需求已锁定：", "已锁定：",
                         })
                {
                    if (kw.StartsWith(prefix, StringComparison.Ordinal))
                    {
                        kw = kw[prefix.Length..].Trim();
                    }
                }

                if (kw.Length >= 8 && kw.Length % 2 == 0)
                {
                    var half = kw.Length / 2;
                    if (kw.AsSpan(0, half).SequenceEqual(kw.AsSpan(half)))
                    {
                        kw = kw[..half].Trim();
                    }
                }

                if (!string.IsNullOrWhiteSpace(kw))
                {
                    if (Feed.Count > 0 && Feed[^1].IsThinking)
                    {
                        Feed[^1].IsStreaming = false;
                        Feed[^1].IsExpanded = false;
                    }

                    if (Keywords.Count < 24 && !Keywords.Contains(kw))
                    {
                        Keywords.Add(kw);
                    }

                    var mid = TruncateUi(ReadString(evt.Payload, "module_id") ?? "", 32);
                    var ep = TruncateUi(ReadString(evt.Payload, "endpoint") ?? "", 32);
                    PushFeed(
                        "keyword",
                        Loc.Instance.T("research.feed.keyword"),
                        kw,
                        string.IsNullOrWhiteSpace(ep) ? mid : $"{mid} · {ep}",
                        expand: false);
                }

                StatusLabel = Loc.Instance.T("research.status.searching");
                break;
            }
            case "evidence":
                AppendEvidenceHits(evt.Payload);
                StatusLabel = Loc.Instance.T("research.status.searching");
                break;
            case "clarify":
            case "directions":
            {
                if (_submittingReply)
                {
                    break;
                }

                var prompt = ReadString(evt.Payload, "prompt")
                    ?? Loc.Instance.T("research.clarify.default");
                if (IsWaitingUser && !IsAwaitingConfirm &&
                    string.Equals(prompt, _lastClarifyPrompt, StringComparison.Ordinal))
                {
                    break;
                }

                IsAwaitingConfirm = false;
                IsWaitingUser = true;
                ClarifyPrompt = prompt;
                _lastClarifyPrompt = prompt;
                LoadChoices(evt.Payload);
                PushFeed("system", Loc.Instance.T("research.feed.ask_user"), prompt);
                StatusLabel = Loc.Instance.T("research.status.waiting");
                ContinueCommand.NotifyCanExecuteChanged();
                break;
            }
            case "confirm_need":
            {
                if (_submittingReply)
                {
                    break;
                }

                var need = CleanLockedText(ReadString(evt.Payload, "clarified_need")
                    ?? ReadString(evt.Payload, "summary")
                    ?? "");
                SummaryText = need;
                HasSummary = !string.IsNullOrWhiteSpace(need);
                ClarifyPrompt = ReadString(evt.Payload, "prompt")
                    ?? Loc.Instance.T("research.confirm.prompt");
                Choices.Clear();
                IsAwaitingConfirm = true;
                IsWaitingUser = true;
                RequirementsLocked = false;
                PushFeed("system", Loc.Instance.T("research.feed.confirm_need"), need);
                StatusLabel = Loc.Instance.T("research.status.confirm_need");
                ContinueCommand.NotifyCanExecuteChanged();
                break;
            }
            case "requirements_locked":
            {
                RequirementsLocked = true;
                IsWaitingUser = false;
                IsAwaitingConfirm = false;
                IsRunning = true;
                IsFeedExpanded = true;
                var need = CleanLockedText(ReadString(evt.Payload, "clarified_need")
                    ?? ReadString(evt.Payload, "summary")
                    ?? "");
                SummaryText = need;
                HasSummary = !string.IsNullOrWhiteSpace(need);
                PushFeed("system", Loc.Instance.T("research.feed.locked"), need);
                StatusLabel = Loc.Instance.T("research.status.researching");
                break;
            }
            case "next_step":
            {
                IsRunning = true;
                IsFeedExpanded = true;
                var note = ReadString(evt.Payload, "note") ?? "";
                PushFeed("system", Loc.Instance.T("research.feed.next_step"), note);
                StatusLabel = Loc.Instance.T("research.status.researching");
                break;
            }
            case "synthesize":
            {
                var md = ReadString(evt.Payload, "markdown")
                    ?? ReadString(evt.Payload, "summary")
                    ?? "";
                var streaming = evt.Payload.ValueKind == JsonValueKind.Object
                    && evt.Payload.TryGetProperty("streaming", out var streamProp)
                    && streamProp.ValueKind is JsonValueKind.True or JsonValueKind.False
                    && streamProp.GetBoolean();
                if (!string.IsNullOrWhiteSpace(md))
                {
                    ReportMarkdown = md;
                    HasReport = true;
                }

                // Avoid spamming the feed while markdown tokens stream in.
                if (!streaming)
                {
                    var already =
                        Feed.Count > 0 && Feed[^1].IsSystem &&
                        Feed[^1].Title == Loc.Instance.T("research.feed.synthesize");
                    if (!already)
                    {
                        PushFeed("system", Loc.Instance.T("research.feed.synthesize"),
                            Loc.Instance.T("research.report.title"));
                    }
                }

                StatusLabel = Loc.Instance.T("research.status.synthesize");
                break;
            }
            case "final":
            {
                var finalSum = CleanLockedText(ReadString(evt.Payload, "summary")
                    ?? ReadString(evt.Payload, "clarified_need"));
                if (!string.IsNullOrWhiteSpace(finalSum))
                {
                    SummaryText = finalSum!;
                    HasSummary = true;
                }

                var md = ReadString(evt.Payload, "markdown");
                if (!string.IsNullOrWhiteSpace(md))
                {
                    ReportMarkdown = md!;
                    HasReport = true;
                }

                RequirementsLocked = true;
                IsWaitingUser = false;
                IsAwaitingConfirm = false;
                IsRunning = false;
                IsFeedExpanded = false;
                StatusLabel = HasReport
                    ? Loc.Instance.T("research.status.completed")
                    : Loc.Instance.T("research.status.locked");
                break;
            }
            case "cancelled":
                IsRunning = false;
                IsWaitingUser = false;
                IsAwaitingConfirm = false;
                IsFeedExpanded = false;
                StatusLabel = Loc.Instance.T("research.status.cancelled");
                break;
            case "error":
                IsRunning = false;
                IsWaitingUser = false;
                IsAwaitingConfirm = false;
                StatusLabel = ReadString(evt.Payload, "error")
                    ?? Loc.Instance.T("research.status.error");
                PushFeed("system", Loc.Instance.T("research.phase.error"), StatusLabel);
                break;
        }

        RaiseChanged();
    }

    private void AppendEvidenceHits(JsonElement payload)
    {
        if (payload.ValueKind != JsonValueKind.Object)
        {
            return;
        }

        var keyword = TruncateUi(ReadString(payload, "keyword") ?? "", 96);
        if (payload.TryGetProperty("ok", out var ok) && ok.ValueKind == JsonValueKind.False)
        {
            var err = TruncateUi(
                ReadString(payload, "error") ?? Loc.Instance.T("research.status.error"),
                220);
            PushFeed("system", Loc.Instance.T("research.feed.search_fail"), err, keyword, expand: false);
        }

        if (!payload.TryGetProperty("hits", out var hits) || hits.ValueKind != JsonValueKind.Array)
        {
            AddEvidenceRow(
                ReadString(payload, "evidence_id") ?? Guid.NewGuid().ToString("N"),
                payload.TryGetProperty("round", out var r) ? r.GetInt32() : 0,
                keyword,
                ReadString(payload, "title") ?? "",
                ReadString(payload, "url") ?? "",
                ReadString(payload, "snippet") ?? "",
                ReadString(payload, "module_id") ?? "");
            return;
        }

        foreach (var hit in hits.EnumerateArray())
        {
            AddEvidenceRow(
                ReadString(hit, "evidence_id") ?? Guid.NewGuid().ToString("N"),
                hit.TryGetProperty("round", out var r) ? r.GetInt32() : 0,
                TruncateUi(ReadString(hit, "keyword") ?? keyword, 96),
                ReadString(hit, "title") ?? "",
                ReadString(hit, "url") ?? "",
                ReadString(hit, "snippet") ?? "",
                ReadString(hit, "module_id") ?? "");
        }
    }

    private void AddEvidenceRow(
        string id,
        int round,
        string keyword,
        string title,
        string url,
        string snippet,
        string moduleId)
    {
        if (!_evidenceIds.Add(id))
        {
            return;
        }

        title = TruncateUi(title, 120);
        url = TruncateUi(url, 160);
        snippet = TruncateUi(snippet, 160);
        if (string.IsNullOrWhiteSpace(title) && string.IsNullOrWhiteSpace(url) &&
            string.IsNullOrWhiteSpace(snippet))
        {
            _evidenceIds.Remove(id);
            return;
        }

        while (Evidence.Count >= 40)
        {
            var drop = Evidence[0];
            _evidenceIds.Remove(drop.Id);
            Evidence.RemoveAt(0);
        }

        Evidence.Add(new ResearchEvidenceRow
        {
            Id = id,
            Round = round,
            Keyword = keyword,
            Title = title,
            Url = url,
            Snippet = snippet,
            ModuleId = moduleId,
            Status = Loc.Instance.T("research.evidence.collected"),
        });

        // Nest hits under the latest search tool card so they don't flood the chat.
        ResearchFeedItem? searchCard = null;
        for (var i = Feed.Count - 1; i >= 0; i--)
        {
            if (Feed[i].IsSearch)
            {
                searchCard = Feed[i];
                break;
            }
        }

        if (searchCard is null)
        {
            // No open search card — keep a compact single-line hit rather than a large card.
            PushFeed(
                "hit",
                string.IsNullOrWhiteSpace(title) ? url : title,
                snippet,
                string.IsNullOrWhiteSpace(keyword) ? moduleId : keyword,
                expand: false,
                url: url);
            return;
        }

        if (searchCard.Hits.Count >= 12)
        {
            return;
        }

        searchCard.Hits.Add(new ResearchFeedHit
        {
            Title = string.IsNullOrWhiteSpace(title) ? url : title,
            Url = url,
            Snippet = snippet,
        });
        OnPropertyChanged(nameof(Feed)); // nudge bindings for HasHits on item if needed
        FeedUpdated?.Invoke();
    }

    private void LoadChoices(JsonElement payload)
    {
        Choices.Clear();
        if (payload.ValueKind != JsonValueKind.Object
            || !payload.TryGetProperty("options", out var opts)
            || opts.ValueKind != JsonValueKind.Array)
        {
            return;
        }

        var index = 0;
        foreach (var o in opts.EnumerateArray())
        {
            var id = ReadString(o, "id") ?? "";
            var label = ReadString(o, "label") ?? id;
            if (string.IsNullOrWhiteSpace(label))
            {
                continue;
            }

            Choices.Add(new ResearchChoiceOption
            {
                Id = id,
                Label = label,
                Hint = TruncateUi(ReadString(o, "hint") ?? "", 160),
                Badge = index < 26 ? ((char)('A' + index)).ToString() : (index + 1).ToString(),
            });
            index++;
        }
    }

    private void PushFeed(string kind, string title, string body, string meta = "", bool expand = false,
        string url = "")
    {
        title = TruncateUi(title, 160);
        body = kind is "thinking" or "hit" ? TruncateUi(body, 32000) : TruncateUi(body, 480);
        meta = TruncateUi(meta, 64);
        url = TruncateUi(url, 240);

        // Drop exact duplicate spam (same search / REST path / hit).
        var fp = kind + "|" + title + "|" + body + "|" + meta + "|" + url;
        if (fp == _lastFeedFingerprint && kind is "keyword" or "hit" or "tool")
        {
            return;
        }

        _lastFeedFingerprint = fp;

        Feed.Add(new ResearchFeedItem
        {
            Kind = kind,
            Title = title,
            Body = body,
            Meta = meta,
            Url = url,
            IsExpanded = expand,
        });
        while (Feed.Count > 120)
        {
            Feed.RemoveAt(0);
        }

        FeedUpdated?.Invoke();
    }

    private static string TruncateUi(string value, int maxChars)
    {
        if (string.IsNullOrEmpty(value) || value.Length <= maxChars)
        {
            return value;
        }

        return value[..maxChars].TrimEnd() + "…";
    }

    private static string? ReadString(JsonElement el, string name) =>
        el.ValueKind == JsonValueKind.Object && el.TryGetProperty(name, out var v)
            ? v.GetString()
            : null;

    private static bool ReadStreamingFlag(JsonElement el, bool defaultIfMissing)
    {
        if (el.ValueKind != JsonValueKind.Object || !el.TryGetProperty("streaming", out var p))
        {
            return defaultIfMissing;
        }

        return p.ValueKind switch
        {
            JsonValueKind.True => true,
            JsonValueKind.False => false,
            _ => defaultIfMissing,
        };
    }

    private static long ReadInt64(JsonElement el, string name)
    {
        if (el.ValueKind != JsonValueKind.Object || !el.TryGetProperty(name, out var p))
        {
            return 0;
        }

        return p.ValueKind switch
        {
            JsonValueKind.Number => p.TryGetInt64(out var n) ? n : 0,
            JsonValueKind.String => long.TryParse(p.GetString(), out var s) ? s : 0,
            _ => 0,
        };
    }

    private static string MapStatus(string status) => status switch
    {
        "waiting_user" => Loc.Instance.T("research.status.waiting"),
        "completed" => Loc.Instance.T("research.status.locked"),
        "cancelled" => Loc.Instance.T("research.status.cancelled"),
        "failed" => Loc.Instance.T("research.status.error"),
        _ => Loc.Instance.T("research.status.discovering"),
    };

    private static Task DispatchAsync(Action action)
    {
        var dispatcher = System.Windows.Application.Current?.Dispatcher;
        if (dispatcher is null || dispatcher.CheckAccess())
        {
            action();
            return Task.CompletedTask;
        }

        return dispatcher.InvokeAsync(action).Task;
    }

    private void RaiseChanged()
    {
        NotifyStageFlags();
        Changed?.Invoke();
    }
}
