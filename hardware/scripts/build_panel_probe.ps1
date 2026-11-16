$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $PSScriptRoot 'panel_build_flags.ps1')
$cli=Join-Path $taskRoot 'tools/arduino-cli/arduino-cli.exe'
$config=Join-Path $taskRoot 'tools/arduino-cli/config.yaml'
$probeSource=Join-Path $taskRoot 'hardware/arduino/panel_probe/src'
New-Item -ItemType Directory -Path $probeSource -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'hardware/arduino/yiyi_desk/src/touch_transform.h') -Destination (Join-Path $probeSource 'touch_transform.h')
& $cli compile --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB' --config-file $config --libraries (Join-Path $taskRoot 'tools/panel-libraries') --build-path (Join-Path $taskRoot 'build/panel-probe-compile') --build-property "compiler.cpp.extra_flags=$panelBuildFlags" --output-dir (Join-Path $taskRoot 'build/panel-probe-output') (Join-Path $taskRoot 'hardware/arduino/panel_probe')
if($LASTEXITCODE -ne 0){throw 'Panel probe build failed'}
