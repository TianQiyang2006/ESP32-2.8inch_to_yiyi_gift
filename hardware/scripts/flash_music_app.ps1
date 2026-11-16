param(
    [string]$Port = 'COM8',
    [switch]$SkipBuild,
    [string]$MusicBin = '',
    [string]$SrmodelsBin = ''
)
$ErrorActionPreference = 'Stop'
# 音乐固件（xiaozhi-esp32-music fork）安装脚本：新布局 app0 7.5MiB / app1 5.0MiB / model 2.5MiB / spiffs 0.96MiB。
# 只写分区表、otadata（启动桌面）、app1 与 model，保留桌面 app0、NVS、LittleFS 和 bootloader。
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$projectPartitions = Join-Path $taskRoot 'hardware/arduino/yiyi_desk/partitions.csv'
if ((Get-Content -LiteralPath $projectPartitions -Raw) -notmatch '(?im)^app0\s*,\s*app\s*,\s*ota_0\s*,\s*0x10000\s*,\s*0x780000\s*,') {
    throw 'Legacy music layout requires app0 7.5 MiB. Current GrokBot updates use install_grokbot.py.'
}
$py = Join-Path $env:USERPROFILE '.espressif/python_env/idf5.4_py3.13_env/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $py)) { throw '缺少 IDF 5.4 Python 环境' }
$source = Join-Path $taskRoot 'hardware/esp-idf/yiyi_music'
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build_music_firmware.ps1')
}
$built = Join-Path $taskRoot 'build/music-firmware'
$bin = if ($MusicBin) { $MusicBin } else { Join-Path $built 'xiaozhi.bin' }
$srmodels = if ($SrmodelsBin) { $SrmodelsBin } else { Join-Path $built 'srmodels.bin' }
$table = Join-Path $built 'partition-table.bin'
foreach ($item in @(@($bin, 0x500000), @($srmodels, 0x280000), @($table, 0x1000))) {
    $file = Get-Item -LiteralPath $item[0]
    if ($file.Length -le 0 -or $file.Length -gt $item[1]) { throw "Invalid image size: $($file.FullName)" }
}
$blank = Join-Path $env:TEMP 'otadata-blank-8k.bin'
$bytes = [byte[]]::new(8192)
for ($i = 0; $i -lt $bytes.Length; $i++) { $bytes[$i] = 255 }
[IO.File]::WriteAllBytes($blank, $bytes)
$log = Join-Path $taskRoot 'build/eval/music-flash.log'
& $py -m esptool --chip esp32s3 --port $Port --before usb_reset -b 921600 write_flash --flash_size 16MB `
    0x8000 $table 0xe000 $blank 0x790000 $bin 0xc90000 $srmodels 2>&1 | Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) { throw 'Music firmware flash failed' }
& $py -m esptool --chip esp32s3 --port $Port --before usb_reset -b 921600 verify_flash --flash_size 16MB `
    0x8000 $table 0xe000 $blank 0x790000 $bin 0xc90000 $srmodels 2>&1 | Tee-Object -FilePath $log -Append
if ($LASTEXITCODE -ne 0) { throw 'Music firmware verify failed' }
Write-Output 'Music firmware installed to app1; device should boot the desktop first.'
exit 0
