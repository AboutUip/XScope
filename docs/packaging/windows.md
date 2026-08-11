# Windows MSI packaging

[中文](windows.zh-CN.md)

Path: `packaging/windows/`  
Produces a **64-bit MSI** that installs a **self-contained** XScope build (end users do not need a separate .NET runtime).

## Prerequisites

- Windows x64
- [.NET 9 SDK](https://dotnet.microsoft.com/download)
- Visual Studio C++ / MSVC + CMake + Ninja + Go (same as SDK build)
- WiX Toolset **5** (installed automatically by the build script as a local `dotnet` tool if missing)

## Build

From the **repository root**:

```powershell
.\packaging\windows\build-msi.ps1
```

Optional:

```powershell
.\packaging\windows\build-msi.ps1 -SkipSdk          # reuse existing x64-release natives
.\packaging\windows\build-msi.ps1 -Configuration Release
```

### What the script does

1. Configure/build SDK preset **`x64-release`** → `xscope_capi.dll` / `xaiop_native.dll`
2. `dotnet publish` the WPF client as **self-contained** `win-x64` Release
3. Copy natives next to the published `XScope.exe`
4. Build `XScope.Installer` (WiX) → MSI under `packaging/windows/out/`

Default output name: `XScope-0.1.0-x64.msi` (version from `clients/windows/XScope.csproj`).

## Installer behavior

| Item | Value |
|------|--------|
| UI | WiX **WixUI_InstallDir** wizard (welcome → **MIT EULA** → folder → confirm → progress → finish) |
| License | `License.rtf` (MIT); Accept required to continue; `LICENSE.txt` also installed with the app |
| Install folder | `%ProgramFiles%\XScope` (user-selectable) |
| Start Menu | Shortcut under Start → XScope |
| Desktop | Shortcut on the desktop (removed on uninstall) |
| Launch | Finish page checkbox **Launch XScope** (on by default) |
| Architecture | x64 |
| Upgrade | Same `UpgradeCode`; newer versions replace older installs |
| Data | Still under `%LocalAppData%\XScope\data` (not removed on uninstall) |
| ARP | Shows MIT copyright / license link in Apps & Features |

**Intentionally not included:** add to PATH, Run at logon, file associations, browser protocol handlers.

## Layout

```text
packaging/windows/
  build-msi.ps1
  XScope.Installer/
    XScope.Installer.wixproj
    Package.wxs
    License.rtf          # MIT EULA shown in the wizard
  out/                 # MSI + publish staging (gitignored)
```

## Notes

- Always ship the **Release** SDK natives with Release MSI builds.
- First-run still requires AI / GitHub / search keys in Settings (secrets are not bundled).
- Code-signing the MSI is optional and out of scope for the script (add `signtool` in CI if needed).
