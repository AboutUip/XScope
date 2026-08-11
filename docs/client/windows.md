# Windows client

[中文](windows.zh-CN.md)

Path: `clients/windows/`  
Stack: .NET 9 WPF, Material Design 3, MVVM  
Priority: **UI first** (user’s first impression)

## Dependencies

| Package | Purpose |
|---------|---------|
| `MaterialDesignThemes` | Material Design 3 controls & defaults |
| `MaterialDesignColors` | Palette / theme colors |
| `CommunityToolkit.Mvvm` | MVVM helpers |
| `Microsoft.Xaml.Behaviors.Wpf` | XAML behaviors |

```powershell
cd clients/windows
dotnet restore
dotnet build
dotnet run
```

## Theme baseline

- Light + Blue primary + Teal secondary
- `MaterialDesign3.Defaults.xaml`
- Main shell: fixed **project list** (left) + Google-like **search** (right); floating **Settings**; footer version + developer `小萱baibai`
- **Settings** (`SettingsWindow`): Google-style left nav; GitHub OAuth/PAT; **Language** (default English, 中文); persisted in `data_root/global/ui.json`
- **Startup splash** (`SplashWindow`): borderless, non-draggable; letter queue **Xuan → X** then **Scope**
- App icon: `clients/windows/Assets/xscope.ico`

No WebView for the product shell.

## GitHub login test

Uses P/Invoke against `xscope_capi.dll` (C ABI over `Workspace` / `GithubOAuth`).

1. Build the SDK so natives exist:

```powershell
cd sdk
cmake --preset x64-debug
cmake --build out/build/x64-debug
```

2. Configure OAuth client id (Device Flow enabled on the GitHub OAuth App):

- Env: `XSCOPE_GITHUB_OAUTH_CLIENT_ID`
- Or `%LocalAppData%\XScope\data\global\github_oauth.json`

3. Run the client:

```powershell
cd clients/windows
dotnet run
```

UI actions: **Connect GitHub** (browser + user code + poll) · **PAT fallback** · **Disconnect**.  
`data_root` defaults to `%LocalAppData%\XScope\data`.

## Client duties (vs SDK)

- Choose a private `data_root` under Windows known folders
- Host windows (including detached task windows in-process)
- Allow multiple process instances; participate in project IPC sync
- Never talk to SQLite directly
- Call SDK via `xscope_capi` (thin C ABI) — do not open SQLite from C#

See also: [Storage](../architecture/storage.md), [Concurrency](../architecture/concurrency.md), [Security](../architecture/security.md), [OAuth & GitHub REST](../architecture/oauth-github.md).
