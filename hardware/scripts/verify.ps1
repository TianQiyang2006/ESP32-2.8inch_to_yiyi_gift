# One-command verification gate for this firmware.
#
#   .\hardware\scripts\verify.ps1                 # build + host render + golden + regression + flash report
#   .\hardware\scripts\verify.ps1 -SkipBuild      # reuse an existing build
#   .\hardware\scripts\verify.ps1 -UpdateGolden   # accept reviewed visual changes (writes the golden hashes)
#
# The golden file (hardware/tests/golden/render-hashes.json) stores one SHA-256
# per host-rendered frame, so an unintended layout visual change fails the gate.
param(
    [switch]$SkipBuild,
    [switch]$UpdateGolden,
    [string]$Out = 'build/host-pages/verify'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$python = 'D:\python3.13\python.exe'
if (-not (Test-Path $python)) { $python = 'python' }

if (-not $SkipBuild) {
    Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
    & (Join-Path $root 'build/impl/build.ps1')
}

# Host offscreen renderer (private object cache; seeded once from the shared cache).
$obj = Join-Path $root 'build/host-lvgl-verify'
$seed = Join-Path $root 'build/host-lvgl'
if (-not (Test-Path $obj) -and (Test-Path $seed)) { Copy-Item $seed $obj -Recurse -Force }
$out = Join-Path $root $Out
& $python (Join-Path $root 'hardware/scripts/test_native_lvgl.py') --pages --obj $obj --out $out
if ($LASTEXITCODE -ne 0) { throw 'host render failed' }

$goldenArgs = @((Join-Path $root 'hardware/scripts/check_render_golden.py'), $out)
if ($UpdateGolden) { $goldenArgs += '--update' }
& $python @goldenArgs
if ($LASTEXITCODE -ne 0) { throw 'render golden check failed (review frames, then -UpdateGolden if intentional)' }

& $python (Join-Path $root 'hardware/scripts/run_host_regression.py')
if ($LASTEXITCODE -ne 0) { throw 'host regression failed' }

& $python (Join-Path $root 'hardware/scripts/font_flash_report.py')
Write-Host 'verify.ps1: PASS'
