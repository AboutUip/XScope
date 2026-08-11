# Windows client

[中文](windows.zh-CN.md)

Path: `clients/windows/`  
Stack: .NET 9 WPF, Material Design 3, MVVM, HelixToolkit (3D graph), MdXaml / Markdig  
Priority: **UI first** (user’s first impression)

## Dependencies

| Package | Purpose |
|---------|---------|
| `MaterialDesignThemes` | Material Design 3 controls & defaults |
| `MaterialDesignColors` | Palette / theme colors |
| `CommunityToolkit.Mvvm` | MVVM helpers |
| `Microsoft.Xaml.Behaviors.Wpf` | XAML behaviors |
| `MdXaml` | In-app Markdown report preview |
| `Markdig` | Markdown AST for report export |
| `DocumentFormat.OpenXml` | `.docx` export |
| `QuestPDF` | `.pdf` export |
| `HelixToolkit.Wpf` | 3D knowledge association graph |

```powershell
cd clients/windows
dotnet restore
dotnet build -c Debug -p:Platform=x64
dotnet run -c Debug -p:Platform=x64
```

Native DLLs (`xscope_capi.dll`, `xaiop_native.dll`) are copied from the SDK build dir after each client build. Default:

`XScopeSdkBuildDir` → `sdk/out/build/x64-debug`  
Override for Release packaging: `-p:XScopeSdkBuildDir=..\..\sdk\out\build\x64-release`

## Theme & chrome

- **Appearance** (Settings → Appearance): Auto / Light / Dark, persisted in `data_root/global/ui.json` (`theme`)
- Semantic brushes: `XScopeWindowBg`, `XScopeSurface`, `XScopeSurfaceAlt`, `XScopeInputBg`, `XScopeHover`, `XScopeBorder`, text / accent tokens — applied by `ThemeService`
- Dark mode follows an X-style charcoal canvas (`#0F1419` / elevated surfaces) with accent `#1D9BF0`
- `WindowThemeChrome` sets immersive dark title bars and Win11 DWM border/caption colors so the OS frame matches the app (avoids pure-black window outlines)
- Main window client background uses `XScopeSurfaceAlt` (not a near-black void) so rounded controls do not show hard black corners
- Report Markdown: MdXaml **Sasabune** (dark) / **SasabuneStandard** (light) for readable tables

## Shell UI

- Main shell: fixed **project list** (left) + research / home (right); floating **Settings**; footer version + developer `小萱baibai`
- **Home**: brand wordmark + search compose (provider / model / precision)
- **Research**: conversation stream, report card, floating follow-up compose, insights rail
- **Settings** (`SettingsWindow`): left nav — GitHub, Search, AI, Appearance, Language, About
- **Language**: default English, 中文; `ui.json`
- **Startup splash** (`SplashWindow`): borderless; letter queue **Xuan → X** then **Scope**
- **App icon**: `Assets/xscope.ico` (multi-size); PNG source `Assets/icon.png` (About page + branding)

No WebView for the product shell.

## Research UX (client-owned)

| Surface | Behavior |
|---------|----------|
| Knowledge graph | Helix 3D view; weight-sized nodes; thinner edges; theme-aware detail card / labels |
| Report preview | MdXaml at end of chat; theme-aware styles |
| Export | After report completes: **Export** under the report → dialog picks **Markdown / PDF / Word** → Save As |
| Export pipeline | Client-only: `ReportMarkdown` → Markdig blocks → UTF-8 `.md` / OpenXml `.docx` / QuestPDF `.pdf` (no SDK export API) |

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
dotnet run -c Debug -p:Platform=x64
```

UI actions: **Connect GitHub** (browser + user code + poll) · **PAT fallback** · **Disconnect**.  
`data_root` defaults to `%LocalAppData%\XScope\data`.

## Packaging (MSI)

See [Windows packaging](../packaging/windows.md). Summary:

```powershell
# From repo root (builds SDK Release, publishes client, produces MSI)
.\packaging\windows\build-msi.ps1
```

Output: `packaging/windows/out/XScope-<version>-x64.msi`

## Client duties (vs SDK)

- Choose a private `data_root` under Windows known folders
- Host windows (including detached task windows in-process)
- Allow multiple process instances; participate in project IPC sync
- Never talk to SQLite directly
- Call SDK via `xscope_capi` (thin C ABI) — do not open SQLite from C#
- Own presentation concerns: theming, Markdown preview, report export formats

See also: [Storage](../architecture/storage.md), [Concurrency](../architecture/concurrency.md), [Security](../architecture/security.md), [OAuth & GitHub REST](../architecture/oauth-github.md), [Research](../architecture/research.md).
