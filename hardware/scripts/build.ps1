param([switch]$Upload, [string]$Port = 'COM8', [string]$OutputPath = '', [string]$BuildPath = '', [string]$BackupPath = '')
$ErrorActionPreference = 'Stop'
if ($Upload -and -not $BackupPath) { throw 'Upload requires -BackupPath with a checked full 16 MiB device backup.' }
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$cli = Join-Path $taskRoot 'tools/arduino-cli/arduino-cli.exe'
$config = Join-Path $taskRoot 'tools/arduino-cli/config.yaml'
$sketch = Join-Path $taskRoot 'hardware/arduino/yiyi_desk'
$output = if ($OutputPath) { $OutputPath } else { Join-Path $taskRoot 'build/yiyi_desk' }
# esptool is a PyInstaller one-file executable. Under the DSH Windows sandbox its
# extraction into the user's %TEMP% is denied, so keep tool temp files inside the
# workspace where the build user has write access.
$toolTemp = Join-Path $taskRoot 'build/esptool-temp'
New-Item -ItemType Directory -Force -Path $toolTemp | Out-Null
$env:TEMP = $toolTemp
$env:TMP = $toolTemp
$buildArgs = @()
. (Join-Path $PSScriptRoot 'panel_build_flags.ps1')
if ($BuildPath) { $buildArgs += @('--build-path', $BuildPath) }
$fqbn = 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB'
# The sketch-local partitions.csv reserves 8.5 MiB for the desktop and 4 MiB for GrokBot app1.
# Core 2.0.17 is intentional: backlight uses its LEDC API.
# arduino-cli forwards Python warnings (e.g. gen_esp32part's UnicodeWarning) on
# stderr. Under $ErrorActionPreference='Stop' PowerShell treats those as
# terminating errors and aborts the script even when the build succeeded, so the
# external call runs with Continue and its exit code is checked explicitly.
$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $cli compile --fqbn $fqbn --config-file $config --libraries (Join-Path $taskRoot 'tools/panel-libraries') --build-property 'upload.maximum_size=8912896' --build-property 'compiler.c.extra_flags=-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -I{build.source.path}' --build-property "compiler.cpp.extra_flags=-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -I{build.source.path} $panelBuildFlags" @buildArgs --output-dir $output $sketch 2>&1 | ForEach-Object { Write-Host $_ }
$compileExit = $LASTEXITCODE
$ErrorActionPreference = $previousErrorAction
if ($compileExit -ne 0) { throw 'Firmware compilation failed' }
if ($Upload) {
    & python (Join-Path $PSScriptRoot 'install_grokbot.py') --desktop-only --flash --port $Port --backup $BackupPath --desktop $output
    if ($LASTEXITCODE -ne 0) { throw 'Firmware upload failed' }
}
