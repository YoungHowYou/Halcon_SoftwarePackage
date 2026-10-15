# =============================================================================
# run-selftest.ps1 -- run the headless model self-test (parm_ui_selftest)
#
#   ASCII-only on purpose (see build-msvc.ps1 for why).
#   Usage: powershell -ExecutionPolicy Bypass -File scripts\run-selftest.ps1
# =============================================================================
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot

# 用相对路径（相对仓库根目录），这样自测里打印出来的也是相对路径
$exe    = "build/msvc/bin/parm_ui_selftest.exe"
$sample = "test/sample_all_types.json"

Push-Location $root
if (-not (Test-Path $exe)) {
    Pop-Location
    throw "parm_ui_selftest.exe not found: $exe  (run scripts\build-msvc.ps1 first)"
}

# the self-test prints UTF-8; make the console agree so Chinese is readable
$OutputEncoding = [Text.Encoding]::UTF8
[Console]::OutputEncoding = [Text.Encoding]::UTF8

& $exe $sample
$code = $LASTEXITCODE
Pop-Location
exit $code
