param(
    [string]$BridgeExePath = "",
    [string]$Source = "custom",
    [string]$Workspace = "SelfTest",
    [string]$SessionId = "selftest-1",
    [string]$PipeName = "QtDesktopPet.AgentStatus.v1",
    [int]$StepDelaySec = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-BridgeExecutable {
    param(
        [string]$ExplicitPath
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $resolved = if ([System.IO.Path]::IsPathRooted($ExplicitPath)) {
            [System.IO.Path]::GetFullPath($ExplicitPath)
        } else {
            [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $ExplicitPath))
        }
        if (Test-Path -LiteralPath $resolved -PathType Leaf) {
            return $resolved
        }
        throw "Specified BridgeExePath does not exist: $resolved"
    }

    $scriptDir = Split-Path -Parent $PSCommandPath
    $repoRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDir "..\.."))
    $candidates = @(
        (Join-Path $scriptDir "QtDesktopPetBridge.exe"),
        (Join-Path (Get-Location) "QtDesktopPetBridge.exe"),
        (Join-Path $repoRoot "QtDesktopPetBridge.exe")
    )

    $buildRoot = Join-Path $repoRoot "build"
    if (Test-Path -LiteralPath $buildRoot -PathType Container) {
        $foundInBuild = Get-ChildItem -LiteralPath $buildRoot -Filter "QtDesktopPetBridge.exe" -Recurse -File -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
        if (-not [string]::IsNullOrWhiteSpace($foundInBuild)) {
            $candidates += $foundInBuild
        }
    }

    $distRoot = Join-Path $repoRoot "dist"
    if (Test-Path -LiteralPath $distRoot -PathType Container) {
        $foundInDist = Get-ChildItem -LiteralPath $distRoot -Filter "QtDesktopPetBridge.exe" -Recurse -File -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
        if (-not [string]::IsNullOrWhiteSpace($foundInDist)) {
            $candidates += $foundInDist
        }
    }

    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }

    throw "Could not locate QtDesktopPetBridge.exe. Pass -BridgeExePath <path-to-QtDesktopPetBridge.exe>."
}

function Format-ProcessArgument {
    param(
        [string]$Value
    )

    if ([string]::IsNullOrEmpty($Value)) {
        return '""'
    }
    if ($Value -match '[\s"]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }
    return $Value
}

function Invoke-BridgeStep {
    param(
        [string]$Executable,
        [string]$State,
        [string]$Summary
    )

    $rawArgs = @(
        "--source", $Source,
        "--state", $State,
        "--session", $SessionId,
        "--workspace", $Workspace,
        "--summary", $Summary,
        "--pipe", $PipeName,
        "--timeout-ms", "1000",
        "--strict"
    )
    $formattedArgs = @()
    foreach ($arg in $rawArgs) {
        $formattedArgs += (Format-ProcessArgument -Value $arg)
    }

    # Keep the console bridge hidden while waiting for its actual exit code.
    $process = Start-Process -FilePath $Executable -ArgumentList $formattedArgs -Wait -PassThru -WindowStyle Hidden
    $exitCode = if ($null -ne $process) { [int]$process.ExitCode } else { 1 }
    if ($exitCode -ne 0) {
        Write-Host ""
        Write-Host "FAILED at state '$State' (exit code $exitCode)." -ForegroundColor Red
        Write-Host "Troubleshooting checklist:" -ForegroundColor Yellow
        Write-Host "  1. Ensure QtDesktopPet.exe is running."
        Write-Host "  2. In Settings -> App, verify 'Enable external coding agent status bridge' is checked."
        Write-Host "  3. Confirm pipe name matches '$PipeName'."
        Write-Host "  4. Ensure the matching Qt6Core.dll and Qt6Network.dll sit beside QtDesktopPetBridge.exe."
        exit 1
    }
}

try {
    $bridgeExe = Resolve-BridgeExecutable -ExplicitPath $BridgeExePath
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}

$delaySeconds = [Math]::Max(0, $StepDelaySec)
Write-Host "Using bridge: $bridgeExe"
Write-Host "Target pipe : $PipeName"
Write-Host "Session     : $Source/$SessionId ($Workspace)"
Write-Host ""

Write-Host "[1/3] Sending 'working'..."
Invoke-BridgeStep -Executable $bridgeExe -State "working" -Summary "Self-test step 1/3: working"
if ($delaySeconds -gt 0) {
    Start-Sleep -Seconds $delaySeconds
}

Write-Host "[2/3] Sending 'needs_input'..."
Invoke-BridgeStep -Executable $bridgeExe -State "needs_input" -Summary "Self-test step 2/3: waiting for confirmation"
if ($delaySeconds -gt 0) {
    Start-Sleep -Seconds $delaySeconds
}

Write-Host "[3/3] Sending 'done'..."
Invoke-BridgeStep -Executable $bridgeExe -State "done" -Summary "Self-test step 3/3: completed"

Write-Host ""
Write-Host "PASS: All 3 self-test events delivered to QtDesktopPet." -ForegroundColor Green
Write-Host "Check the pet's status capsule bar and detail card on screen."
exit 0
