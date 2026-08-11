using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XScope.Services;

namespace XScope.ViewModels;

internal partial class SearchModuleItemViewModel : ObservableObject
{
    private readonly SearchModulesService _service;
    private bool _suppress;

    public SearchModuleItemViewModel(SearchModulesService service, SearchModuleInfo info)
    {
        _service = service;
        Id = info.Id;
        Name = info.Name;
        Description = info.Description;
        RequiresApiKey = info.RequiresApiKey;
        AuthType = info.AuthType;
        SupportsApiKeyEntry = info.SupportsApiKeyEntry;
        SecretConfigured = info.SecretConfigured;
        _enabled = info.Enabled;
    }

    public string Id { get; }
    public string Name { get; }
    public string Description { get; }
    public bool RequiresApiKey { get; }
    public string AuthType { get; }
    public bool SupportsApiKeyEntry { get; }

    [ObservableProperty]
    private bool _secretConfigured;

    [ObservableProperty]
    private bool _enabled;

    [ObservableProperty]
    private string _apiKey = "";

    [ObservableProperty]
    private bool _isBusy;

    [ObservableProperty]
    private string _errorText = "";

    [ObservableProperty]
    private string _statusText = "";

    public string AuthHint
    {
        get
        {
            if (!RequiresApiKey)
            {
                return Loc.Instance.T("search.auth.none");
            }

            if (SupportsApiKeyEntry)
            {
                return SecretConfigured
                    ? Loc.Instance.T("search.auth.ready")
                    : Loc.Instance.T("search.auth.apikey_needed");
            }

            return SecretConfigured
                ? Loc.Instance.T("search.auth.ready")
                : Loc.Instance.T("search.auth.needed");
        }
    }

    partial void OnEnabledChanged(bool value)
    {
        if (_suppress)
        {
            return;
        }

        try
        {
            _service.SetEnabled(Id, value);
            ErrorText = "";
        }
        catch (Exception ex)
        {
            _suppress = true;
            Enabled = !value;
            _suppress = false;
            ErrorText = ex.Message;
        }
    }

    public void NotifyLoc()
    {
        OnPropertyChanged(nameof(AuthHint));
    }

    partial void OnSecretConfiguredChanged(bool value) => OnPropertyChanged(nameof(AuthHint));

    [RelayCommand]
    private async Task SaveApiKeyAsync()
    {
        if (!SupportsApiKeyEntry)
        {
            return;
        }

        if (string.IsNullOrWhiteSpace(ApiKey))
        {
            StatusText = Loc.Instance.T("search.key.empty");
            return;
        }

        IsBusy = true;
        StatusText = Loc.Instance.T("search.key.saving");
        try
        {
            await Task.Run(() => _service.SetApiKey(Id, ApiKey.Trim()));
            ApiKey = "";
            SecretConfigured = true;
            StatusText = Loc.Instance.T("search.key.saved");
            ErrorText = "";
        }
        catch (Exception ex)
        {
            StatusText = "";
            ErrorText = ex.Message;
        }
        finally
        {
            IsBusy = false;
        }
    }
}
