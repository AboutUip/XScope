#Requires -Version 5.1
<#
.SYNOPSIS
  Build a self-contained x64 MSI for XScope (SDK Release + publish + WiX).

.EXAMPLE
  .\packaging\windows\build-msi.ps1
  .\packaging\windows\build-msi.ps1 -SkipSdk
#>
[CmdletBinding()]
param(
    [string] $Configuration = "Release",
    [switch] $SkipSdk
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$SdkDir = Join-Path $RepoRoot "sdk"
$ClientProj = Join-Path $RepoRoot "clients\windows\XScope.csproj"
$InstallerProj = Join-Path $PSScriptRoot "XScope.Installer\XScope.Installer.wixproj"
$OutDir = Join-Path $PSScriptRoot "out"
$PublishDir = Join-Path $OutDir "publish"
$SdkBuildDir = Join-Path $SdkDir "out\build\x64-release"

function Get-ProductVersion {
    $xml = [xml](Get-Content -LiteralPath $ClientProj -Raw)
    $ver = $xml.Project.PropertyGroup.Version | Where-Object { $_ } | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($ver)) { return "0.1.0" }
    return $ver.Trim()
}

function Ensure-VsDevEnvironment {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "Visual Studio / MSVC not found (need Desktop C++ for SDK build)."
    }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) {
        throw "No VS install with VC tools found."
    }
    $vsDev = Join-Path $vsPath "Common7\Tools\Launch-VsDevShell.ps1"
    if (-not (Test-Path $vsDev)) {
        throw "Launch-VsDevShell.ps1 missing under $vsPath"
    }
    & $vsDev -Arch amd64 -HostArch amd64 | Out-Null
}

Write-Host "==> Repo: $RepoRoot"
$version = Get-ProductVersion
Write-Host "==> Product version: $version"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
if (Test-Path $PublishDir) {
    Remove-Item -LiteralPath $PublishDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $PublishDir | Out-Null

if (-not $SkipSdk) {
    Write-Host "==> SDK configure/build (x64-release)"
    Ensure-VsDevEnvironment
    Push-Location $SdkDir
    try {
        cmake --preset x64-release
        cmake --build out/build/x64-release --target xscope_capi --config Release
    }
    finally {
        Pop-Location
    }
}
else {
    Write-Host "==> Skipping SDK build (-SkipSdk)"
}

$capi = Join-Path $SdkBuildDir "xscope_capi.dll"
$xaiop = Join-Path $SdkBuildDir "xaiop_native.dll"
if (-not (Test-Path $capi)) {
    throw "Missing $capi — build SDK x64-release first (or omit -SkipSdk)."
}
if (-not (Test-Path $xaiop)) {
    throw "Missing $xaiop — build SDK x64-release first (or omit -SkipSdk)."
}

Write-Host "==> Publish client (self-contained win-x64)"
dotnet publish $ClientProj `
    -c $Configuration `
    -r win-x64 `
    --self-contained true `
    -p:Platform=x64 `
    -p:PublishSingleFile=false `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:XScopeSdkBuildDir=$SdkBuildDir `
    -o $PublishDir

Copy-Item -LiteralPath $capi -Destination (Join-Path $PublishDir "xscope_capi.dll") -Force
Copy-Item -LiteralPath $xaiop -Destination (Join-Path $PublishDir "xaiop_native.dll") -Force

if (-not (Test-Path (Join-Path $PublishDir "XScope.exe"))) {
    throw "Publish did not produce XScope.exe under $PublishDir"
}

Write-Host "==> Build MSI (WiX)"
dotnet build $InstallerProj `
    -c Release `
    -p:PublishDir="$PublishDir\" `
    -p:ProductVersion=$version
if ($LASTEXITCODE -ne 0) {
    throw "WiX MSI build failed (exit $LASTEXITCODE)."
}

$msi = Get-ChildItem -LiteralPath $OutDir -Filter "XScope-*-x64.msi" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $msi) {
    throw "MSI not found under $OutDir"
}

Write-Host ""
Write-Host "Done: $($msi.FullName)"
Write-Host "Size: $([math]::Round($msi.Length / 1MB, 1)) MB"
