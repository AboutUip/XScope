using System.Collections.ObjectModel;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XScope.Services;

namespace XScope.ViewModels;

internal partial class ResearchSidePanelViewModel : ObservableObject
{
    private readonly ResearchService _research;
    private string _projectId = "";
    private int _loadSerial;

    [ObservableProperty]
    private bool _isExpanded = true;

    [ObservableProperty]
    private string _graphJson = "";

    [ObservableProperty]
    private bool _isLoading;

    [ObservableProperty]
    private string _statusHint = "";

    [ObservableProperty]
    private int _nodeCount;

    [ObservableProperty]
    private int _edgeCount;

    public Loc L => Loc.Instance;

    public bool HasGraph => NodeCount > 0;
    public bool IsEmpty => !IsLoading && NodeCount == 0;

    public string StatsLabel =>
        string.Format(Loc.Instance.T("side.panel.stats"), NodeCount, EdgeCount);

    public ResearchSidePanelViewModel(ResearchService research)
    {
        _research = research;
    }

    partial void OnIsExpandedChanged(bool value) => ExpandedChanged?.Invoke();

    partial void OnNodeCountChanged(int value)
    {
        OnPropertyChanged(nameof(HasGraph));
        OnPropertyChanged(nameof(IsEmpty));
        OnPropertyChanged(nameof(StatsLabel));
    }

    partial void OnEdgeCountChanged(int value) => OnPropertyChanged(nameof(StatsLabel));

    partial void OnIsLoadingChanged(bool value) => OnPropertyChanged(nameof(IsEmpty));

    public event Action? ExpandedChanged;

    public void Clear()
    {
        _projectId = "";
        GraphJson = "";
        NodeCount = 0;
        EdgeCount = 0;
        StatusHint = "";
        IsLoading = false;
        IsExpanded = true;
    }

    public void NotifyLoc()
    {
        OnPropertyChanged(nameof(L));
        OnPropertyChanged(nameof(StatsLabel));
    }

    public async Task EnsureLoadedAsync(string projectId, string? cachedGraphJson = null)
    {
        if (string.IsNullOrWhiteSpace(projectId))
        {
            Clear();
            return;
        }

        var sameProject = string.Equals(_projectId, projectId, StringComparison.Ordinal);
        _projectId = projectId;

        if (!string.IsNullOrWhiteSpace(cachedGraphJson) &&
            (!sameProject || string.IsNullOrWhiteSpace(GraphJson)))
        {
            ApplyGraphJson(cachedGraphJson);
        }

        var serial = ++_loadSerial;
        IsLoading = true;
        StatusHint = Loc.Instance.T("side.panel.loading");
        try
        {
            var json = await Task.Run(() => _research.GetKnowledgeGraphJson(projectId));
            if (serial != _loadSerial)
            {
                return;
            }

            ApplyGraphJson(json);
            StatusHint = HasGraph
                ? StatsLabel
                : Loc.Instance.T("side.panel.empty");
        }
        catch (Exception ex)
        {
            if (serial != _loadSerial)
            {
                return;
            }

            if (string.IsNullOrWhiteSpace(GraphJson))
            {
                StatusHint = ex.Message;
            }
        }
        finally
        {
            if (serial == _loadSerial)
            {
                IsLoading = false;
            }
        }
    }

    public void ApplyGraphJson(string? json)
    {
        if (string.IsNullOrWhiteSpace(json))
        {
            NodeCount = 0;
            EdgeCount = 0;
            GraphJson = "";
            return;
        }

        try
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;
            if (root.TryGetProperty("graph", out var nested) && nested.ValueKind == JsonValueKind.Object)
            {
                root = nested;
            }

            var nodeCount = 0;
            if (root.TryGetProperty("nodes", out var nodes) && nodes.ValueKind == JsonValueKind.Array)
            {
                foreach (var n in nodes.EnumerateArray())
                {
                    var id = ReadString(n, "id");
                    if (!string.IsNullOrWhiteSpace(id))
                    {
                        nodeCount++;
                    }
                }
            }

            var edgeCount = 0;
            if (root.TryGetProperty("edges", out var edges) && edges.ValueKind == JsonValueKind.Array)
            {
                foreach (var e in edges.EnumerateArray())
                {
                    var from = ReadString(e, "from_id") ?? ReadString(e, "from");
                    var to = ReadString(e, "to_id") ?? ReadString(e, "to");
                    if (!string.IsNullOrWhiteSpace(from) && !string.IsNullOrWhiteSpace(to))
                    {
                        edgeCount++;
                    }
                }
            }

            NodeCount = nodeCount;
            EdgeCount = edgeCount;
            GraphJson = root.GetRawText();
            OnPropertyChanged(nameof(StatsLabel));
            OnPropertyChanged(nameof(HasGraph));
            OnPropertyChanged(nameof(IsEmpty));
        }
        catch
        {
            // Keep prior snapshot if parse fails.
        }
    }

    [RelayCommand]
    private void Expand() => IsExpanded = true;

    [RelayCommand]
    private void Collapse() => IsExpanded = false;

    [RelayCommand]
    private void ToggleExpanded() => IsExpanded = !IsExpanded;

    [RelayCommand]
    private async Task RefreshAsync()
    {
        if (string.IsNullOrWhiteSpace(_projectId))
        {
            return;
        }

        await EnsureLoadedAsync(_projectId);
    }

    private static string? ReadString(JsonElement el, string name) =>
        el.TryGetProperty(name, out var p) && p.ValueKind == JsonValueKind.String
            ? p.GetString()
            : null;
}
