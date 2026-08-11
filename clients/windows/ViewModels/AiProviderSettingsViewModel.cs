using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XScope.Services;

namespace XScope.ViewModels;

internal sealed class AiModelChoice
{
    public required string Id { get; init; }
    public required string DisplayName { get; init; }
}

internal partial class AiProviderSettingsViewModel : ObservableObject
{
    private readonly AiSettingsService _service;
    private bool _suppressCapSave;

    public AiProviderSettingsViewModel(AiSettingsService service, AiProviderStatus status)
    {
        _service = service;
        Id = status.Id;
        Name = status.Name;
        SecretId = status.SecretId;
        ApplyStatus(status);
    }

    public string Id { get; }
    public string Name { get; }
    public string SecretId { get; }

    public ObservableCollection<AiModelChoice> Models { get; } = [];

    [ObservableProperty]
    private string _apiKey = "";

    [ObservableProperty]
    private bool _secretPresent;

    [ObservableProperty]
    private bool _isBusy;

    [ObservableProperty]
    private string _statusText = "";

    [ObservableProperty]
    private AiModelChoice? _selectedModel;

    [ObservableProperty]
    private bool _hasModels;

    /// <summary>Text chat is always on for AI providers.</summary>
    public bool CapChat => true;

    [ObservableProperty]
    private bool _capImageInput;

    [ObservableProperty]
    private bool _capVideoInput;

    public Loc L => Loc.Instance;

    public string SecretHint => SecretPresent
        ? Loc.Instance.T("ai.key.configured")
        : Loc.Instance.T("ai.key.missing");

    public void NotifyLoc()
    {
        OnPropertyChanged(nameof(L));
        OnPropertyChanged(nameof(SecretHint));
    }

    partial void OnSelectedModelChanged(AiModelChoice? value)
    {
        if (value is null || IsBusy || string.IsNullOrEmpty(value.Id))
        {
            return;
        }

        try
        {
            _service.SetPreferredModel(Id, value.Id);
            StatusText = Loc.Instance.T("ai.model.saved");
        }
        catch (Exception ex)
        {
            StatusText = ex.Message;
        }
    }

    partial void OnCapImageInputChanged(bool value) => PersistCapabilities();

    partial void OnCapVideoInputChanged(bool value) => PersistCapabilities();

    public void ApplyStatus(AiProviderStatus status)
    {
        SecretPresent = status.SecretPresent;
        OnPropertyChanged(nameof(SecretHint));
        ApplyCapabilities(status.ModelCapabilities);
        ReplaceModels(status.Models, status.PreferredModelId);
    }

    public void ApplyModels(AiModelsResult result)
    {
        SecretPresent = true;
        OnPropertyChanged(nameof(SecretHint));
        if (result.ModelCapabilities.Count > 0)
        {
            ApplyCapabilities(result.ModelCapabilities);
        }

        ReplaceModels(result.Models, result.PreferredModelId);
        StatusText = Loc.Instance.T("ai.models.refreshed");
    }

    private void ApplyCapabilities(IReadOnlyList<string> caps)
    {
        _suppressCapSave = true;
        try
        {
            CapImageInput = AiModelCapabilities.Has(caps, AiModelCapabilities.ImageInput);
            CapVideoInput = AiModelCapabilities.Has(caps, AiModelCapabilities.VideoInput);
        }
        finally
        {
            _suppressCapSave = false;
        }
    }

    private void PersistCapabilities()
    {
        if (_suppressCapSave || IsBusy)
        {
            return;
        }

        try
        {
            var caps = new List<string> { AiModelCapabilities.Chat };
            if (CapImageInput)
            {
                caps.Add(AiModelCapabilities.ImageInput);
            }

            if (CapVideoInput)
            {
                caps.Add(AiModelCapabilities.VideoInput);
            }

            var saved = _service.SetModelCapabilities(Id, caps);
            ApplyCapabilities(saved);
            StatusText = Loc.Instance.T("ai.caps.saved");
        }
        catch (Exception ex)
        {
            StatusText = ex.Message;
        }
    }

    private void ReplaceModels(IReadOnlyList<AiModelInfo> models, string preferredId)
    {
        var wasBusy = IsBusy;
        IsBusy = true;
        try
        {
            Models.Clear();
            foreach (var m in models)
            {
                Models.Add(new AiModelChoice
                {
                    Id = m.Id,
                    DisplayName = string.IsNullOrWhiteSpace(m.Name) ? m.Model : m.Name,
                });
            }

            HasModels = Models.Count > 0;
            SelectedModel = Models.FirstOrDefault(x => x.Id == preferredId) ?? Models.FirstOrDefault();
        }
        finally
        {
            IsBusy = wasBusy;
        }
    }

    [RelayCommand]
    private async Task SaveApiKeyAsync()
    {
        if (string.IsNullOrWhiteSpace(ApiKey))
        {
            StatusText = Loc.Instance.T("ai.key.empty");
            return;
        }

        IsBusy = true;
        StatusText = Loc.Instance.T("ai.key.saving");
        try
        {
            var result = await Task.Run(() => _service.SetApiKey(Id, ApiKey.Trim()));
            ApiKey = "";
            ApplyModels(result);
            StatusText = Loc.Instance.T("ai.key.saved");
        }
        catch (Exception ex)
        {
            StatusText = ex.Message;
        }
        finally
        {
            IsBusy = false;
        }
    }

    [RelayCommand]
    private async Task RefreshModelsAsync()
    {
        if (!SecretPresent)
        {
            StatusText = Loc.Instance.T("ai.key.missing");
            return;
        }

        IsBusy = true;
        StatusText = Loc.Instance.T("ai.models.refreshing");
        try
        {
            var result = await Task.Run(() => _service.RefreshModels(Id));
            ApplyModels(result);
        }
        catch (Exception ex)
        {
            StatusText = ex.Message;
        }
        finally
        {
            IsBusy = false;
        }
    }
}
