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

/// <summary>Realtime feed row: thinking / keyword / hit / system.</summary>
internal sealed partial class ResearchFeedItem : ObservableObject
{
    public required string Kind { get; init; } // thinking | keyword | hit | system

    [ObservableProperty]
    private string _title = "";

    [ObservableProperty]
    private string _body = "";

    [ObservableProperty]
    private string _meta = "";

    [ObservableProperty]
    private bool _isExpanded = true;

    public bool IsThinking => Kind == "thinking";
    public bool ShowExpander => Kind == "thinking" && !string.IsNullOrWhiteSpace(Body);
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

    /// <summary>Running with no clarify sheet and no final report yet.</summary>
    public bool ShowBusyStage => IsRunning && !IsWaitingUser && !HasReport;

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
        StatusLabel = Loc.Instance.T("research.status.cancelled");
        RaiseChanged();
    }

    private bool CanContinue() =>
        !string.IsNullOrEmpty(_runId) && IsWaitingUser && !_submittingReply;

    [RelayCommand(CanExecute = nameof(CanContinue))]
    private async Task ContinueAsync()
    {
        if (string.IsNullOrEmpty(_runId) || !IsWaitingUser || _submittingReply)
        {
            return;
        }

        var reply = UserReply.Trim();
        if (reply.Length == 0)
        {
            // Prefer picking a choice button; free-text is optional supplement.
            StatusLabel = Loc.Instance.T("research.status.pick_or_type");
            RaiseChanged();
            return;
        }

        await SubmitUserReplyAsync(reply);
    }

    [RelayCommand]
    private async Task ChooseAsync(ResearchChoiceOption? option)
    {
        if (option is null || _submittingReply || !IsWaitingUser)
        {
            return;
        }

        var reply = string.IsNullOrWhiteSpace(option.Hint)
            ? option.Label
            : $"{option.Id}: {option.Label} — {option.Hint}";
        await SubmitUserReplyAsync(reply);
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
            PushFeed("system", Loc.Instance.T("research.feed.user_reply"), reply);
            IsWaitingUser = false;
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
        OnPropertyChanged(nameof(ShowBusyStage));
    }

    partial void OnIsRunningChanged(bool value) => OnPropertyChanged(nameof(ShowBusyStage));

    partial void OnHasReportChanged(bool value) => OnPropertyChanged(nameof(ShowBusyStage));

    partial void OnRequirementsLockedChanged(bool value) => OnPropertyChanged(nameof(ShowBusyStage));

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
        HasSummary = false;
        RequirementsLocked = false;
        HasReport = false;
        ReportMarkdown = "";
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
                var text = TruncateUi(ReadString(evt.Payload, "text") ?? "", 280);
                LatestThinking = text;
                // Collapse by default — expanded thinking text thrash-layouts the rail.
                PushFeed("thinking", Loc.Instance.T("research.feed.thinking"), text, expand: false);
                StatusLabel = Loc.Instance.T("research.status.thinking");
                break;
            }
            case "plan":
                // Catalog dumps are huge; never treat as keyword feed rows.
                StatusLabel = Loc.Instance.T("research.status.researching");
                break;
            case "keyword":
            case "searching":
            {
                var kw = TruncateUi(
                    ReadString(evt.Payload, "keyword")
                    ?? ReadString(evt.Payload, "q")
                    ?? "",
                    96);
                if (!string.IsNullOrWhiteSpace(kw))
                {
                    if (Keywords.Count < 24 && !Keywords.Contains(kw))
                    {
                        Keywords.Add(kw);
                    }

                    PushFeed(
                        "keyword",
                        Loc.Instance.T("research.feed.keyword"),
                        kw,
                        TruncateUi(ReadString(evt.Payload, "module_id") ?? "", 24),
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
                // Ignore duplicate clarify while already waiting on the same prompt.
                if (IsWaitingUser && string.Equals(prompt, _lastClarifyPrompt, StringComparison.Ordinal))
                {
                    break;
                }

                IsWaitingUser = true;
                ClarifyPrompt = prompt;
                _lastClarifyPrompt = prompt;
                LoadChoices(evt.Payload);
                PushFeed("system", Loc.Instance.T("research.feed.ask_user"), prompt);
                StatusLabel = Loc.Instance.T("research.status.waiting");
                ContinueCommand.NotifyCanExecuteChanged();
                break;
            }
            case "requirements_locked":
            {
                RequirementsLocked = true;
                IsWaitingUser = false;
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
                if (!string.IsNullOrWhiteSpace(md))
                {
                    ReportMarkdown = md;
                    HasReport = true;
                }

                PushFeed("system", Loc.Instance.T("research.feed.synthesize"),
                    Loc.Instance.T("research.report.title"));
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
                IsFeedExpanded = false;
                StatusLabel = Loc.Instance.T("research.status.cancelled");
                break;
            case "error":
                IsRunning = false;
                IsWaitingUser = false;
                IsFeedExpanded = false;
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
        PushFeed(
            "hit",
            string.IsNullOrWhiteSpace(title) ? url : title,
            snippet,
            string.IsNullOrWhiteSpace(keyword) ? moduleId : keyword,
            expand: false);
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

    private void PushFeed(string kind, string title, string body, string meta = "", bool expand = false)
    {
        title = TruncateUi(title, 120);
        body = TruncateUi(body, 220);
        meta = TruncateUi(meta, 48);

        // Drop exact duplicate spam (same keyword / REST path repeated).
        var fp = kind + "|" + title + "|" + body;
        if (fp == _lastFeedFingerprint && kind is "keyword" or "hit")
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
            IsExpanded = expand,
        });
        while (Feed.Count > 60)
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
        OnPropertyChanged(nameof(ShowBusyStage));
        Changed?.Invoke();
    }
}
