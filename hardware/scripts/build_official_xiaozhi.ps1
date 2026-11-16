param(
    [string]$IdfPath = "$env:USERPROFILE/.cache/yiyi-xiaozhi/esp-idf-6.0.1",
    [string]$PythonEnv = "$env:USERPROFILE/.espressif/python_env/idf6.0_py3.13_env"
)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$source = Join-Path $taskRoot 'hardware/esp-idf/yiyi_xiaozhi'
$cache = Join-Path $env:USERPROFILE '.cache/yiyi-xiaozhi/firmware-2.5.0'
New-Item -ItemType Directory -Path $cache -Force | Out-Null
# IDF/CMake and some vendor scripts need an ASCII-only working directory on Windows.
& robocopy $source $cache /E /XD build managed_components .git /XF sdkconfig dependencies.lock lang_config.h /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw 'Official source synchronization failed' }
$env:IDF_PATH = $IdfPath
$idfPython = Join-Path $PythonEnv 'Scripts/python.exe'
if (-not (Test-Path -LiteralPath $idfPython)) { throw 'Install the ESP-IDF 6.0 Python environment first' }
$exports = & $idfPython (Join-Path $IdfPath 'tools/idf_tools.py') export --format key-value
if ($LASTEXITCODE -ne 0) { throw 'IDF tools are incomplete' }
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
    & $idfPython (Join-Path $cache 'scripts/gen_lang.py') --language zh-CN --output (Join-Path $cache 'main/assets/lang_config.h')
    if ($LASTEXITCODE -ne 0) { throw 'Official language generation failed' }
    # Use the canonical single-board builder: it generates defaults and replaces
    # stale sdkconfig. The CLI also validates every unrelated legacy board in
    # this vendor snapshot, whose catalog is not yet fully migrated.
    & $idfPython -c "import sys; sys.path.insert(0, 'scripts'); import build; build.build_board('yiyi-es3c28p', name_filter='yiyi-es3c28p', idf_version=build._detect_idf_version())"
    if ($LASTEXITCODE -ne 0) { throw 'Official Xiaozhi compilation failed' }
    $out = Join-Path $taskRoot 'build/official-xiaozhi'
    New-Item -ItemType Directory -Path $out -Force | Out-Null
    foreach ($file in @('xiaozhi.bin','generated_assets.bin','flasher_args.json','sdkconfig')) {
        $input = if ($file -eq 'sdkconfig') { Join-Path $cache $file } else { Join-Path "$cache/build" $file }
        Copy-Item -LiteralPath $input -Destination $out
    }
    Copy-Item -LiteralPath "$cache/build/partition_table/partition-table.bin" -Destination $out
    Copy-Item -LiteralPath "$cache/dependencies.lock" -Destination (Join-Path $source 'dependencies.lock')
} finally { Pop-Location }
