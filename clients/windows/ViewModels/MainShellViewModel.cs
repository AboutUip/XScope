using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using MaterialDesignThemes.Wpf;
using XScope.Services;

namespace XScope.ViewModels;

public enum ResearchPrecisionKind
{
    Quick = 0,
    Normal = 1,
    Deep = 2,
    Maximum = 3,
}

internal sealed class SearchProviderOption
{
    public required string Id { get; init; }
    public required string DisplayName { get; init; }
    public required string PreferredModelId { get; init; }
    public required IReadOnlyList<AiModelInfo> Models { get; init; }
    public string? LogoUri { get; init; }
    public bool HasLogo => !string.IsNullOrWhiteSpace(LogoUri);
    public PackIconKind FallbackIconKind { get; init; } = PackIconKind.CreationOutline;

    public static string? LogoUriForProviderId(string id) =>
        id.Trim().ToLowerInvariant() switch
        {
            "deepseek" => "pack://application:,,,/Assets/Providers/deepseek.png",
            "kimi" => "pack://application:,,,/Assets/Providers/kimi.png",
            _ => null,
        };

    public static PackIconKind FallbackIconForProviderId(string id) =>
        id.Trim().ToLowerInvariant() switch
        {
            "deepseek" => PackIconKind.Shark,
            "kimi" => PackIconKind.MoonWaningCrescent,
            _ => PackIconKind.CreationOutline,
        };
}

internal sealed class SearchModelOption
{
    public required string Id { get; init; }
    public required string DisplayName { get; init; }
}

internal sealed class PrecisionOption
{
    public required ResearchPrecisionKind Kind { get; init; }
    public required string LocKey { get; init; }
    public required string TokenKey { get; init; }
    public required string TimeKey { get; init; }
    public required string DetailKey { get; init; }

    public string DisplayName => Loc.Instance.T(LocKey);
    public string TokenHint => Loc.Instance.T(TokenKey);
    public string TimeHint => Loc.Instance.T(TimeKey);
    public string Detail => Loc.Instance.T(DetailKey);
}

internal partial class ProjectListItem : ObservableObject
{
    public required string Id { get; init; }

    [ObservableProperty]
    private string _title = "";

    [ObservableProperty]
    private bool _pinned;

    public string Subtitle => Pinned ? Loc.Instance.T("project.pinned") : Loc.Instance.T("project.recent");

    partial void OnPinnedChanged(bool value) => OnPropertyChanged(nameof(Subtitle));

    public void NotifyLoc() => OnPropertyChanged(nameof(Subtitle));
}

internal partial class MainShellViewModel : ObservableObject, IDisposable
{
    private readonly ProjectsService _projects = new();
    private readonly AiSettingsService _ai = new();
    private readonly ResearchService _researchService = new();
    private bool _suppressSelection;
    private bool _suppressAiSelection;

    public ObservableCollection<ProjectListItem> Projects { get; } = [];
    public ObservableCollection<SearchProviderOption> UsableProviders { get; } = [];
    public ObservableCollection<SearchModelOption> UsableModels { get; } = [];
    public ObservableCollection<PrecisionOption> Precisions { get; } = [];

    public ResearchProgressViewModel Research { get; }

    [ObservableProperty]
    private ProjectListItem? _selectedProject;

    [ObservableProperty]
    private SearchProviderOption? _selectedProvider;

    [ObservableProperty]
    private SearchModelOption? _selectedModel;

    [ObservableProperty]
    private PrecisionOption? _selectedPrecision;

    [ObservableProperty]
    private string _searchText = "";

    /// <summary>True when showing the Google-like home (ephemeral, not a library row).</summary>
    [ObservableProperty]
    private bool _isHome = true;

    [ObservableProperty]
    private string _activeProjectTitle = "";

    [ObservableProperty]
    private string _researchQuery = "";

    [ObservableProperty]
    private string _statusText = "";

    [ObservableProperty]
    private string _aiOptionsHint = "";

    [ObservableProperty]
    private bool _hasUsableAi;

    [ObservableProperty]
    private bool _showAiOptionsHint;

    [ObservableProperty]
    private int _precisionIndex = 1;

    public Loc L => Loc.Instance;
    public string VersionLabel { get; }
    public string DeveloperLabel { get; } = "小萱baibai";

    public bool IsResearch => !IsHome;

    public string ActiveProviderLabel => SelectedProvider?.DisplayName ?? "—";
    public string ActiveModelLabel => SelectedModel?.DisplayName ?? "—";
    public string ActivePrecisionLabel => SelectedPrecision?.DisplayName ?? "—";
    public string? SelectedProviderLogoUri => SelectedProvider?.LogoUri;
    public bool SelectedProviderHasLogo => SelectedProvider?.HasLogo == true;
    public PackIconKind SelectedProviderFallbackIcon =>
        SelectedProvider?.FallbackIconKind ?? PackIconKind.CreationOutline;

    public ResearchPrecisionKind ActivePrecisionKind =>
        SelectedPrecision?.Kind ?? ResearchPrecisionKind.Normal;

    public string PrecisionTokenHint => SelectedPrecision?.TokenHint ?? "";
    public string PrecisionTimeHint => SelectedPrecision?.TimeHint ?? "";
    public string PrecisionDetail => SelectedPrecision?.Detail ?? "";

    public MainShellViewModel()
    {
        Research = new ResearchProgressViewModel(_researchService);
        Research.Changed += OnResearchChanged;
        var v = typeof(App).Assembly.GetName().Version;
        VersionLabel = v is null ? "v0.1.0" : $"v{v.Major}.{v.Minor}.{v.Build}";
        Loc.Instance.PropertyChanged += OnLocChanged;
        RebuildPrecisions(selectKind: ResearchPrecisionKind.Normal);
        ResetToHome();
        RefreshAiOptions();
        _ = ReloadProjectsAsync();
    }

    public event Action? OpenSettingsRequested;
    public event Func<string, string?, Task<string?>>? PromptRenameRequested;
    public event Func<string, Task<bool>>? ConfirmDeleteRequested;

    /// <summary>Reload providers/models that are actually usable (enabled + key + models).</summary>
    public void RefreshAiOptions()
    {
        try
        {
            var prevProviderId = SelectedProvider?.Id;
            var prevModelId = SelectedModel?.Id;

            var usable = _ai.ListProviders()
                .Where(p => p.Enabled && p.SecretPresent)
                .Select(p => new SearchProviderOption
                {
                    Id = p.Id,
                    DisplayName = string.IsNullOrWhiteSpace(p.Name) ? p.Id : p.Name,
                    PreferredModelId = p.PreferredModelId,
                    Models = p.Models.Where(m => m.Enabled).ToList(),
                    LogoUri = SearchProviderOption.LogoUriForProviderId(p.Id),
                    FallbackIconKind = SearchProviderOption.FallbackIconForProviderId(p.Id),
                })
                .Where(p => p.Models.Count > 0)
                .ToList();

            _suppressAiSelection = true;
            UsableProviders.Clear();
            foreach (var p in usable)
            {
                UsableProviders.Add(p);
            }

            HasUsableAi = UsableProviders.Count > 0;
            var remembered = UiLanguageConfig.ReadPreferredProviderId();
            SelectedProvider = UsableProviders.FirstOrDefault(p => p.Id == prevProviderId)
                ?? UsableProviders.FirstOrDefault(p =>
                    !string.IsNullOrWhiteSpace(remembered) &&
                    string.Equals(p.Id, remembered, StringComparison.OrdinalIgnoreCase))
                ?? UsableProviders.FirstOrDefault();
            ApplyModelsForProvider(SelectedProvider, preferModelId: prevModelId);
            _suppressAiSelection = false;

            UpdateAiOptionsHint();
            NotifyActiveAiLabels();
        }
        catch (Exception ex)
        {
            _suppressAiSelection = false;
            HasUsableAi = false;
            UsableProviders.Clear();
            UsableModels.Clear();
            SelectedProvider = null;
            SelectedModel = null;
            AiOptionsHint = ex.Message;
            ShowAiOptionsHint = true;
            NotifyActiveAiLabels();
        }
    }

    [RelayCommand]
    private void OpenSettings() => OpenSettingsRequested?.Invoke();

    /// <summary>New project: return to ephemeral home — do not create a library row.</summary>
    [RelayCommand]
    private void NewProject() => ResetToHome();

    [RelayCommand]
    private async Task SubmitSearchAsync()
    {
        var q = SearchText.Trim();
        if (q.Length == 0)
        {
            StatusText = Loc.Instance.T("main.search.empty");
            return;
        }

        if (SelectedProvider is null || SelectedModel is null)
        {
            StatusText = Loc.Instance.T("main.search.need_ai");
            ShowAiOptionsHint = true;
            UpdateAiOptionsHint();
            return;
        }

        try
        {
            string projectId;
            if (IsHome || SelectedProject is null)
            {
                var title = ProjectsService.TitleFromSearch(q);
                var created = await Task.Run(() => _projects.Create(title));
                await ReloadProjectsAsync(created.Id);
                EnterResearch(created.Id, created.Title, q);
                projectId = created.Id;
            }
            else
            {
                ResearchQuery = q;
                projectId = SelectedProject.Id;
                StatusText = Loc.Instance.T("main.research.queued");
            }

            NotifyActiveAiLabels();
            StatusText = Loc.Instance.T("research.status.starting");
            await Research.StartAsync(
                projectId,
                q,
                SelectedModel.Id,
                (int)ActivePrecisionKind);
            StatusText = Research.StatusLabel;
        }
        catch (Exception ex)
        {
            StatusText = ex.Message;
        }
    }

    [RelayCommand]
    private async Task TogglePinAsync(ProjectListItem? item)
    {
        if (item is null || string.IsNullOrEmpty(item.Id))
        {
            return;
        }

        try
        {
            await Task.Run(() => _projects.SetPinned(item.Id, !item.Pinned));
            await ReloadProjectsAsync(SelectedProject?.Id);
        }
        catch (Exception ex)
        {
            StatusText = ex.Message;
        }
    }

    [RelayCommand]
    private async Task RenameAsync(ProjectListItem? item)
    {
        if (item is null || string.IsNullOrEmpty(item.Id) || PromptRenameRequested is null)
        {
            return;
        }

        var next = await PromptRenameRequested(item.Title, Loc.Instance.T("project.rename.prompt"));
        if (string.IsNullOrWhiteSpace(next) || next.Trim() == item.Title)
        {
            return;
        }

        try
        {
            var title = ProjectsService.TitleFromSearch(next);
            await Task.Run(() => _projects.Rename(item.Id, title));
            await ReloadProjectsAsync(item.Id);
            if (SelectedProject?.Id == item.Id)
            {
                ActiveProjectTitle = title;
            }
        }
        catch (Exception ex)
        {
            StatusText = ex.Message;
        }
    }

    [RelayCommand]
    private async Task DeleteAsync(ProjectListItem? item)
    {
        if (item is null || string.IsNullOrEmpty(item.Id))
        {
            return;
        }

        if (ConfirmDeleteRequested is not null)
        {
            var ok = await ConfirmDeleteRequested(item.Title);
            if (!ok)
            {
                return;
            }
        }

        try
        {
            var deletingActive = SelectedProject?.Id == item.Id;
            await Task.Run(() => _projects.Delete(item.Id));
            if (deletingActive)
            {
                ResetToHome();
            }

            await ReloadProjectsAsync(deletingActive ? null : SelectedProject?.Id);
        }
        catch (Exception ex)
        {
            StatusText = ex.Message;
        }
    }

    partial void OnSelectedProjectChanged(ProjectListItem? value)
    {
        if (_suppressSelection)
        {
            return;
        }

        if (value is null)
        {
            return;
        }

        EnterResearch(value.Id, value.Title, ResearchQuery);
    }

    partial void OnIsHomeChanged(bool value)
    {
        OnPropertyChanged(nameof(IsResearch));
    }

    partial void OnSelectedProviderChanged(SearchProviderOption? value)
    {
        if (_suppressAiSelection)
        {
            return;
        }

        if (value is not null)
        {
            UiLanguageConfig.SavePreferredProviderId(value.Id);
        }

        ApplyModelsForProvider(value, preferModelId: value?.PreferredModelId);
        UpdateAiOptionsHint();
        NotifyActiveAiLabels();
    }

    partial void OnSelectedModelChanged(SearchModelOption? value)
    {
        NotifyActiveAiLabels();
        if (_suppressAiSelection || value is null || SelectedProvider is null)
        {
            return;
        }

        try
        {
            _ai.SetPreferredModel(SelectedProvider.Id, value.Id);
        }
        catch (Exception ex)
        {
            StatusText = ex.Message;
        }
    }

    partial void OnSelectedPrecisionChanged(PrecisionOption? value)
    {
        if (value is not null && PrecisionIndex != (int)value.Kind)
        {
            PrecisionIndex = (int)value.Kind;
        }

        NotifyActiveAiLabels();
        OnPropertyChanged(nameof(ActivePrecisionKind));
    }

    partial void OnPrecisionIndexChanged(int value)
    {
        value = Math.Clamp(value, 0, 3);
        var match = Precisions.FirstOrDefault(p => (int)p.Kind == value);
        if (match is not null && !ReferenceEquals(SelectedPrecision, match))
        {
            SelectedPrecision = match;
        }

        OnPropertyChanged(nameof(ActivePrecisionKind));
        NotifyActiveAiLabels();
    }

    private void ApplyModelsForProvider(SearchProviderOption? provider, string? preferModelId)
    {
        var wasSuppressed = _suppressAiSelection;
        _suppressAiSelection = true;
        try
        {
            UsableModels.Clear();
            if (provider is null)
            {
                SelectedModel = null;
                return;
            }

            foreach (var m in provider.Models)
            {
                UsableModels.Add(new SearchModelOption
                {
                    Id = m.Id,
                    DisplayName = string.IsNullOrWhiteSpace(m.Name) ? m.Model : m.Name,
                });
            }

            var preferred = preferModelId;
            if (string.IsNullOrWhiteSpace(preferred))
            {
                preferred = provider.PreferredModelId;
            }

            SelectedModel = UsableModels.FirstOrDefault(m => m.Id == preferred)
                ?? UsableModels.FirstOrDefault();
        }
        finally
        {
            _suppressAiSelection = wasSuppressed;
        }
    }

    private void UpdateAiOptionsHint()
    {
        if (HasUsableAi)
        {
            AiOptionsHint = "";
            ShowAiOptionsHint = false;
            return;
        }

        AiOptionsHint = Loc.Instance.T("main.search.need_ai");
        ShowAiOptionsHint = true;
    }

    private void RebuildPrecisions(ResearchPrecisionKind? selectKind)
    {
        var keep = selectKind ?? SelectedPrecision?.Kind ?? ResearchPrecisionKind.Normal;
        var items = new[]
        {
            new PrecisionOption
            {
                Kind = ResearchPrecisionKind.Quick,
                LocKey = "main.precision.quick",
                TokenKey = "main.precision.quick.token",
                TimeKey = "main.precision.quick.time",
                DetailKey = "main.precision.quick.detail",
            },
            new PrecisionOption
            {
                Kind = ResearchPrecisionKind.Normal,
                LocKey = "main.precision.normal",
                TokenKey = "main.precision.normal.token",
                TimeKey = "main.precision.normal.time",
                DetailKey = "main.precision.normal.detail",
            },
            new PrecisionOption
            {
                Kind = ResearchPrecisionKind.Deep,
                LocKey = "main.precision.deep",
                TokenKey = "main.precision.deep.token",
                TimeKey = "main.precision.deep.time",
                DetailKey = "main.precision.deep.detail",
            },
            new PrecisionOption
            {
                Kind = ResearchPrecisionKind.Maximum,
                LocKey = "main.precision.max",
                TokenKey = "main.precision.max.token",
                TimeKey = "main.precision.max.time",
                DetailKey = "main.precision.max.detail",
            },
        };

        Precisions.Clear();
        foreach (var p in items)
        {
            Precisions.Add(p);
        }

        SelectedPrecision = Precisions.FirstOrDefault(p => p.Kind == keep) ?? Precisions[1];
        PrecisionIndex = (int)(SelectedPrecision?.Kind ?? ResearchPrecisionKind.Normal);
        NotifyActiveAiLabels();
        OnPropertyChanged(nameof(ActivePrecisionKind));
    }

    private void NotifyActiveAiLabels()
    {
        OnPropertyChanged(nameof(ActiveProviderLabel));
        OnPropertyChanged(nameof(ActiveModelLabel));
        OnPropertyChanged(nameof(ActivePrecisionLabel));
        OnPropertyChanged(nameof(SelectedProviderLogoUri));
        OnPropertyChanged(nameof(SelectedProviderHasLogo));
        OnPropertyChanged(nameof(SelectedProviderFallbackIcon));
        OnPropertyChanged(nameof(PrecisionTokenHint));
        OnPropertyChanged(nameof(PrecisionTimeHint));
        OnPropertyChanged(nameof(PrecisionDetail));
    }

    private void ResetToHome()
    {
        _ = Research.StopPollingAsync(cancelRun: true);
        _suppressSelection = true;
        SelectedProject = null;
        _suppressSelection = false;
        IsHome = true;
        SearchText = "";
        ResearchQuery = "";
        ActiveProjectTitle = "";
        StatusText = "";
        UpdateAiOptionsHint();
    }

    private void EnterResearch(string id, string title, string query)
    {
        IsHome = false;
        ActiveProjectTitle = title;
        if (!string.IsNullOrWhiteSpace(query))
        {
            ResearchQuery = query.Trim();
            SearchText = query.Trim();
        }

        StatusText = Loc.Instance.T("main.research.ready");

        _suppressSelection = true;
        SelectedProject = Projects.FirstOrDefault(p => p.Id == id);
        _suppressSelection = false;
        NotifyActiveAiLabels();
    }

    private async Task ReloadProjectsAsync(string? selectId = null)
    {
        try
        {
            var list = await Task.Run(() => _projects.List());
            _suppressSelection = true;
            Projects.Clear();
            foreach (var p in list)
            {
                Projects.Add(new ProjectListItem
                {
                    Id = p.Id,
                    Title = p.Title,
                    Pinned = p.Pinned,
                });
            }

            if (!string.IsNullOrEmpty(selectId))
            {
                SelectedProject = Projects.FirstOrDefault(x => x.Id == selectId);
            }
            else if (IsHome)
            {
                SelectedProject = null;
            }
            else if (SelectedProject is not null)
            {
                SelectedProject = Projects.FirstOrDefault(x => x.Id == SelectedProject.Id);
            }

            _suppressSelection = false;
        }
        catch (Exception ex)
        {
            StatusText = ex.Message;
            _suppressSelection = false;
        }
    }

    private void OnResearchChanged()
    {
        if (!string.IsNullOrWhiteSpace(Research.StatusLabel))
        {
            StatusText = Research.StatusLabel;
        }
    }

    private void OnLocChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        OnPropertyChanged(nameof(L));
        foreach (var p in Projects)
        {
            p.NotifyLoc();
        }

        RebuildPrecisions(SelectedPrecision?.Kind);
        UpdateAiOptionsHint();
        NotifyActiveAiLabels();
    }

    public void Dispose()
    {
        Loc.Instance.PropertyChanged -= OnLocChanged;
        Research.Changed -= OnResearchChanged;
        _ = Research.StopPollingAsync(cancelRun: true);
        _projects.Dispose();
        _ai.Dispose();
        _researchService.Dispose();
    }
}
