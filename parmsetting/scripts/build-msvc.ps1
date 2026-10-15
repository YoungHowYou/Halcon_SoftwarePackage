# =============================================================================
# build-msvc.ps1 -- Windows (MSVC) one-shot build for parm_ui
#
#   NOTE: this script is deliberately ASCII-only. Windows PowerShell 5.1 reads
#   .ps1 files using the ANSI code page unless the file starts with a UTF-8 BOM,
#   so non-ASCII text here would be mangled and could even break the parser.
#   (文档与 C++ 注释仍是中文，只有这个 .ps1 必须是 ASCII。)
#
#   1) locate a Visual Studio installation with the MSVC toolchain via vswhere
#      (VS 2019 / VS 2022 both work)
#   2) enter the x64 developer environment (what vcvars64.bat does)
#   3) configure + build with CMake + the Ninja bundled with Visual Studio
#
#   Usage:
#     powershell -ExecutionPolicy Bypass -File scripts\build-msvc.ps1
#     powershell -ExecutionPolicy Bypass -File scripts\build-msvc.ps1 -Config Debug
#     powershell -ExecutionPolicy Bypass -File scripts\build-msvc.ps1 -Target parm_ui_selftest
# =============================================================================
param(
    [string]$BuildDir = "build/msvc",
    [string]$Config   = "Release",
    [string]$Target   = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot

# --------------------------------------------------------- 1) locate Visual Studio
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found; cannot locate Visual Studio (MSVC)" }

$vs = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vs) { throw "No Visual Studio installation with the MSVC toolchain found" }
Write-Host "[build] Visual Studio: $vs"

# ------------------------------------------------- 2) enter the x64 dev environment
Import-Module (Join-Path $vs "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation `
    -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null

# --------------------------------------------------- 3) Ninja (bundled one first)
$ninjaDir = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
if (Test-Path (Join-Path $ninjaDir "ninja.exe")) {
    $env:Path = "$ninjaDir;$env:Path"
} elseif (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    throw "ninja not found; try: python -m pip install ninja"
}

# ------------------------------------------------------------------ 4) build
Push-Location $root
try {
    # NOTE: -D... arguments MUST stay quoted, otherwise PowerShell passes the
    # literal text "$Config" through to CMake.
    cmake -S . -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config"
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

    if ($Target -ne "") {
        cmake --build $BuildDir --target $Target
    } else {
        cmake --build $BuildDir
    }
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }
} finally {
    Pop-Location
}

Write-Host "[build] done. artifacts: $BuildDir/bin"
