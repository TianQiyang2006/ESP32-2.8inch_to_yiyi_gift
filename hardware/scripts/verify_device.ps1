# Device golden gate for yiyi-desk (V2 Core Phase 1).
#
# Wraps hardware/scripts/check_device_golden.py. It NEVER modifies verify.ps1
# (the host-only gate) and it never flashes the board. Serial is only opened in
# -Baseline / -Check mode; the default dry-run only prints the view manifest.
#
#   .\hardware\scripts\verify_device.ps1                                   # dry-run: list views (no serial)
#   .\hardware\scripts\verify_device.ps1 -Baseline -Repeat 2               # capture device baseline + reference frames
#   .\hardware\scripts\verify_device.ps1 -Check                            # compare device against baseline (exit 1 on FAIL/UNSTABLE)
#   .\hardware\scripts\verify_device.ps1 -Check -Only page-0-home,app-settings
#   .\hardware\scripts\verify_device.ps1 -UpdateOnly wifi-keyboard,wifi-keyboard-upper,wifi-symbols
#                                                                          # approved visual change: replace only these
#                                                                          # baseline entries, keep all others, write
#                                                                          # old/new hash + diff evidence into baseline.updates
#   .\hardware\scripts\verify_device.ps1 -FromDir build/review/device-allb -Check   # offline self-test, no device
#   .\hardware\scripts\verify_device.ps1 -FromDir build/review/device-allb -Baseline `
#       -BaselineJson build/audit-20261028/offline-baseline.json                    # offline baseline for self-test
#
# What -Baseline writes:
#   hardware/tests/golden/device-hashes.json      (reviewable hash + environment envelope per view)
#   build/device-golden/baseline/<view>.png       (local reference frames, build/ is git-ignored)
#
# What -Check writes on failure:
#   build/device-golden/last/<view>.png           (latest device frame)
#   build/device-golden/last/<view>-diff.png      (left: reference / right: current, red bbox of changed pixels)
#   build/device-golden/last/report.json          (per-view PASS/FAIL/UNSTABLE/EXPECTED-CHANGE/INFO-DIFF/SKIP + reasons)
#
# What -UpdateOnly writes:
#   hardware/tests/golden/device-hashes.json      (only the selected entries changed; baseline.updates records
#                                                 previousSha256 -> sha256 + diff bbox/PNG per changed view)
#   build/device-golden/last/<view>.png / -diff.png / update-report.json   (evidence for review)
#
# Notes:
#   * K2 keyboard views (wifi-keyboard / wifi-symbols, and wifi-keyboard-upper) are
#     marked expectNoChange=false: after the approved K2 keyboard redesign they are
#     NOT a FAIL, but a human must review the new frames and refresh those two/three
#     baselines with:  -UpdateOnly wifi-keyboard,wifi-keyboard-upper,wifi-symbols
#   * Baseline capture should run on the baseline firmware BEFORE any behaviour
#     change (plan Phase 1 gate); -Check then runs on the new firmware.
#   * Preferred environment: device NOT connected to Wi-Fi (offline fixture mode:
#     no NTP/weather/reading refresh), otherwise network data can change pixels.
#   * settle: the tool tolerates a permanent animations=1 (LVGL keyboard caret blink)
#     and verifies frame-hash stability instead; keyboard views mask the input rows.
#
# Exit codes: 0 = PASS / dry-run OK; 1 = FAIL or UNSTABLE; 2 = configuration error.
param(
    [switch]$Baseline,
    [switch]$Check,
    [string]$UpdateOnly = '',
    [ValidateRange(1, 5)][int]$Repeat = 1,
    [string]$FromDir = '',
    [string]$Port = 'COM8',
    [string]$BaselineJson = '',
    [string]$Only = '',
    [double]$EnvWait = 0,
    [string]$Python = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$tool = Join-Path $root 'hardware/scripts/check_device_golden.py'

if (-not (Test-Path -LiteralPath $tool)) {
    Write-Host "verify_device.ps1: device golden tool not found: $tool" -ForegroundColor Red
    exit 2
}
if ($Baseline -and $Check) {
    Write-Host 'verify_device.ps1: -Baseline and -Check are mutually exclusive' -ForegroundColor Red
    exit 2
}
if ($UpdateOnly -and ($Baseline -or $Check)) {
    Write-Host 'verify_device.ps1: -UpdateOnly cannot be combined with -Baseline/-Check' -ForegroundColor Red
    exit 2
}
if ($UpdateOnly -and $FromDir) {
    Write-Host 'verify_device.ps1: -UpdateOnly is device-only and cannot be combined with -FromDir' -ForegroundColor Red
    exit 2
}
if ($UpdateOnly -and $Only) {
    Write-Host 'verify_device.ps1: use either -UpdateOnly <ids> or -Only <ids>, not both' -ForegroundColor Red
    exit 2
}
if (-not $Python) {
    $Python = 'D:\python3.13\python.exe'
    if (-not (Test-Path -LiteralPath $Python)) { $Python = 'python' }
}

function Resolve-RepoPath([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return '' }
    if ([System.IO.Path]::IsPathRooted($Value)) { return $Value }
    return (Join-Path $root $Value)
}

$toolArgs = @($tool)
if ($Baseline) {
    $toolArgs += '--baseline'
    if ($Repeat -gt 1) { $toolArgs += @('--repeat', "$Repeat") }
}
elseif ($Check) {
    $toolArgs += '--check'
    if ($EnvWait -gt 0) { $toolArgs += @('--env-wait', "$EnvWait") }
}
elseif ($UpdateOnly) {
    $toolArgs += @('--update-only', $UpdateOnly)
    if ($Repeat -gt 1) { $toolArgs += @('--repeat', "$Repeat") }
}
if ($Only) { $toolArgs += @('--only', $Only) }
if ($FromDir) {
    $toolArgs += @('--from-dir', (Resolve-RepoPath $FromDir))
}
elseif ($Baseline -or $Check -or $UpdateOnly) {
    $toolArgs += @('--port', $Port)
}
if ($BaselineJson) {
    $toolArgs += @('--baseline-json', (Resolve-RepoPath $BaselineJson))
}

Write-Host ("verify_device.ps1: " + $Python + " " + ($toolArgs -join ' '))
& $Python @toolArgs
$code = $LASTEXITCODE

if ($code -ne 0) {
    if ($Baseline) {
        Write-Host "verify_device.ps1: BASELINE FAILED (exit $code) - review build/device-golden/last/" -ForegroundColor Red
    }
    elseif ($Check) {
        Write-Host "verify_device.ps1: CHECK FAILED (exit $code) - review build/device-golden/last/ (frame + bbox diff + report.json)" -ForegroundColor Red
    }
    elseif ($UpdateOnly) {
        Write-Host "verify_device.ps1: UPDATE-ONLY FAILED (exit $code) - baseline NOT changed; review build/device-golden/last/update-report.json" -ForegroundColor Red
    }
    else {
        Write-Host "verify_device.ps1: FAILED (exit $code)" -ForegroundColor Red
    }
    exit $code
}

Write-Host 'verify_device.ps1: PASS'
exit 0
