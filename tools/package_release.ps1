param(
    [Parameter(Mandatory = $true)]
    [string]$ExePath,
    [string]$Version = "",
    [string]$OutputRoot = "dist",
    [string]$WindeployQtPath = "",
    [switch]$NoZip,
    [switch]$BuildInstaller,
    [string]$InnoCompilerPath = "",
    [string]$InstallerOutputDir = "",
    [string]$InstallerWorkOutputDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    $scriptDirectory = Split-Path -Parent $PSCommandPath
    return Split-Path -Parent $scriptDirectory
}

function Resolve-FullPath {
    param(
        [string]$Path,
        [string]$BasePath
    )

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $BasePath $Path))
}

function Read-ProjectVersion {
    param(
        [string]$RepoRoot
    )

    $cmakeFile = Join-Path $RepoRoot "CMakeLists.txt"
    $content = Get-Content -LiteralPath $cmakeFile -Raw
    $match = [regex]::Match($content, 'project\s*\(\s*QtDesktopPet\s+VERSION\s+([0-9]+(?:\.[0-9]+){1,3})')
    if ($match.Success) {
        return $match.Groups[1].Value
    }

    return "0.1.0"
}

function Resolve-WindeployQt {
    param(
        [string]$ExplicitPath
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $resolved = Resolve-FullPath -Path $ExplicitPath -BasePath (Get-Location)
        if (Test-Path -LiteralPath $resolved) {
            return $resolved
        }

        throw "windeployqt was not found at '$ExplicitPath'."
    }

    $command = Get-Command "windeployqt.exe" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    throw "windeployqt.exe was not found. Add the Qt bin directory to PATH or pass -WindeployQtPath."
}

function Resolve-InnoCompiler {
    param(
        [string]$ExplicitPath
    )

    $candidatePaths = @()
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $candidatePaths += $ExplicitPath
    } else {
        $candidatePaths += @(
            "D:\Inno Setup 7\ISCC.exe",
            "D:\Inno Setup 6\ISCC.exe",
            "C:\Program Files (x86)\Inno Setup 7\ISCC.exe",
            "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
        )
    }

    foreach ($candidatePath in $candidatePaths) {
        $resolved = Resolve-FullPath -Path $candidatePath -BasePath (Get-Location)
        if (Test-Path -LiteralPath $resolved) {
            return $resolved
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        throw "Inno Setup compiler was not found at '$ExplicitPath'."
    }

    throw "ISCC.exe was not found. Install Inno Setup or pass -InnoCompilerPath."
}

function Resolve-DefaultInstallerWorkOutputDir {
    param(
        [string]$RepoRoot
    )

    $driveRoot = [System.IO.Path]::GetPathRoot([System.IO.Path]::GetFullPath($RepoRoot))
    if ([string]::IsNullOrWhiteSpace($driveRoot)) {
        return [System.IO.Path]::GetFullPath("QtDesktopPetInstallerOutput")
    }

    return [System.IO.Path]::GetFullPath((Join-Path $driveRoot "QtDesktopPetInstallerOutput"))
}

function Assert-RequiredPath {
    param(
        [string]$Path,
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description was not found: $Path"
    }
}

function Assert-VersionSegment {
    param(
        [string]$Value
    )

    if ($Value -notmatch '^[0-9A-Za-z._-]+$') {
        throw "Version contains unsupported path characters: $Value"
    }
}

function Assert-ChildPath {
    param(
        [string]$Path,
        [string]$ParentPath,
        [string]$Description
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    $fullParentPath = [System.IO.Path]::GetFullPath($ParentPath).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    $expectedPrefix = $fullParentPath + [System.IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description must stay inside output root: $fullPath"
    }
}

function Copy-DirectoryFresh {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
}

function Invoke-CheckedProcess {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath exited with code $LASTEXITCODE."
    }
}

$repoRoot = Resolve-RepoRoot
$resolvedExePath = Resolve-FullPath -Path $ExePath -BasePath $repoRoot
$resolvedOutputRoot = Resolve-FullPath -Path $OutputRoot -BasePath $repoRoot
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = Read-ProjectVersion -RepoRoot $repoRoot
}
Assert-VersionSegment -Value $Version

$packageName = "QtDesktopPet-$Version-windows-x64"
$packageRoot = Join-Path $resolvedOutputRoot $packageName
$installerFileName = "$packageName-setup.exe"
$targetExePath = Join-Path $packageRoot "QtDesktopPet.exe"
$resourcesRoot = Join-Path $repoRoot "resources"
$charactersRoot = Join-Path $resourcesRoot "characters"
$iconsRoot = Join-Path $resourcesRoot "icons"
$licensePath = Join-Path $repoRoot "LICENSE"
$readmePath = Join-Path $repoRoot "README.md"
$installerScriptPath = Join-Path $repoRoot "installer\QtDesktopPet.iss"

Assert-RequiredPath -Path $resolvedExePath -Description "QtDesktopPet.exe"
Assert-RequiredPath -Path (Join-Path $charactersRoot "shiroko\character.json") -Description "Default character package"
Assert-RequiredPath -Path (Join-Path $iconsRoot "app_icon.ico") -Description "Application icon"
Assert-RequiredPath -Path $licensePath -Description "LICENSE"
Assert-RequiredPath -Path $readmePath -Description "README.md"

$windeployQt = Resolve-WindeployQt -ExplicitPath $WindeployQtPath
Assert-ChildPath -Path $packageRoot -ParentPath $resolvedOutputRoot -Description "Release package directory"

if (Test-Path -LiteralPath $packageRoot) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null

Copy-Item -LiteralPath $resolvedExePath -Destination $targetExePath -Force
Copy-DirectoryFresh -Source $charactersRoot -Destination (Join-Path $packageRoot "resources\characters")
Copy-DirectoryFresh -Source $iconsRoot -Destination (Join-Path $packageRoot "resources\icons")
Copy-Item -LiteralPath $licensePath -Destination (Join-Path $packageRoot "LICENSE") -Force
Copy-Item -LiteralPath $readmePath -Destination (Join-Path $packageRoot "README.md") -Force

Write-Host "Running windeployqt: $windeployQt"
Invoke-CheckedProcess -FilePath $windeployQt -Arguments @("--release", "--compiler-runtime", $targetExePath)

$manifestPath = Join-Path $packageRoot "package_manifest.txt"
@(
    "QtDesktopPet release package",
    "Version: $Version",
    "CreatedUtc: $((Get-Date).ToUniversalTime().ToString("o"))",
    "SourceExe: $resolvedExePath",
    "Includes: QtDesktopPet.exe, Qt runtime, resources, LICENSE, README.md",
    "Excludes: tools, docs, reports, build, dist, .git"
) | Set-Content -LiteralPath $manifestPath -Encoding UTF8

if (-not $NoZip) {
    $zipPath = Join-Path $resolvedOutputRoot "$packageName.zip"
    Assert-ChildPath -Path $zipPath -ParentPath $resolvedOutputRoot -Description "ZIP package"
    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }

    Compress-Archive -LiteralPath $packageRoot -DestinationPath $zipPath -Force
    Write-Host "ZIP package: $zipPath"
}

if ($BuildInstaller) {
    Assert-RequiredPath -Path $installerScriptPath -Description "Inno Setup script"
    $resolvedInnoCompilerPath = Resolve-InnoCompiler -ExplicitPath $InnoCompilerPath

    if ([string]::IsNullOrWhiteSpace($InstallerOutputDir)) {
        $installerFinalOutputDir = $repoRoot
    } else {
        $installerFinalOutputDir = Resolve-FullPath -Path $InstallerOutputDir -BasePath $repoRoot
    }

    if ([string]::IsNullOrWhiteSpace($InstallerWorkOutputDir)) {
        $installerWorkOutputDir = Resolve-DefaultInstallerWorkOutputDir -RepoRoot $repoRoot
    } else {
        $installerWorkOutputDir = Resolve-FullPath -Path $InstallerWorkOutputDir -BasePath $repoRoot
    }
    New-Item -ItemType Directory -Force -Path $installerWorkOutputDir | Out-Null
    New-Item -ItemType Directory -Force -Path $installerFinalOutputDir | Out-Null

    Invoke-CheckedProcess -FilePath $resolvedInnoCompilerPath -Arguments @(
        "/DAppVersion=$Version",
        "/DSourceDir=$packageRoot",
        "/DOutputDir=$installerWorkOutputDir",
        $installerScriptPath
    )

    $builtInstallerPath = Join-Path $installerWorkOutputDir $installerFileName
    Assert-RequiredPath -Path $builtInstallerPath -Description "Built installer"

    $finalInstallerPath = Join-Path $installerFinalOutputDir $installerFileName
    if ([System.IO.Path]::GetFullPath($builtInstallerPath) -ne [System.IO.Path]::GetFullPath($finalInstallerPath)) {
        Copy-Item -LiteralPath $builtInstallerPath -Destination $finalInstallerPath -Force
    }

    Write-Host "Installer work output: $installerWorkOutputDir"
    Write-Host "Installer final output: $finalInstallerPath"
}

Write-Host "Release package directory: $packageRoot"
