# =============================================================================
# smoke.ps1 -- launch the real GUI briefly and dump a screenshot
#
#   Opens the parameter window on the desktop, captures frame 3 as a BMP and
#   closes it again (as "cancel").  Used to verify rendering/fonts in CI-less
#   environments.  ASCII-only on purpose (see build-msvc.ps1).
#
#   Usage: powershell -ExecutionPolicy Bypass -File scripts\smoke.ps1
#          powershell -ExecutionPolicy Bypass -File scripts\smoke.ps1 -Competence 1
#          powershell -ExecutionPolicy Bypass -File scripts\smoke.ps1 -ClickSaveFrame 20 -Png
# =============================================================================
param(
    [switch]$SaveOnly,
    [int]$Milliseconds = 2500,
    [int]$Competence = 3,
    # frame at which the "保存并关闭" button is simulated (0 = never).
    # Useful to capture the validation-error dialog.
    [int]$ClickSaveFrame = 0,
    # also convert the BMP screenshots to PNG (needs python + opencv)
    [switch]$Png,
    # input config file (default: test/sample_all_types.json)
    [string]$InputFile = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot

# 一律用相对路径（相对仓库根目录），打印出来的也只有相对路径
$exe    = "build/msvc/bin/parm_ui_demo.exe"
$sample = "test/sample_all_types.json"
if ($InputFile -ne "") { $sample = $InputFile }
$out    = "build/smoke_out.json"
$shot   = "build/smoke.bmp"
$shot2  = "build/smoke2.bmp"

Push-Location $root
if (-not (Test-Path $exe)) {
    Pop-Location
    throw "parm_ui_demo.exe not found: $exe  (run scripts\build-msvc.ps1 first)"
}

$env:PARM_UI_SMOKE_MS   = "$Milliseconds"
$env:PARM_UI_SMOKE_SHOT = $shot
if ($ClickSaveFrame -gt 0) {
    $env:PARM_UI_SMOKE_CLICK_SAVE = "$ClickSaveFrame"
    $env:PARM_UI_SMOKE_SHOT2      = $shot2
} else {
    Remove-Item Env:\PARM_UI_SMOKE_CLICK_SAVE -ErrorAction SilentlyContinue
    Remove-Item Env:\PARM_UI_SMOKE_SHOT2 -ErrorAction SilentlyContinue
}
if ($SaveOnly) { $env:PARM_UI_SMOKE_SAVE = "1" } else { Remove-Item Env:\PARM_UI_SMOKE_SAVE -ErrorAction SilentlyContinue }

$OutputEncoding = [Text.Encoding]::UTF8
[Console]::OutputEncoding = [Text.Encoding]::UTF8

# 先清掉上一次的截图/输出，避免看到旧文件误以为没跑
Remove-Item $shot, $shot2, $out -ErrorAction SilentlyContinue

& $exe $sample $out $Competence

if ($Png) {
    $conv = "scripts/bmp2png.py"
    if ((Test-Path $conv) -and (Get-Command python -ErrorAction SilentlyContinue)) {
        & python $conv $shot ($shot -replace '\.bmp$', '.png')
        if (Test-Path $shot2) { & python $conv $shot2 ($shot2 -replace '\.bmp$', '.png') }
    }
}

Write-Host "[smoke] input: $sample"
Write-Host "[smoke] screenshot: $shot"
if ($ClickSaveFrame -gt 0) { Write-Host "[smoke] screenshot2: $shot2" }
Write-Host "[smoke] result json: $out"
$code = $LASTEXITCODE
Pop-Location
exit $code
