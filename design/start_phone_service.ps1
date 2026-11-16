param([switch]$Restart)
$ErrorActionPreference = 'Stop'
$phoneRoot = $PSScriptRoot
$phonePython = (Get-Command python).Source
$phonePort = Get-NetTCPConnection -LocalPort 8766 -State Listen -ErrorAction SilentlyContinue
if ($phonePort) {
    if (-not $Restart) { Write-Output 'Phone companion is already listening on 8766.'; exit 0 }
    $phoneProcess = Get-CimInstance Win32_Process -Filter "ProcessId=$($phonePort.OwningProcess)"
    if ($phoneProcess.CommandLine -notmatch 'phone_server\.py') { throw 'Port 8766 belongs to a different process.' }
    Stop-Process -Id $phonePort.OwningProcess
}
& $phonePython (Join-Path $phoneRoot 'make_phone_certificate.py')
if ($LASTEXITCODE -ne 0) { throw 'Phone HTTPS certificate generation failed.' }
$phoneArguments = @((Join-Path $phoneRoot 'phone_server.py'), '--cert', (Join-Path $phoneRoot '.local/phone-server.pem'), '--key', (Join-Path $phoneRoot '.local/phone-server-key.pem'))
Start-Process -FilePath $phonePython -ArgumentList ($phoneArguments | ForEach-Object { '"' + $_ + '"' }) -WorkingDirectory $phoneRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $phoneRoot '.local/service.log') -RedirectStandardError (Join-Path $phoneRoot '.local/service-error.log')
Write-Output 'Phone companion started on 8766, with HTTPS on 8767.'
