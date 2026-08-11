using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XScope.Services;

namespace XScope.ViewModels;

internal enum SettingsSection
{
    GitHub,
    Search,
    Ai,
    Language,
    About,
}

internal sealed class LanguageChoice
{
    public required AppLanguage Id { get; init; }
    public required string Code { get; init; }

    /// <summary>Native label for the option (stable across UI language).</summary>
    public string DisplayName => Id == AppLanguage.ChineseSimplified ? "中文（简体）" : "English";
}

internal sealed class SettingsSearchHit
{
    public required SettingsSection Section { get; init; }
    public required string Title { get; init; }
    public required string Category { get; init; }
    public required string Snippet { get; init; }
}

internal sealed class SettingsSearchEntry
{
    public required SettingsSection Section { get; init; }
    public required string TitleKey { get; init; }
    public required string SnippetKey { get; init; }
    public required string[] Keywords { get; init; }

    public bool Matches(string query)
    {
        if (string.IsNullOrWhiteSpace(query))
        {
            return true;
        }

        var q = query.Trim();
        // All space-separated tokens must match (Google-like narrowing).
        var tokens = q.Split([' ', '\t', '、', ',', ';'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (tokens.Length == 0)
        {
            return true;
        }

        return tokens.All(TokenMatches);
    }

    private bool TokenMatches(string token)
    {
        var loc = Loc.Instance;
        if (Contains(loc.T(TitleKey), token) || Contains(loc.T(SnippetKey), token) ||
            Contains(CategoryName(Section), token))
        {
            return true;
        }

        foreach (var kw in Keywords)
        {
            if (Contains(kw, token) || Contains(token, kw))
            {
                return true;
            }
        }

        return false;
    }

    public SettingsSearchHit ToHit()
    {
        var loc = Loc.Instance;
        return new SettingsSearchHit
        {
            Section = Section,
            Title = loc.T(TitleKey),
            Category = CategoryName(Section),
            Snippet = loc.T(SnippetKey),
        };
    }

    private static string CategoryName(SettingsSection section) => section switch
    {
        SettingsSection.GitHub => Loc.Instance.NavGitHub,
        SettingsSection.Search => Loc.Instance.NavSearch,
        SettingsSection.Ai => Loc.Instance.NavAi,
        SettingsSection.Language => Loc.Instance.NavLanguage,
        SettingsSection.About => Loc.Instance.NavAbout,
        _ => section.ToString(),
    };

    private static bool Contains(string haystack, string needle) =>
        haystack.Contains(needle, StringComparison.OrdinalIgnoreCase);
}

internal partial class SettingsViewModel : ObservableObject, IDisposable
{
    private static readonly SettingsSearchEntry[] Catalog =
    [
        new()
        {
            Section = SettingsSection.GitHub,
            TitleKey = "github.status",
            SnippetKey = "github.description",
            Keywords =
            [
                "github", "status", "connect", "disconnect", "oauth", "device flow", "account", "login",
                "状态", "连接", "断开", "账号", "登录", "授权",
            ],
        },
        new()
        {
            Section = SettingsSection.GitHub,
            TitleKey = "github.client_id",
            SnippetKey = "github.client_id.hint",
            Keywords =
            [
                "client id", "oauth app", "device flow", "github apps", "ov23",
                "客户端", "应用", "device",
            ],
        },
        new()
        {
            Section = SettingsSection.GitHub,
            TitleKey = "github.pat",
            SnippetKey = "github.pat.hint",
            Keywords =
            [
                "pat", "token", "personal access token", "ghp", "fine-grained",
                "令牌", "个人访问令牌", "密钥",
            ],
        },
        new()
        {
            Section = SettingsSection.Search,
            TitleKey = "search.title",
            SnippetKey = "search.description",
            Keywords =
            [
                "search", "modules", "research", "enable", "provider", "github search",
                "搜索", "模块", "调研", "启用",
            ],
        },
        new()
        {
            Section = SettingsSection.Ai,
            TitleKey = "ai.title",
            SnippetKey = "ai.description",
            Keywords =
            [
                "ai", "llm", "model", "provider", "api key", "secret",
                "模型", "提供商", "接口", "密钥",
            ],
        },
        new()
        {
            Section = SettingsSection.Ai,
            TitleKey = "ai.deepseek.name",
            SnippetKey = "ai.deepseek.hint",
            Keywords = ["deepseek", "deep seek", "deepseek.default"],
        },
        new()
        {
            Section = SettingsSection.Ai,
            TitleKey = "ai.kimi.name",
            SnippetKey = "ai.kimi.hint",
            Keywords = ["kimi", "moonshot", "kimi.default", "月之暗面"],
        },
        new()
        {
            Section = SettingsSection.Language,
            TitleKey = "language.display",
            SnippetKey = "language.display.hint",
            Keywords =
            [
                "language", "locale", "english", "chinese", "zh", "en", "i18n", "display language",
                "语言", "中文", "英文", "简体", "界面语言",
            ],
        },
        new()
        {
            Section = SettingsSection.About,
            TitleKey = "about.version",
            SnippetKey = "about.title",
            Keywords = ["version", "about", "release", "版本", "关于"],
        },
        new()
        {
            Section = SettingsSection.About,
            TitleKey = "about.developer",
            SnippetKey = "about.title",
            Keywords = ["developer", "author", "credit", "小萱baibai", "baibai", "xuan", "开发者", "作者"],
        },
    ];

    private readonly AiSettingsService _aiService = new();
    private readonly SearchModulesService _searchService = new();

    public GithubLoginViewModel GitHub { get; } = new();
    public Loc L => Loc.Instance;

    public ObservableCollection<AiProviderSettingsViewModel> AiProviders { get; } = [];
    public ObservableCollection<SearchModuleItemViewModel> SearchModules { get; } = [];

    public IReadOnlyList<LanguageChoice> LanguageChoices { get; } =
    [
        new() { Id = AppLanguage.English, Code = "en" },
        new() { Id = AppLanguage.ChineseSimplified, Code = "zh-Hans" },
    ];

    public ObservableCollection<SettingsSearchHit> SearchResults { get; } = [];

    [ObservableProperty]
    private SettingsSection _selectedSection = SettingsSection.GitHub;

    [ObservableProperty]
    private string _settingsSearch = "";

    [ObservableProperty]
    private LanguageChoice? _selectedLanguage;

    [ObservableProperty]
    private bool _isSearching;

    [ObservableProperty]
    private bool _hasSearchResults;

    [ObservableProperty]
    private string _searchEmptyText = "";

    [ObservableProperty]
    private bool _hasSearchModules;

    [ObservableProperty]
    private string _searchModulesEmptyText = "";

    public string VersionLabel { get; }
    public string DeveloperLabel { get; } = "小萱baibai";

    public SettingsViewModel()
    {
        var v = typeof(App).Assembly.GetName().Version;
        VersionLabel = v is null ? "v0.1.0" : $"v{v.Major}.{v.Minor}.{v.Build}";
        SelectedLanguage = LanguageChoices.First(c => c.Id == Loc.Instance.Language);
        Loc.Instance.PropertyChanged += OnLocChanged;
        RefreshSearch();
    }

    public bool IsGitHub => !IsSearching && SelectedSection == SettingsSection.GitHub;
    public bool IsSearch => !IsSearching && SelectedSection == SettingsSection.Search;
    public bool IsAi => !IsSearching && SelectedSection == SettingsSection.Ai;
    public bool IsLanguage => !IsSearching && SelectedSection == SettingsSection.Language;
    public bool IsAbout => !IsSearching && SelectedSection == SettingsSection.About;

    public bool ShowNavGitHub => !IsSearching || SectionHasHits(SettingsSection.GitHub);
    public bool ShowNavSearch => !IsSearching || SectionHasHits(SettingsSection.Search);
    public bool ShowNavAi => !IsSearching || SectionHasHits(SettingsSection.Ai);
    public bool ShowNavLanguage => !IsSearching || SectionHasHits(SettingsSection.Language);
    public bool ShowNavAbout => !IsSearching || SectionHasHits(SettingsSection.About);

    public async Task InitializeAsync()
    {
        await GitHub.InitializeAsync();
        await Task.Run(LoadAiAndSearch);
    }

    private void LoadAiAndSearch()
    {
        var providers = _aiService.ListProviders();
        var modules = _searchService.List();
        System.Windows.Application.Current.Dispatcher.Invoke(() =>
        {
            AiProviders.Clear();
            foreach (var p in providers)
            {
                AiProviders.Add(new AiProviderSettingsViewModel(_aiService, p));
            }

            SearchModules.Clear();
            foreach (var m in modules)
            {
                SearchModules.Add(new SearchModuleItemViewModel(_searchService, m));
            }

            HasSearchModules = SearchModules.Count > 0;
            SearchModulesEmptyText = HasSearchModules ? "" : Loc.Instance.T("search.empty");
        });
    }

    partial void OnSelectedSectionChanged(SettingsSection value) => NotifySectionFlags();

    partial void OnSettingsSearchChanged(string value) => RefreshSearch();

    partial void OnIsSearchingChanged(bool value) => NotifySectionFlags();

    partial void OnSelectedLanguageChanged(LanguageChoice? value)
    {
        if (value is null || value.Id == Loc.Instance.Language)
        {
            return;
        }

        Loc.Instance.SetLanguage(value.Id);
    }

    private void OnLocChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        OnPropertyChanged(nameof(L));
        SelectedLanguage = LanguageChoices.First(c => c.Id == Loc.Instance.Language);
        foreach (var m in SearchModules)
        {
            m.NotifyLoc();
        }

        foreach (var p in AiProviders)
        {
            p.NotifyLoc();
        }

        if (!HasSearchModules)
        {
            SearchModulesEmptyText = Loc.Instance.T("search.empty");
        }

        RefreshSearch();
    }

    private void RefreshSearch()
    {
        var q = SettingsSearch.Trim();
        SearchResults.Clear();
        if (q.Length == 0)
        {
            IsSearching = false;
            HasSearchResults = false;
            SearchEmptyText = "";
            NotifyNavFlags();
            return;
        }

        IsSearching = true;
        foreach (var entry in Catalog)
        {
            if (entry.Matches(q))
            {
                SearchResults.Add(entry.ToHit());
            }
        }

        HasSearchResults = SearchResults.Count > 0;
        SearchEmptyText = HasSearchResults
            ? ""
            : string.Format(Loc.Instance.T("settings.search.no_results"), q);
        NotifyNavFlags();
        NotifySectionFlags();
    }

    private bool SectionHasHits(SettingsSection section) =>
        SearchResults.Any(r => r.Section == section);

    private void NotifyNavFlags()
    {
        OnPropertyChanged(nameof(ShowNavGitHub));
        OnPropertyChanged(nameof(ShowNavSearch));
        OnPropertyChanged(nameof(ShowNavAi));
        OnPropertyChanged(nameof(ShowNavLanguage));
        OnPropertyChanged(nameof(ShowNavAbout));
    }

    private void NotifySectionFlags()
    {
        OnPropertyChanged(nameof(IsGitHub));
        OnPropertyChanged(nameof(IsSearch));
        OnPropertyChanged(nameof(IsAi));
        OnPropertyChanged(nameof(IsLanguage));
        OnPropertyChanged(nameof(IsAbout));
    }

    [RelayCommand]
    private void SelectSection(SettingsSection section)
    {
        SettingsSearch = "";
        SelectedSection = section;
    }

    [RelayCommand]
    private void OpenSearchHit(SettingsSearchHit? hit)
    {
        if (hit is null)
        {
            return;
        }

        SettingsSearch = "";
        SelectedSection = hit.Section;
    }

    [RelayCommand]
    private void ClearSearch() => SettingsSearch = "";

    public void Dispose()
    {
        Loc.Instance.PropertyChanged -= OnLocChanged;
        GitHub.Dispose();
        _aiService.Dispose();
        _searchService.Dispose();
    }
}
