param(
    [string]$IdfPath = "$env:USERPROFILE/.cache/yiyi-xiaozhi/esp-idf-v5.4.3",
    [string]$PythonEnv = "$env:USERPROFILE/.espressif/python_env/idf5.4_py3.13_env"
)
$ErrorActionPreference = 'Stop'
# 构建“音乐小智”app1（Maggotxy/xiaozhi-esp32-music 本地移植副本，ESP-IDF 5.4.3）
# 产物输出到 build/music-firmware/：xiaozhi.bin、srmodels.bin、partition-table.bin、sdkconfig
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$source = Join-Path $taskRoot 'hardware/esp-idf/yiyi_music'
$cache = Join-Path $env:USERPROFILE '.cache/yiyi-xiaozhi/music-yiyi-build'
New-Item -ItemType Directory -Force -Path $cache | Out-Null
# IDF/CMake 在 Windows 上需要 ASCII 工作目录；保留 managed_components 与 build 以增量编译。
& robocopy $source $cache /E /XD build managed_components /XF sdkconfig sdkconfig.old /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw '音乐固件源码同步失败' }
# Keep both applications on the same tested voltage curve and protection policy.
Copy-Item -LiteralPath (Join-Path $taskRoot 'hardware/arduino/yiyi_desk/src/battery_policy.h') -Destination (Join-Path $cache 'main/boards/yiyi-es3c28p/battery_policy.h') -Force

$env:IDF_PATH = $IdfPath
$idfPython = Join-Path $PythonEnv 'Scripts/python.exe'
if (-not (Test-Path -LiteralPath $idfPython)) { throw '缺少 ESP-IDF 5.4.3 Python 环境（idf5.4_py3.13_env）' }
if (-not (Test-Path -LiteralPath (Join-Path $IdfPath 'tools/idf.py'))) { throw '缺少 ESP-IDF 5.4.3 源码（.cache/yiyi-xiaozhi/esp-idf-v5.4.3）' }
$exports = & $idfPython (Join-Path $IdfPath 'tools/idf_tools.py') export --format key-value
if ($LASTEXITCODE -ne 0) { throw 'IDF 工具导出失败' }
foreach ($entry in $exports) {
    if ($entry -match '^([^=]+)=(.*)$') {
        $name = $Matches[1]; $value = $Matches[2].Replace('$PATH', $env:PATH)
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
}
$env:PATH = (Split-Path $idfPython) + ';' + $env:PATH
$env:IDF_PYTHON_ENV_PATH = Split-Path (Split-Path $idfPython)
$env:IDF_TARGET = 'esp32s3'
Push-Location $cache
try {
    $idfPy = Join-Path $IdfPath 'tools/idf.py'
    if (-not (Test-Path -LiteralPath 'sdkconfig')) {
        & $idfPython $idfPy set-target esp32s3
        if ($LASTEXITCODE -ne 0) { throw 'idf.py set-target 失败' }
    }
    $marker = '# yiyi music layout overrides'
    $hasMarker = (Test-Path -LiteralPath 'sdkconfig') -and (Select-String -LiteralPath 'sdkconfig' -Pattern ([regex]::Escape($marker)) -Quiet)
    if (-not $hasMarker) {
        Add-Content -LiteralPath 'sdkconfig' -Value ''
        Add-Content -LiteralPath 'sdkconfig' -Value $marker
    }
    foreach ($setting in (Get-Content -LiteralPath (Join-Path $source 'sdkconfig.yiyi.append') | Where-Object { $_ -match '^CONFIG_[A-Z0-9_]+=.' })) {
        if (-not (Select-String -LiteralPath 'sdkconfig' -Pattern ("^" + [regex]::Escape($setting) + "$") -Quiet)) {
            Add-Content -LiteralPath 'sdkconfig' -Value $setting
        }
    }

    $espMl307 = Get-ChildItem -LiteralPath (Join-Path $cache 'managed_components') -Directory |
        Where-Object { $_.Name -match '(^|__)esp-ml307$' } | Select-Object -First 1
    if (-not $espMl307) { throw 'esp-ml307 managed component is missing' }
    $udpSource = Join-Path $espMl307.FullName 'src/esp/esp_udp.cc'
    $udpText = Get-Content -LiteralPath $udpSource -Raw
    $udpCoreTask = '    }, "udp_receive", 4096, this, 8, &receive_task_handle_, 0);'
    if ($udpText.Contains('    xTaskCreate([](void* arg) {')) {
        $udpText = $udpText.Replace('    xTaskCreate([](void* arg) {', '    xTaskCreatePinnedToCore([](void* arg) {')
    }
    foreach ($oldTaskLine in @(
        '    }, "udp_receive", 4096, this, 1, &receive_task_handle_);',
        '    }, "udp_receive", 4096, this, 5, &receive_task_handle_);'
    )) {
        if ($udpText.Contains($oldTaskLine)) { $udpText = $udpText.Replace($oldTaskLine, $udpCoreTask) }
    }
    $socketCreate = '    udp_fd_ = socket(AF_INET, SOCK_DGRAM, 0);'
    $socketTuned = '    udp_fd_ = socket(AF_INET, SOCK_DGRAM, 0);' + "`n" + '    if (udp_fd_ >= 0) {' + "`n" + '        int receive_buffer = 64 * 1024;' + "`n" + '        setsockopt(udp_fd_, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer));' + "`n" + '    }'
    if ($udpText.Contains($socketCreate) -and -not $udpText.Contains('int receive_buffer = 64 * 1024;')) {
        $udpText = $udpText.Replace($socketCreate, $socketTuned)
    }
    if (-not $udpText.Contains($udpCoreTask)) {
        throw 'Unable to apply speech UDP receive priority patch'
    }
    [IO.File]::WriteAllText($udpSource, $udpText, [Text.UTF8Encoding]::new($false))
    Write-Output 'Speech UDP receive pinned to core 0 at priority 8; socket receive buffer=64K; LWIP UDP mailbox=16'

    & $idfPython $idfPy build
    if ($LASTEXITCODE -ne 0) { throw '音乐固件编译失败' }
    $out = Join-Path $taskRoot 'build/music-firmware'
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    Copy-Item -LiteralPath (Join-Path $cache 'build/xiaozhi.bin') -Destination $out -Force
    Copy-Item -LiteralPath (Join-Path $cache 'build/srmodels/srmodels.bin') -Destination $out -Force
    Copy-Item -LiteralPath (Join-Path $cache 'build/partition_table/partition-table.bin') -Destination $out -Force
    Copy-Item -LiteralPath (Join-Path $cache 'sdkconfig') -Destination $out -Force
    $bin = Get-Item -LiteralPath (Join-Path $out 'xiaozhi.bin')
    $models = Get-Item -LiteralPath (Join-Path $out 'srmodels.bin')
    if ($bin.Length -gt 0x500000) { throw "app1 镜像超过 5 MiB 分区: $($bin.Length)" }
    if ($models.Length -gt 0x280000) { throw "srmodels.bin 超过 model 分区: $($models.Length)" }
    Write-Output "music firmware: xiaozhi.bin=$($bin.Length) srmodels.bin=$($models.Length) -> $out"
} finally { Pop-Location }
exit 0
