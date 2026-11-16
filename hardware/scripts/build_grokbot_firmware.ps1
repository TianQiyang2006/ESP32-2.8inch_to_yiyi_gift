param(
    [string]$IdfPath = "$env:USERPROFILE/.cache/yiyi-xiaozhi/esp-idf-6.0.1",
    [string]$PythonEnv = "$env:USERPROFILE/.espressif/python_env/idf6.0_py3.13_env",
    [switch]$Incremental
)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$source = Join-Path $taskRoot 'xiaozhi-esp32-2.5.3-dev-grokbot'
$cache = Join-Path $env:USERPROFILE '.cache/yiyi-xiaozhi/grokbot-yiyi-build'
$out = Join-Path $taskRoot 'build/grokbot-firmware'
$py = Join-Path $PythonEnv 'Scripts/python.exe'
if (-not (Test-Path -LiteralPath $py)) { throw 'Missing IDF 6 Python environment' }
if (-not (Test-Path -LiteralPath (Join-Path $IdfPath 'tools/idf.py'))) { throw 'Missing ESP-IDF 6.0.1 source' }
New-Item -ItemType Directory -Force -Path $cache, $out | Out-Null
& robocopy $source $cache /E /XD build managed_components releases .git /XF sdkconfig sdkconfig.old /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw 'Source synchronization failed' }
$env:IDF_PATH = $IdfPath
$env:IDF_PYTHON_ENV_PATH = $PythonEnv
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$env:IDF_SKIP_CHECK_SUBMODULES = '1'
$env:CMAKE_NINJA_FORCE_RESPONSE_FILE = '1'
$env:IDF_COMPONENT_CHECK_NEW_VERSION = '0'
$exports = & $py (Join-Path $IdfPath 'tools/idf_tools.py') export --format key-value
if ($LASTEXITCODE -ne 0) { throw 'IDF environment export failed' }
foreach ($entry in $exports) {
    if ($entry -match '^([A-Z_]+)=(.*)$') {
        $value = $Matches[2].Replace('%PATH%', $env:PATH).Replace('$PATH', $env:PATH)
        [Environment]::SetEnvironmentVariable($Matches[1], $value, 'Process')
    }
}
$env:PATH = (Split-Path $py) + ';' + $env:PATH
Push-Location $cache
try {
    & $py (Join-Path $IdfPath 'tools/idf.py') --version
    if ($LASTEXITCODE -ne 0) { throw 'IDF version check failed' }
    if ($Incremental) {
        $configured = Get-Content -LiteralPath 'sdkconfig'
        foreach ($required in @(
            'CONFIG_IDF_TARGET="esp32s3"',
            'CONFIG_BOARD_TYPE_YIYI_ES3C28P_GROKBOT=y',
            'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/yiyi-dual.csv"',
            'CONFIG_XIAOZHI_DISABLE_FIRMWARE_UPGRADE=y',
            'CONFIG_XIAOZHI_DISABLE_ASSET_DOWNLOAD=y',
            'CONFIG_XIAOZHI_PRESERVE_SHARED_NVS=y'
        )) {
            if ($required -notin $configured) { throw "Incremental build configuration mismatch: $required" }
        }
        & $py (Join-Path $IdfPath 'tools/idf.py') -DCMAKE_NINJA_FORCE_RESPONSE_FILE=ON build
    } else {
        & $py scripts/build.py yiyi-es3c28p-grokbot --name yiyi-es3c28p-grokbot --language zh-CN --wake-word nihaoxiaozhi
    }
    if ($LASTEXITCODE -ne 0) { throw 'GrokBot firmware build failed' }
    foreach ($pair in @(
        @('build/xiaozhi.bin', 'xiaozhi.bin'),
        @('build/generated_assets.bin', 'generated_assets.bin'),
        @('build/partition_table/partition-table.bin', 'partition-table.bin'),
        @('sdkconfig', 'sdkconfig'),
        @('dependencies.lock', 'dependencies.lock')
    )) {
        Copy-Item -LiteralPath $pair[0] -Destination (Join-Path $out $pair[1]) -Force
    }
    if ((Get-Item -LiteralPath (Join-Path $out 'xiaozhi.bin')).Length -gt 0x400000) { throw 'app1 exceeds 4 MiB' }
    if ((Get-Item -LiteralPath (Join-Path $out 'generated_assets.bin')).Length -gt 0x280000) { throw 'Assets exceed 2.5 MiB' }
    Get-ChildItem -LiteralPath $out -Filter '*.bin' | ForEach-Object { Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256 }
} finally { Pop-Location }
