param(
    [string]$Port = 'COM8',
    [string]$DesktopPath = ''
)
$ErrorActionPreference = 'Stop'
# 回滚：把设备恢复成“官方 2.5.0 双应用旧布局”（app0 9.5MiB / app1 3MiB / assets 2.5MiB / spiffs 0.96MiB）。
# 仅在需要放弃音乐固件时使用；会覆盖分区表、otadata、app0、app1 和 assets。
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$py = Join-Path $env:USERPROFILE '.espressif/python_env/idf5.4_py3.13_env/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $py)) { throw '缺少 IDF 5.4 Python 环境' }
$old = Join-Path $taskRoot 'build/official-xiaozhi'
$table = Join-Path $old 'partition-table.bin'
$official = Join-Path $old 'xiaozhi.bin'
$assets = Join-Path $old 'generated_assets.bin'
$desktopDir = if ($DesktopPath) { $DesktopPath } else { Join-Path $taskRoot 'build/yiyi_desk' }
$desktop = Join-Path $desktopDir 'yiyi_desk.ino.bin'
foreach ($item in @(@($table, 0x1000), @($official, 0x300000), @($assets, 0x280000), @($desktop, 0x980000))) {
    $file = Get-Item -LiteralPath $item[0]
    if ($file.Length -le 0 -or $file.Length -gt $item[1]) { throw "Invalid restore image: $($file.FullName) $($file.Length)" }
}
$blank = Join-Path $env:TEMP 'otadata-blank-8k-restore.bin'
$bytes = [byte[]]::new(8192)
for ($i = 0; $i -lt $bytes.Length; $i++) { $bytes[$i] = 255 }
[IO.File]::WriteAllBytes($blank, $bytes)
$log = Join-Path $taskRoot 'build/eval/official-restore-flash.log'
& $py -m esptool --chip esp32s3 --port $Port --before usb_reset -b 921600 write_flash --flash_size 16MB `
    0x8000 $table 0xe000 $blank 0x10000 $desktop 0x990000 $official 0xc90000 $assets 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw '官方固件回滚写入失败' }
& $py -m esptool --chip esp32s3 --port $Port --before usb_reset -b 921600 verify_flash --flash_size 16MB `
    0x8000 $table 0xe000 $blank 0x10000 $desktop 0x990000 $official 0xc90000 $assets 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw '官方固件回滚校验失败' }
Write-Output 'Restored official 2.5.0 dual-app layout (app0 desktop / app1 official Xiaozhi).'
exit 0
