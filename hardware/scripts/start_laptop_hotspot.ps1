param([string]$ExpectedSsid = 'qingyu')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Networking.Connectivity.NetworkInformation, Windows.Networking.Connectivity, ContentType = WindowsRuntime]
$null = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime]
$resultType = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime]
$profile = [Windows.Networking.Connectivity.NetworkInformation]::GetInternetConnectionProfile()
$manager = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager]::CreateFromConnectionProfile($profile)
$config = $manager.GetCurrentAccessPointConfiguration()
if ($config.Ssid -ne $ExpectedSsid) { throw 'Hotspot name differs from the requested network' }
if ($manager.TetheringOperationalState.ToString() -eq 'Off') {
    $asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.IsGenericMethodDefinition -and
        $_.GetGenericArguments().Count -eq 1 -and $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
    } | Select-Object -First 1
    $operation = $manager.StartTetheringAsync()
    $task = $asTask.MakeGenericMethod($resultType).Invoke($null, @($operation))
    if (-not $task.Wait(20000)) { throw 'Hotspot start timed out' }
    if ($task.Result.Status.ToString() -ne 'Success') {
        throw ('Hotspot start failed: ' + $task.Result.Status.ToString())
    }
}
[pscustomobject]@{
    SSID = $config.Ssid
    Band = $config.Band.ToString()
    State = $manager.TetheringOperationalState.ToString()
    ClientCount = $manager.ClientCount
} | ConvertTo-Json
