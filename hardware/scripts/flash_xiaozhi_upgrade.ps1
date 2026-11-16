param([string]$Port = 'COM8', [string]$DesktopPath = '')
$ErrorActionPreference = 'Stop'
# Back up current settings and LittleFS, validate both partition tables, and
# migrate app1 together with the desktop partition.
& (Join-Path $PSScriptRoot 'flash_dual_app.ps1') -SkipBuild -Port $Port -DesktopPath $DesktopPath
if ($LASTEXITCODE -ne 0) { throw 'Dual application upgrade failed' }
