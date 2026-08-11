using System.Windows;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using XScope.Services;

namespace XScope.ViewModels;

internal partial class GithubLoginViewModel : ObservableObject, IDisposable
{
    private GithubAuthService? _auth;
    private CancellationTokenSource? _pollCts;
    private string _deviceCode = "";

    [ObservableProperty]
    private string _statusText = Loc.Instance.T("gh.checking");

    [ObservableProperty]
    private string _detailText = "";

    [ObservableProperty]
    private string _userCode = "";

    [ObservableProperty]
    private string _verificationUri = "https://github.com/login/device";

    [ObservableProperty]
    private string _patToken = "";

    [ObservableProperty]
    private string _clientId = "";

    [ObservableProperty]
    private string _dataRootText = "";

    [ObservableProperty]
    private bool _isConnected;

    [ObservableProperty]
    private bool _isBusy;

    [ObservableProperty]
    private bool _isPolling;

    [ObservableProperty]
    private bool _clientIdConfigured;

    [ObservableProperty]
    private bool _showOauthSetup = true;

    public GithubLoginViewModel()
    {
        DataRootText = AppPaths.DataRoot;
        ClientId = GithubOauthConfig.ReadClientId();
        ClientIdConfigured = !string.IsNullOrWhiteSpace(ClientId);
        ShowOauthSetup = !ClientIdConfigured;
        StatusText = Loc.Instance.T("gh.checking");
        Loc.Instance.PropertyChanged += OnLocChanged;
    }

    public async Task InitializeAsync()
    {
        try
        {
            ClientId = GithubOauthConfig.ReadClientId();
            await Task.Run(EnsureAuth).ConfigureAwait(true);
            RefreshStatus();
        }
        catch (Exception ex)
        {
            StatusText = Loc.Instance.T("gh.sdk_unavailable");
            DetailText = ex.Message;
            IsConnected = false;
        }
    }

    private void OnLocChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        try
        {
            RefreshStatus();
        }
        catch
        {
            // ignore while SDK unavailable
        }
    }

    private GithubAuthService EnsureAuth()
    {
        _auth ??= new GithubAuthService();
        return _auth;
    }

    [RelayCommand]
    private void RefreshStatus()
    {
        try
        {
            var status = EnsureAuth().Status();
            ApplyStatus(status);
        }
        catch (Exception ex)
        {
            StatusText = "SDK unavailable";
            DetailText = ex.Message;
            IsConnected = false;
        }
    }

    [RelayCommand]
    private void SaveClientId()
    {
        try
        {
            GithubOauthConfig.SaveClientId(ClientId);
            ClientIdConfigured = true;
            ShowOauthSetup = false;
            DetailText = string.Format(Loc.Instance.T("gh.saved_client"), GithubOauthConfig.ConfigPath);
            RefreshStatus();
        }
        catch (Exception ex)
        {
            DetailText = ex.Message;
            ShowOauthSetup = true;
        }
    }

    [RelayCommand]
    private void OpenOauthAppSettings()
    {
        try
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = "https://github.com/settings/developers",
                UseShellExecute = true,
            });
        }
        catch (Exception ex)
        {
            DetailText = ex.Message;
        }
    }

    [RelayCommand]
    private async Task ConnectAsync()
    {
        if (IsBusy)
        {
            return;
        }

        try
        {
            IsBusy = true;
            DetailText = "";
            if (string.IsNullOrWhiteSpace(GithubOauthConfig.ReadClientId()))
            {
                ShowOauthSetup = true;
                StatusText = Loc.Instance.T("gh.connect_failed");
                DetailText = Loc.Instance.T("gh.missing_client");
                return;
            }

            var start = await Task.Run(() => EnsureAuth().Start(openBrowser: true)).ConfigureAwait(true);
            _deviceCode = start.DeviceCode;
            UserCode = start.UserCode;
            VerificationUri = string.IsNullOrWhiteSpace(start.VerificationUriComplete)
                ? start.VerificationUri
                : start.VerificationUriComplete;
            StatusText = Loc.Instance.T("gh.waiting");
            DetailText = string.Format(Loc.Instance.T("gh.enter_code"), start.UserCode, start.VerificationUri);
            IsPolling = true;
            await PollUntilDoneAsync(start.Interval, start.ExpiresIn).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            StatusText = Loc.Instance.T("gh.connect_failed");
            DetailText = ex.Message;
            IsPolling = false;
        }
        finally
        {
            IsBusy = false;
        }
    }

    [RelayCommand]
    private void CancelPoll()
    {
        _pollCts?.Cancel();
        IsPolling = false;
        StatusText = IsConnected ? StatusText : Loc.Instance.T("gh.cancelled");
    }

    [RelayCommand]
    private async Task DisconnectAsync()
    {
        try
        {
            IsBusy = true;
            _pollCts?.Cancel();
            var status = await Task.Run(() => EnsureAuth().Disconnect()).ConfigureAwait(true);
            ApplyStatus(status);
            UserCode = "";
            DetailText = Loc.Instance.T("gh.disconnected");
        }
        catch (Exception ex)
        {
            DetailText = ex.Message;
        }
        finally
        {
            IsBusy = false;
            IsPolling = false;
        }
    }

    [RelayCommand]
    private async Task SavePatAsync()
    {
        if (string.IsNullOrWhiteSpace(PatToken))
        {
            DetailText = Loc.Instance.T("gh.pat_empty");
            return;
        }

        try
        {
            IsBusy = true;
            var token = PatToken.Trim();
            var status = await Task.Run(() => EnsureAuth().SetPat(token, "read:user")).ConfigureAwait(true);
            PatToken = "";
            ApplyStatus(status);
            DetailText = status.Connected
                ? Loc.Instance.T("gh.pat_stored")
                : Loc.Instance.T("gh.pat_disconnected");
        }
        catch (Exception ex)
        {
            DetailText = ex.Message;
        }
        finally
        {
            IsBusy = false;
        }
    }

    [RelayCommand]
    private void CopyUserCode()
    {
        if (!string.IsNullOrWhiteSpace(UserCode))
        {
            Clipboard.SetText(UserCode);
            DetailText = Loc.Instance.T("gh.code_copied");
        }
    }

    [RelayCommand]
    private void OpenVerificationUri()
    {
        if (string.IsNullOrWhiteSpace(VerificationUri))
        {
            return;
        }

        try
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = VerificationUri,
                UseShellExecute = true,
            });
        }
        catch (Exception ex)
        {
            DetailText = ex.Message;
        }
    }

    private async Task PollUntilDoneAsync(int intervalSeconds, int expiresIn)
    {
        _pollCts?.Cancel();
        _pollCts = new CancellationTokenSource(TimeSpan.FromSeconds(Math.Max(30, expiresIn + 5)));
        var ct = _pollCts.Token;
        var delay = Math.Max(5, intervalSeconds);

        try
        {
            while (!ct.IsCancellationRequested)
            {
                await Task.Delay(TimeSpan.FromSeconds(delay), ct).ConfigureAwait(true);
                var poll = await Task.Run(() => EnsureAuth().Poll(_deviceCode), ct).ConfigureAwait(true);
                if (poll.Interval > 0)
                {
                    delay = Math.Max(5, poll.Interval);
                }

                switch (poll.Status)
                {
                    case "authorized":
                        if (poll.Connection is not null)
                        {
                            ApplyStatus(poll.Connection);
                        }
                        else
                        {
                            RefreshStatus();
                        }

                        DetailText = Loc.Instance.T("gh.connected");
                        IsPolling = false;
                        return;
                    case "authorization_pending":
                        StatusText = Loc.Instance.T("gh.waiting");
                        break;
                    case "slow_down":
                        StatusText = Loc.Instance.T("gh.slow");
                        delay = Math.Max(delay + 5, poll.Interval > 0 ? poll.Interval : delay + 5);
                        break;
                    default:
                        StatusText = Loc.Instance.T("gh.auth_failed");
                        DetailText = string.IsNullOrWhiteSpace(poll.ErrorDescription)
                            ? poll.Error
                            : $"{poll.Error}: {poll.ErrorDescription}";
                        IsPolling = false;
                        return;
                }
            }
        }
        catch (OperationCanceledException)
        {
            if (!IsConnected)
            {
                StatusText = Loc.Instance.T("gh.cancelled_timeout");
            }
        }
        finally
        {
            IsPolling = false;
        }
    }

    private void ApplyStatus(GithubStatus status)
    {
        IsConnected = status.Connected;
        ClientIdConfigured = status.ClientIdConfigured;
        if (status.Connected)
        {
            StatusText = string.IsNullOrWhiteSpace(status.AccountLogin)
                ? Loc.Instance.T("gh.connected_plain")
                : string.Format(Loc.Instance.T("gh.connected_as"), status.AccountLogin);
            DetailText = string.IsNullOrWhiteSpace(status.Scope)
                ? $"secret: {status.SecretId}"
                : $"scope: {status.Scope} · secret: {status.SecretId}";
        }
        else
        {
            StatusText = Loc.Instance.T("gh.not_connected");
            ClientIdConfigured = status.ClientIdConfigured || !string.IsNullOrWhiteSpace(ClientId);
            ShowOauthSetup = !ClientIdConfigured;
            DetailText = ClientIdConfigured
                ? Loc.Instance.T("gh.ready")
                : Loc.Instance.T("gh.need_client");
        }
    }

    public void Dispose()
    {
        Loc.Instance.PropertyChanged -= OnLocChanged;
        _pollCts?.Cancel();
        _pollCts?.Dispose();
        _auth?.Dispose();
    }
}
