param(
    [string]$ProcessName = "QtDesktopPet",
    [Alias("Pid")]
    [int]$ProcessId = 0,
    [ValidateRange(1, 86400)]
    [int]$IntervalSeconds = 5,
    [ValidateRange(1, 604800)]
    [int]$DurationSeconds = 300,
    [string]$OutputPath = "reports/perf"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-TargetProcess {
    param(
        [string]$Name,
        [int]$Id
    )

    if ($Id -gt 0) {
        return Get-Process -Id $Id -ErrorAction SilentlyContinue
    }

    $processes = @(Get-Process -Name $Name -ErrorAction SilentlyContinue)
    if ($processes.Count -eq 0) {
        return $null
    }

    if ($processes.Count -gt 1) {
        Write-Warning "Multiple processes named '$Name' were found. Sampling PID $($processes[0].Id). Use -Pid to target a specific process."
    }

    return $processes[0]
}

function Read-ProcessSnapshot {
    param(
        [System.Diagnostics.Process]$Process
    )

    $path = ""
    try {
        $path = $Process.Path
    }
    catch {
        $path = ""
    }

    $responding = ""
    try {
        $responding = $Process.Responding
    }
    catch {
        $responding = ""
    }

    return [pscustomobject]@{
        TimestampUtc      = (Get-Date).ToUniversalTime().ToString("o")
        Pid               = $Process.Id
        CpuSeconds        = [double]($Process.CPU)
        WorkingSetMB      = [math]::Round($Process.WorkingSet64 / 1MB, 2)
        PrivateMemoryMB   = [math]::Round($Process.PrivateMemorySize64 / 1MB, 2)
        HandleCount       = $Process.HandleCount
        ThreadCount       = $Process.Threads.Count
        Responding        = $responding
        Path              = $path
    }
}

function New-OutputFilePath {
    param(
        [string]$Directory,
        [string]$Name
    )

    if (-not (Test-Path -LiteralPath $Directory)) {
        New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    }

    $safeName = $Name -replace '[^a-zA-Z0-9._-]', '_'
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    return Join-Path $Directory "$timestamp-$safeName.csv"
}

$target = Resolve-TargetProcess -Name $ProcessName -Id $ProcessId
if ($null -eq $target) {
    if ($ProcessId -gt 0) {
        Write-Error "Process with PID $ProcessId was not found."
    }
    else {
        Write-Error "Process '$ProcessName' was not found. Start the app first or pass -Pid."
    }
    exit 1
}

$logicalProcessorCount = [Environment]::ProcessorCount
if ($logicalProcessorCount -lt 1) {
    $logicalProcessorCount = 1
}

$outputFile = New-OutputFilePath -Directory $OutputPath -Name $target.ProcessName
$sampleCount = [math]::Max(1, [math]::Ceiling($DurationSeconds / $IntervalSeconds))

Write-Host "Sampling PID $($target.Id) ($($target.ProcessName)) every $IntervalSeconds seconds for up to $DurationSeconds seconds."
Write-Host "Output: $outputFile"

$previousSnapshot = Read-ProcessSnapshot -Process $target
$previousTime = Get-Date
$firstRow = [pscustomobject]@{
    TimestampUtc     = $previousSnapshot.TimestampUtc
    Pid              = $previousSnapshot.Pid
    CpuPercent       = 0
    WorkingSetMB     = $previousSnapshot.WorkingSetMB
    PrivateMemoryMB  = $previousSnapshot.PrivateMemoryMB
    HandleCount      = $previousSnapshot.HandleCount
    ThreadCount      = $previousSnapshot.ThreadCount
    Responding       = $previousSnapshot.Responding
    Path             = $previousSnapshot.Path
    Status           = "running"
}
$firstRow | Export-Csv -Path $outputFile -NoTypeInformation -Encoding UTF8

for ($index = 1; $index -le $sampleCount; $index++) {
    Start-Sleep -Seconds $IntervalSeconds

    $currentProcess = Get-Process -Id $target.Id -ErrorAction SilentlyContinue
    if ($null -eq $currentProcess) {
        [pscustomobject]@{
            TimestampUtc     = (Get-Date).ToUniversalTime().ToString("o")
            Pid              = $target.Id
            CpuPercent       = ""
            WorkingSetMB     = ""
            PrivateMemoryMB  = ""
            HandleCount      = ""
            ThreadCount      = ""
            Responding       = ""
            Path             = $previousSnapshot.Path
            Status           = "exited"
        } | Export-Csv -Path $outputFile -NoTypeInformation -Encoding UTF8 -Append

        Write-Warning "Process $($target.Id) exited. Sampling stopped."
        break
    }

    $currentSnapshot = Read-ProcessSnapshot -Process $currentProcess
    $currentTime = Get-Date
    $elapsedSeconds = [math]::Max(0.001, ($currentTime - $previousTime).TotalSeconds)
    $cpuPercent = (($currentSnapshot.CpuSeconds - $previousSnapshot.CpuSeconds) / $elapsedSeconds) * 100 / $logicalProcessorCount
    if ($cpuPercent -lt 0) {
        $cpuPercent = 0
    }

    [pscustomobject]@{
        TimestampUtc     = $currentSnapshot.TimestampUtc
        Pid              = $currentSnapshot.Pid
        CpuPercent       = [math]::Round($cpuPercent, 2)
        WorkingSetMB     = $currentSnapshot.WorkingSetMB
        PrivateMemoryMB  = $currentSnapshot.PrivateMemoryMB
        HandleCount      = $currentSnapshot.HandleCount
        ThreadCount      = $currentSnapshot.ThreadCount
        Responding       = $currentSnapshot.Responding
        Path             = $currentSnapshot.Path
        Status           = "running"
    } | Export-Csv -Path $outputFile -NoTypeInformation -Encoding UTF8 -Append

    $previousSnapshot = $currentSnapshot
    $previousTime = $currentTime
}

Write-Host "Sampling finished."
