param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [ValidateRange(1, 60)]
    [int]$SampleSeconds = 3
)

$ErrorActionPreference = 'Stop'

$resolved = (Resolve-Path $Executable).Path
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
$process = Start-Process -FilePath $resolved -ArgumentList '--resident-smoke' -PassThru

try {
    $inputIdle = $process.WaitForInputIdle(5000)
    $startupMs = $stopwatch.Elapsed.TotalMilliseconds
    if (-not $inputIdle) {
        throw 'process did not reach input-idle state within 5 seconds'
    }

    Start-Sleep -Milliseconds 250
    $sample = Get-Process -Id $process.Id -ErrorAction Stop
    $cpuStart = $sample.TotalProcessorTime.TotalSeconds

    Start-Sleep -Seconds $SampleSeconds
    $sample = Get-Process -Id $process.Id -ErrorAction Stop
    $cpuEnd = $sample.TotalProcessorTime.TotalSeconds
    $privateBytes = [int64]$sample.PrivateMemorySize64
    $workingSetBytes = [int64]$sample.WorkingSet64
    $perf = Get-CimInstance -ClassName Win32_PerfFormattedData_PerfProc_Process -Filter ("IDProcess = " + $process.Id) -ErrorAction SilentlyContinue
    $privateWorkingSetBytes = $null
    if ($perf -and $null -ne $perf.WorkingSetPrivate) {
        $privateWorkingSetBytes = [int64]$perf.WorkingSetPrivate
    }

    $cpuPercentOneCore = (($cpuEnd - $cpuStart) / $SampleSeconds) * 100.0

    Write-Output ('STARTUP_INPUT_IDLE_MS={0:N1}' -f $startupMs)
    Write-Output ('CPU_PERCENT_ONE_CORE={0:N3}' -f $cpuPercentOneCore)
    Write-Output ("PRIVATE_BYTES=$privateBytes")
    if ($null -ne $privateWorkingSetBytes) {
        Write-Output ("PRIVATE_WORKING_SET_BYTES=$privateWorkingSetBytes")
    } else {
        Write-Output 'PRIVATE_WORKING_SET_BYTES=UNAVAILABLE'
    }
    Write-Output ("WORKING_SET_BYTES=$workingSetBytes")
    Write-Output ("EXE_BYTES=" + (Get-Item $resolved).Length)
}
finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
}
