param(
    [Parameter(Mandatory = $true)]
    [string]$ExePath,
    [string]$BridgeExePath = "",
    [string]$Version = "",
    [string]$OutputRoot = "dist",
    [string]$WindeployQtPath = "",
    [string]$MsvcRuntimeDir = "",
    [string]$WebpRuntimeDir = "",
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

    throw "Cannot determine the project version from CMakeLists.txt."
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

function Test-MsvcRuntimeDirectory {
    param(
        [string]$Directory
    )

    $requiredFiles = @(
        "msvcp140.dll",
        "msvcp140_1.dll",
        "msvcp140_2.dll",
        "vcruntime140.dll",
        "vcruntime140_1.dll"
    )
    foreach ($fileName in $requiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $Directory $fileName) -PathType Leaf)) {
            return $false
        }
    }

    return $true
}

function Resolve-MsvcRuntimeDirectory {
    param(
        [string]$ExplicitPath
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $resolved = Resolve-FullPath -Path $ExplicitPath -BasePath (Get-Location)
        if ((Test-Path -LiteralPath $resolved -PathType Container) -and
            (Test-MsvcRuntimeDirectory -Directory $resolved)) {
            return $resolved
        }

        throw "The x64 MSVC runtime directory is incomplete: $resolved"
    }

    $x64Roots = @()
    if (-not [string]::IsNullOrWhiteSpace($env:VCToolsRedistDir)) {
        $x64Roots += Join-Path $env:VCToolsRedistDir "x64"
    }

    if (-not [string]::IsNullOrWhiteSpace($env:VCINSTALLDIR)) {
        $redistRoot = Join-Path $env:VCINSTALLDIR "Redist\MSVC"
        if (Test-Path -LiteralPath $redistRoot -PathType Container) {
            $versionDirectories = Get-ChildItem -LiteralPath $redistRoot -Directory |
                Sort-Object Name -Descending
            foreach ($versionDirectory in $versionDirectories) {
                $x64Roots += Join-Path $versionDirectory.FullName "x64"
            }
        }
    }

    foreach ($x64Root in $x64Roots) {
        if (-not (Test-Path -LiteralPath $x64Root -PathType Container)) {
            continue
        }

        $runtimeDirectories = Get-ChildItem -LiteralPath $x64Root -Directory -Filter "Microsoft.VC*.CRT" |
            Sort-Object Name -Descending
        foreach ($runtimeDirectory in $runtimeDirectories) {
            if (Test-MsvcRuntimeDirectory -Directory $runtimeDirectory.FullName) {
                return $runtimeDirectory.FullName
            }
        }
    }

    throw "The x64 MSVC runtime was not found. Run from a Visual Studio developer shell or pass -MsvcRuntimeDir."
}

function Copy-MsvcRuntime {
    param(
        [string]$SourceDirectory,
        [string]$DestinationDirectory
    )

    $runtimeFiles = @(Get-ChildItem -LiteralPath $SourceDirectory -File -Filter "*.dll")
    if ($runtimeFiles.Count -eq 0) {
        throw "No MSVC runtime DLLs were found in: $SourceDirectory"
    }

    foreach ($runtimeFile in $runtimeFiles) {
        Copy-Item -LiteralPath $runtimeFile.FullName -Destination $DestinationDirectory -Force
    }

    if (-not (Test-MsvcRuntimeDirectory -Directory $DestinationDirectory)) {
        throw "MSVC runtime deployment validation failed: $DestinationDirectory"
    }
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

    Assert-ChildPath -Path $Destination -ParentPath $packageRoot -Description "Packaged resource directory"
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
if ([string]::IsNullOrWhiteSpace($BridgeExePath)) {
    $resolvedBridgeExePath = Join-Path (Split-Path -Parent $resolvedExePath) "QtDesktopPetBridge.exe"
} else {
    $resolvedBridgeExePath = Resolve-FullPath -Path $BridgeExePath -BasePath $repoRoot
}
$resolvedOutputRoot = Resolve-FullPath -Path $OutputRoot -BasePath $repoRoot
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = Read-ProjectVersion -RepoRoot $repoRoot
}
Assert-VersionSegment -Value $Version

$packageName = "QtDesktopPet-$Version-windows-x64"
$packageRoot = Join-Path $resolvedOutputRoot $packageName
$installerFileName = "$packageName-setup.exe"
$targetExePath = Join-Path $packageRoot "QtDesktopPet.exe"
$targetBridgeExePath = Join-Path $packageRoot "QtDesktopPetBridge.exe"
$resourcesRoot = Join-Path $repoRoot "resources"
$charactersRoot = Join-Path $resourcesRoot "characters"
$iconsRoot = Join-Path $resourcesRoot "icons"
$soundsRoot = Join-Path $resourcesRoot "sounds"
$licensePath = Join-Path $repoRoot "LICENSE"
$readmePath = Join-Path $repoRoot "README.md"
$userGuidePath = Join-Path $repoRoot "使用说明.md"
$agentExamplesRoot = Join-Path $repoRoot "examples\agent_bridge"
$verifyScriptPath = Join-Path $repoRoot "tools\verify_release.ps1"
$installerScriptPath = Join-Path $repoRoot "installer\QtDesktopPet.iss"

Assert-RequiredPath -Path $resolvedExePath -Description "QtDesktopPet.exe"
Assert-RequiredPath -Path $resolvedBridgeExePath -Description "QtDesktopPetBridge.exe (build target QtDesktopPetBridge or pass -BridgeExePath)"
Assert-RequiredPath -Path (Join-Path $charactersRoot "shiroko\character.json") -Description "Default character package"
Assert-RequiredPath -Path (Join-Path $iconsRoot "app_icon.ico") -Description "Application icon"
Assert-RequiredPath -Path (Join-Path $soundsRoot "reminders\reminder_default.wav") -Description "Default reminder sound"
Assert-RequiredPath -Path $licensePath -Description "LICENSE"
Assert-RequiredPath -Path $readmePath -Description "README.md"
Assert-RequiredPath -Path $userGuidePath -Description "Offline user guide"
Assert-RequiredPath -Path $verifyScriptPath -Description "Release dependency verifier"
$agentSupportFiles = @(
    "README.md", "codex.hooks.json", "antigravity.hooks.json",
    "dsh-cordis-plugin.js", "test_bridge.ps1"
)
foreach ($fileName in $agentSupportFiles) {
    Assert-RequiredPath -Path (Join-Path $agentExamplesRoot $fileName) -Description "Agent setup asset"
}

$windeployQt = Resolve-WindeployQt -ExplicitPath $WindeployQtPath
$webpBundle = if ($WebpRuntimeDir) {
    Resolve-FullPath -Path $WebpRuntimeDir -BasePath (Get-Location)
} else {
    Join-Path $repoRoot "build\webp-runtime\bundle"
}
foreach ($requiredWebpFile in @("qwebp.dll", "dependency.json", "NOTICE.txt",
    "libwebp-COPYING.txt", "libwebp-PATENTS.txt", "libwebp-AUTHORS.txt",
    "LGPL-3.0-only.txt", "GPL-3.0-only.txt", "libwebp-source.tar.gz", "qtimageformats-source.zip", "build_webp_plugin.ps1")) {
    Assert-RequiredPath -Path (Join-Path $webpBundle $requiredWebpFile) -Description "WebP runtime bundle (run tools/build_webp_plugin.ps1 or pass -WebpRuntimeDir)"
}
$isMsvcDeployment = (-not [string]::IsNullOrWhiteSpace($MsvcRuntimeDir)) -or
    ($windeployQt -match '(?i)[\\/]msvc[^\\/]*[\\/]bin[\\/]windeployqt\.exe$')
$resolvedMsvcRuntimeDir = ""
if ($isMsvcDeployment) {
    $resolvedMsvcRuntimeDir = Resolve-MsvcRuntimeDirectory -ExplicitPath $MsvcRuntimeDir
}
Assert-ChildPath -Path $packageRoot -ParentPath $resolvedOutputRoot -Description "Release package directory"

if (Test-Path -LiteralPath $packageRoot) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null

Copy-Item -LiteralPath $resolvedExePath -Destination $targetExePath -Force
Copy-Item -LiteralPath $resolvedBridgeExePath -Destination $targetBridgeExePath -Force
Copy-DirectoryFresh -Source $charactersRoot -Destination (Join-Path $packageRoot "resources\characters")
Copy-DirectoryFresh -Source $iconsRoot -Destination (Join-Path $packageRoot "resources\icons")
Copy-DirectoryFresh -Source $soundsRoot -Destination (Join-Path $packageRoot "resources\sounds")
Copy-Item -LiteralPath $licensePath -Destination (Join-Path $packageRoot "LICENSE") -Force
Copy-Item -LiteralPath $readmePath -Destination (Join-Path $packageRoot "README.md") -Force
Copy-Item -LiteralPath $userGuidePath -Destination (Join-Path $packageRoot "使用说明.md") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "CHANGELOG.md") -Destination $packageRoot
$targetAgentExamplesRoot = Join-Path $packageRoot "examples\agent_bridge"
New-Item -ItemType Directory -Force -Path $targetAgentExamplesRoot | Out-Null
foreach ($fileName in $agentSupportFiles) {
    Copy-Item -LiteralPath (Join-Path $agentExamplesRoot $fileName) -Destination $targetAgentExamplesRoot -Force
}
# Keep Qt plugin lookup relative to the installed application, never the build Kit.
@("[Paths]", "Prefix=.", "Plugins=.") | Set-Content -LiteralPath (Join-Path $packageRoot "qt.conf") -Encoding ASCII

Write-Host "Running windeployqt: $windeployQt"
$windeployArguments = @("--release")
if ($isMsvcDeployment) {
    $windeployArguments += "--no-compiler-runtime"
} else {
    $windeployArguments += "--compiler-runtime"
}
$windeployArguments += @($targetExePath, $targetBridgeExePath)
Invoke-CheckedProcess -FilePath $windeployQt -Arguments $windeployArguments

# Deploy the reviewed plugin after windeployqt; never fall back to an arbitrary
# development PATH copy. Corresponding sources/notices travel with ZIP and installer.
$webpNotices = Join-Path $packageRoot "third_party\webp"
New-Item -ItemType Directory -Path $webpNotices,(Join-Path $packageRoot "imageformats") -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $webpBundle "qwebp.dll") -Destination (Join-Path $packageRoot "imageformats\qwebp.dll") -Force
Get-ChildItem -LiteralPath $webpBundle -File | Where-Object Name -ne "qwebp.dll" |
    Copy-Item -Destination $webpNotices -Force
$webpExample = Join-Path $packageRoot "examples\character_webp"
New-Item -ItemType Directory -Path $webpExample -Force | Out-Null
foreach ($file in @("README.md", "character.json", "runtime-probe.webp")) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "examples\character_webp\$file") -Destination $webpExample -Force
}
$videoExample = Join-Path $packageRoot "examples\character_alpha_video"
New-Item -ItemType Directory -Path $videoExample -Force | Out-Null
foreach ($file in @("README.md", "character.json", "runtime-probe.mp4")) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "examples\character_alpha_video\$file") -Destination $videoExample -Force
}
Copy-Item -LiteralPath (Join-Path $repoRoot "examples\character_effects") -Destination (Join-Path $packageRoot "examples\character_effects") -Recurse
New-Item -ItemType Directory -Path (Join-Path $packageRoot "docs") -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot "docs\character-package-v2.md") -Destination (Join-Path $packageRoot "docs") -Force
Copy-Item -LiteralPath (Join-Path $repoRoot "docs\character-studio.md") -Destination (Join-Path $packageRoot "docs") -Force

if ($isMsvcDeployment) {
    Write-Host "Deploying app-local MSVC runtime: $resolvedMsvcRuntimeDir"
    Copy-MsvcRuntime -SourceDirectory $resolvedMsvcRuntimeDir -DestinationDirectory $packageRoot
}

# Fail before producing a ZIP/installer if a runtime, plugin or Agent asset is missing.
& $verifyScriptPath -PackageRoot $packageRoot -ReportPath (Join-Path $packageRoot "deployment-report.json")

$manifestPath = Join-Path $packageRoot "package_manifest.txt"
@(
    "QtDesktopPet release package",
    "Version: $Version",
    "CreatedUtc: $((Get-Date).ToUniversalTime().ToString("o"))",
    "SourceExeSHA256: $((Get-FileHash -LiteralPath $resolvedExePath -Algorithm SHA256).Hash)",
    "SourceBridgeExeSHA256: $((Get-FileHash -LiteralPath $resolvedBridgeExePath -Algorithm SHA256).Hash)",
    "Includes: QtDesktopPet.exe, QtDesktopPetBridge.exe, Qt/compiler runtime, Qt plugins including qwebp and video backends, resources, examples/agent_bridge, examples/character_webp, examples/character_alpha_video, third_party/webp notices/sources/recipe, offline user guide, deployment-report.json, LICENSE, README.md",
    "Excludes: developer tools, internal docs, reports, build, dist, .git, personal Agent configurations",
    "External Agent clients are installed separately; the DSH plugin uses its host's Node runtime.",
    "Verification: app-local normal/delay imports, x64 binaries, one Qt version, required plugins/assets, WebP dependency hashes and actual EXE WebP/H.264 alpha-video decoding plus bridge startup with empty PATH.",
    "Clean-machine GUI/HTTPS/audio and installer acceptance remain separate release checks."
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
