# Windows 客户端

[English](windows.md)

路径：`clients/windows/`  
技术栈：.NET 9 WPF、Material Design 3、MVVM  
优先级：**UI 优先**（用户第一感觉）

## 依赖

| 包 | 用途 |
|----|------|
| `MaterialDesignThemes` | Material Design 3 控件与默认样式 |
| `MaterialDesignColors` | 调色板 / 主题色 |
| `CommunityToolkit.Mvvm` | MVVM 辅助 |
| `Microsoft.Xaml.Behaviors.Wpf` | XAML behaviors |

```powershell
cd clients/windows
dotnet restore
dotnet build
dotnet run
```

## 主题基线

- Light + Blue 主色 + Teal 辅色
- `MaterialDesign3.Defaults.xaml`
- 主壳：左侧固定**项目列表** + 右侧 Google 式**搜索栏**；右上悬浮**设置**；右下版本号与开发者 `小萱baibai`
- **设置**（`SettingsWindow`）：Google 式左侧导航；GitHub OAuth/PAT；**语言**（默认英文，支持中文）；持久化于 `data_root/global/ui.json`
- **启动片头**（`SplashWindow`）：无边框、不可拖动；字母排队 **Xuan → X** 再拼 **Scope**
- 应用图标：`clients/windows/Assets/xscope.ico`

主产品壳不使用 WebView。

## GitHub 登录测试

通过 P/Invoke 调用 `xscope_capi.dll`（`Workspace` / `GithubOAuth` 的 C ABI）。

1. 先编译 SDK：

```powershell
cd sdk
cmake --preset x64-debug
cmake --build out/build/x64-debug
```

2. 配置 OAuth client id（GitHub OAuth App 需启用 Device Flow）：

- 环境变量：`XSCOPE_GITHUB_OAUTH_CLIENT_ID`
- 或 `%LocalAppData%\XScope\data\global\github_oauth.json`

3. 运行客户端：

```powershell
cd clients/windows
dotnet run
```

界面：**Connect GitHub**（浏览器 + user code + 轮询）· **PAT 回退** · **Disconnect**。  
默认 `data_root`：`%LocalAppData%\XScope\data`。

## 客户端职责（相对 SDK）

- 在 Windows 已知文件夹下选择私有 `data_root`
- 承载窗口（含同进程拖出的任务窗口）
- 允许多进程实例，并参与项目 IPC 同步
- 不直连 SQLite
- 经 `xscope_capi` 调用 SDK，勿在 C# 中打开 SQLite

另见：[存储](../architecture/storage.zh-CN.md)、[并发](../architecture/concurrency.zh-CN.md)、[安全](../architecture/security.zh-CN.md)、[OAuth 与 GitHub REST](../architecture/oauth-github.zh-CN.md)。
