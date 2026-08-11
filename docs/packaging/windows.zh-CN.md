# Windows MSI 打包

[English](windows.md)

路径：`packaging/windows/`  
产出 **64 位 MSI**，内含 **自包含** XScope（最终用户无需单独安装 .NET 运行时）。

## 环境要求

- Windows x64
- [.NET 9 SDK](https://dotnet.microsoft.com/download)
- Visual Studio C++ / MSVC + CMake + Ninja + Go（与 SDK 构建相同）
- WiX Toolset **5**（若缺失，构建脚本会以本地 `dotnet` 工具自动安装）

## 构建

在**仓库根目录**执行：

```powershell
.\packaging\windows\build-msi.ps1
```

可选参数：

```powershell
.\packaging\windows\build-msi.ps1 -SkipSdk          # 复用已有 x64-release 原生库
.\packaging\windows\build-msi.ps1 -Configuration Release
```

### 脚本步骤

1. 配置并编译 SDK 预设 **`x64-release`** → `xscope_capi.dll` / `xaiop_native.dll`
2. 将 WPF 客户端 `dotnet publish` 为 **self-contained** `win-x64` Release
3. 把原生 DLL 复制到发布目录的 `XScope.exe` 旁
4. 构建 `XScope.Installer`（WiX）→ MSI 输出到 `packaging/windows/out/`

默认文件名：`XScope-0.1.0-x64.msi`（版本取自 `clients/windows/XScope.csproj`）。

## 安装行为

| 项 | 值 |
|----|------|
| 界面 | WiX **WixUI_InstallDir** 向导（欢迎 → **MIT 许可协议** → 目录 → 确认 → 进度 → 完成） |
| 协议 | `License.rtf`（MIT）；须点「我接受」才能继续；安装目录另附 `LICENSE.txt` |
| 安装目录 | `%ProgramFiles%\XScope`（可改） |
| 开始菜单 | 快捷方式：开始 → XScope |
| 桌面 | 桌面快捷方式（卸载时移除） |
| 启动 | 完成页勾选 **Launch XScope**（默认勾选） |
| 架构 | x64 |
| 升级 | 固定 `UpgradeCode`；新版本替换旧安装 |
| 用户数据 | 仍在 `%LocalAppData%\XScope\data`（卸载默认不删） |
| 应用信息 | 「应用和功能」中显示 MIT 版权与许可链接 |

**刻意不做：** 写入 PATH、开机自启、文件关联、自定义 URL 协议。

## 目录结构

```text
packaging/windows/
  build-msi.ps1
  XScope.Installer/
    XScope.Installer.wixproj
    Package.wxs
    License.rtf          # 向导中展示的 MIT 协议
  out/                 # MSI 与发布暂存（gitignore）
```

## 说明

- MSI 务必搭配 **Release** SDK 原生库。
- 首次运行仍需在设置中配置 AI / GitHub / 搜索密钥（密钥不打进安装包）。
- MSI 代码签名为可选项；脚本未包含（可在 CI 中加 `signtool`）。
