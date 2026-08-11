# Windows 客户端

[English](windows.md)

路径：`clients/windows/`  
技术栈：.NET 9 WPF、Material Design 3、MVVM、HelixToolkit（三维图谱）、MdXaml / Markdig  
优先级：**UI 优先**（用户第一感觉）

## 依赖

| 包 | 用途 |
|----|------|
| `MaterialDesignThemes` | Material Design 3 控件与默认样式 |
| `MaterialDesignColors` | 调色板 / 主题色 |
| `CommunityToolkit.Mvvm` | MVVM 辅助 |
| `Microsoft.Xaml.Behaviors.Wpf` | XAML behaviors |
| `MdXaml` | 应用内 Markdown 报告预览 |
| `Markdig` | 报告导出用 Markdown AST |
| `DocumentFormat.OpenXml` | `.docx` 导出 |
| `QuestPDF` | `.pdf` 导出 |
| `HelixToolkit.Wpf` | 三维知识关联图 |

```powershell
cd clients/windows
dotnet restore
dotnet build -c Debug -p:Platform=x64
dotnet run -c Debug -p:Platform=x64
```

客户端每次构建后会从 SDK 输出目录复制原生 DLL（`xscope_capi.dll`、`xaiop_native.dll`）。默认：

`XScopeSdkBuildDir` → `sdk/out/build/x64-debug`  
Release 打包时可覆盖：`-p:XScopeSdkBuildDir=..\..\sdk\out\build\x64-release`

## 主题与窗口铬

- **外观**（设置 → 外观）：自动 / 浅色 / 深色，写入 `data_root/global/ui.json`（`theme`）
- 语义色刷：`XScopeWindowBg`、`XScopeSurface`、`XScopeSurfaceAlt`、`XScopeInputBg`、`XScopeHover`、`XScopeBorder`、文本 / 强调色 — 由 `ThemeService` 应用
- 深色为 X 风格炭灰画布（`#0F1419` 与抬升表面）+ 强调色 `#1D9BF0`
- `WindowThemeChrome` 设置沉浸式深色标题栏，以及 Win11 DWM 边框/标题色，避免系统默认纯黑描边
- 主窗口客户区背景使用 `XScopeSurfaceAlt`，避免圆角控件下透出硬黑角
- 报告 Markdown：深色用 MdXaml **Sasabune**，浅色用 **SasabuneStandard**（表格可读）

## 壳层 UI

- 主壳：左侧固定**项目列表** + 右侧主页/调研；悬浮**设置**；页脚版本号与开发者 `小萱baibai`
- **主页**：品牌字标 + 搜索撰写条（提供商 / 模型 / 精度）
- **调研**：对话流、报告卡片、底部追问条、洞察侧栏
- **设置**（`SettingsWindow`）：左侧导航 — GitHub、搜索、AI、外观、语言、关于
- **语言**：默认英文，支持中文；`ui.json`
- **启动片头**（`SplashWindow`）：无边框；字母排队 **Xuan → X** 再拼 **Scope**
- **应用图标**：`Assets/xscope.ico`（多尺寸）；PNG 源 `Assets/icon.png`（关于页等）

主产品壳不使用 WebView。

## 调研相关 UX（客户端负责）

| 界面 | 行为 |
|------|------|
| 知识关联图 | Helix 三维；按权重缩放节点；细边线；主题感知详情卡 / 标签 |
| 报告预览 | 对话末尾 MdXaml；随主题切换样式 |
| 导出 | 报告完成后：报告**底部「导出」** → 对话框选择 **Markdown / PDF / Word** → 另存为 |
| 导出管线 | 仅客户端：`ReportMarkdown` → Markdig 块模型 → UTF-8 `.md` / OpenXml `.docx` / QuestPDF `.pdf`（无 SDK 导出 API） |

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
dotnet run -c Debug -p:Platform=x64
```

界面：**Connect GitHub**（浏览器 + user code + 轮询）· **PAT 回退** · **Disconnect**。  
默认 `data_root`：`%LocalAppData%\XScope\data`。

## 打包（MSI）

见 [Windows 打包](../packaging/windows.zh-CN.md)。摘要：

```powershell
# 仓库根目录（编译 SDK Release、发布客户端、产出 MSI）
.\packaging\windows\build-msi.ps1
```

产物：`packaging/windows/out/XScope-<version>-x64.msi`

## 客户端职责（相对 SDK）

- 在 Windows 已知文件夹下选择私有 `data_root`
- 承载窗口（含同进程拖出的任务窗口）
- 允许多进程实例，并参与项目 IPC 同步
- 不直连 SQLite
- 经 `xscope_capi` 调用 SDK，勿在 C# 中打开 SQLite
- 负责呈现层：主题、Markdown 预览、报告导出格式

另见：[存储](../architecture/storage.zh-CN.md)、[并发](../architecture/concurrency.zh-CN.md)、[安全](../architecture/security.zh-CN.md)、[OAuth 与 GitHub REST](../architecture/oauth-github.zh-CN.md)、[调研编排](../architecture/research.zh-CN.md)。
