param([string]$Port = 'COM8', [switch]$SkipBuild, [string]$DesktopPath = '')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
# 注意：本脚本对应“官方 2.5.0 双应用旧布局”（app0 9.5MiB / app1 0x990000 / assets 0xc90000）。
# 2026-10-28 起设备已换成音乐固件新布局（app0 7.5MiB / music app1 0x790000 / model 0xc90000），
# 直接运行本脚本会写错偏移。请使用：
#   - hardware/scripts/flash_music_app.ps1     安装/更新音乐固件
#   - hardware/scripts/restore_official_dual.ps1 回滚官方 2.5.0
$projectPartitions = Join-Path $taskRoot 'hardware/arduino/yiyi_desk/partitions.csv'
if ((Get-Content -LiteralPath $projectPartitions -Raw) -notmatch '(?im)^app0\s*,\s*app\s*,\s*ota_0\s*,\s*0x10000\s*,\s*0x980000\s*,') {
    throw 'Legacy official-layout script requires app0 9.5 MiB. Current GrokBot updates use install_grokbot.py.'
}
$idfPython = Join-Path $env:USERPROFILE '.espressif/python_env/idf6.0_py3.13_env/Scripts/python.exe'
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -BuildPath "$env:USERPROFILE/.cache/yiyi-xiaozhi/desktop-build"
    & (Join-Path $PSScriptRoot 'build_official_xiaozhi.ps1')
}
$desktopOutput = if ($DesktopPath) { $DesktopPath } else { Join-Path $taskRoot 'build/yiyi_desk' }
$desktop = Join-Path $desktopOutput 'yiyi_desk.ino.bin'
$table = Join-Path $desktopOutput 'yiyi_desk.ino.partitions.bin'
$official = Join-Path $taskRoot 'build/official-xiaozhi/xiaozhi.bin'
$officialTable = Join-Path $taskRoot 'build/official-xiaozhi/partition-table.bin'
$assets = Join-Path $taskRoot 'build/official-xiaozhi/generated_assets.bin'
foreach ($item in @(@($desktop,0x980000),@($official,0x300000),@($assets,0x280000))) {
    $file = Get-Item -LiteralPath $item[0]
    if ($file.Length -le 0 -or $file.Length -gt $item[1]) { throw "Invalid partition image size: $($file.Name)" }
}
if ((Get-FileHash -LiteralPath $table).Hash -ne (Get-FileHash -LiteralPath $officialTable).Hash) {
    throw 'Desktop and official partition tables differ'
}
$backup = Join-Path $taskRoot ('build/backups/dual-app-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
& $idfPython -m esptool --chip esp32s3 --port $Port --before usb_reset --no-stub read_flash --flash_size 16MB 0 0x10000 (Join-Path $backup 'boot-nvs-ota.bin')
if ($LASTEXITCODE -ne 0) { throw 'Boot/NVS/OTA backup failed' }
& $idfPython -m esptool --chip esp32s3 --port $Port --before usb_reset --no-stub read_flash --flash_size 16MB 0xf10000 0xf0000 (Join-Path $backup 'littlefs.bin')
if ($LASTEXITCODE -ne 0) { throw 'LittleFS backup failed' }
foreach ($saved in @('boot-nvs-ota.bin', 'littlefs.bin')) {
    $file = Join-Path $backup $saved
    (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash | Set-Content -LiteralPath ($file + '.sha256')
}
# Blank OTA selection starts the first valid app (the desktop).
$selection = Join-Path $taskRoot 'build/official-xiaozhi/desktop-otadata.bin'
$bytes = [byte[]]::new(8192)
for ($index = 0; $index -lt $bytes.Length; $index++) { $bytes[$index] = 255 }
[IO.File]::WriteAllBytes($selection, $bytes)
# Keep the already verified Arduino bootloader; preserve NVS and LittleFS.
& $idfPython -m esptool --chip esp32s3 --port $Port --before usb_reset --no-stub write_flash --flash_size 16MB 0x8000 $table 0xe000 $selection 0x10000 $desktop 0x990000 $official 0xc90000 $assets
if ($LASTEXITCODE -ne 0) { throw 'Dual application flash failed' }
& $idfPython -m esptool --chip esp32s3 --port $Port --before usb_reset --no-stub verify_flash 0x8000 $table 0x10000 $desktop 0x990000 $official 0xc90000 $assets
if ($LASTEXITCODE -ne 0) { throw 'Dual application Flash verification failed' }
Write-Output "Desktop + official Xiaozhi flashed; settings backup: $backup"
