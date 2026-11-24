param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [switch]$Force
)

$ErrorActionPreference = "Stop"
# Public user documentation only. Internal plans and acceptance records stay private.
$publicDocFiles = @("character-package-v2.md", "character-studio.md")

function Get-FullPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }

    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Is-SubPath([string]$Candidate, [string]$Parent) {
    $candidateFull = [System.IO.Path]::GetFullPath($Candidate).TrimEnd('\', '/')
    $parentFull = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
    return $candidateFull.Equals($parentFull, [System.StringComparison]::OrdinalIgnoreCase) -or
        $candidateFull.StartsWith($parentFull + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)
}

function Should-ExcludeDirectory([string]$RelativePath) {
    $segments = @($RelativePath -split '[\\/]' | Where-Object { $_ -ne "" })
    if ($segments.Count -eq 0) {
        return $false
    }

    $first = $segments[0].ToLowerInvariant()
    if ($first -in @(
        ".git",
        ".vs",
        ".vscode",
        ".idea",
        ".agents",
        ".codex",
        "build",
        "dist",
        "output",
        "release_packages",
        "docs",
        "internal",
        "reports",
        "config",
        "logs"
    )) {
        return $true
    }

    return $first.StartsWith("cmake-build-")
}

function Should-ExcludeFile([string]$RelativePath) {
    $fileName = [System.IO.Path]::GetFileName($RelativePath).ToLowerInvariant()
    if ($fileName -in @(
        "readme.dev.md"
    )) {
        return $true
    }

    foreach ($pattern in @(
        "*.user",
        "*.suo",
        "*.vc.db",
        "*.vc.opendb",
        "*.autosave",
        "*.broken.json",
        "*.exe",
        "*.dll",
        "*.pdb",
        "*.ilk",
        "*.obj",
        "*.o",
        "*.a",
        "*.lib"
    )) {
        if ($fileName -like $pattern) {
            return $true
        }
    }

    return $false
}

function Copy-PublicTree([string]$SourceDir, [string]$DestinationDir, [string]$RelativeDir) {
    foreach ($item in Get-ChildItem -LiteralPath $SourceDir -Force) {
        $relativePath = if ($RelativeDir) {
            Join-Path $RelativeDir $item.Name
        } else {
            $item.Name
        }

        if ($item.PSIsContainer) {
            if (Should-ExcludeDirectory $relativePath) {
                continue
            }

            $targetDir = Join-Path $DestinationDir $item.Name
            New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
            Copy-PublicTree -SourceDir $item.FullName -DestinationDir $targetDir -RelativeDir $relativePath
            continue
        }

        if (Should-ExcludeFile $relativePath) {
            continue
        }

        Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $DestinationDir $item.Name) -Force
    }
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outputPath = Get-FullPath $OutputDir

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    throw "OutputDir is required."
}

if (Is-SubPath -Candidate $outputPath -Parent $repoRoot) {
    throw "OutputDir must be outside the repository to avoid recursive export: $outputPath"
}

if (Test-Path -LiteralPath $outputPath) {
    $hasContent = (Get-ChildItem -LiteralPath $outputPath -Force | Select-Object -First 1) -ne $null
    if ($hasContent -and -not $Force) {
        throw "OutputDir already exists and is not empty. Pass -Force to replace it: $outputPath"
    }

    if ($Force) {
        Remove-Item -LiteralPath $outputPath -Recurse -Force
    }
}

New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
Copy-PublicTree -SourceDir $repoRoot -DestinationDir $outputPath -RelativeDir ""
New-Item -ItemType Directory -Path (Join-Path $outputPath "docs") -Force | Out-Null
foreach ($name in $publicDocFiles) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "docs/$name") -Destination (Join-Path $outputPath "docs/$name")
}

foreach ($forbiddenRoot in @("internal", "output", "reports", "build", "dist", "release_packages", ".git", ".agents", "config", "logs")) {
    if (Test-Path -LiteralPath (Join-Path $outputPath $forbiddenRoot)) {
        throw "Public export isolation failed; forbidden directory was copied: $forbiddenRoot"
    }
}
foreach ($item in Get-ChildItem -LiteralPath (Join-Path $outputPath "docs") -Force -Recurse) {
    if ($item.PSIsContainer -or $item.Name -notin $publicDocFiles) {
        throw "Public export isolation failed; unreviewed documentation was copied: $($item.FullName)"
    }
}
if (Test-Path -LiteralPath (Join-Path $outputPath "README.dev.md")) {
    throw "Public export isolation failed; README.dev.md was copied."
}

Write-Host "GitHub export prepared:"
Write-Host "  $outputPath"
Write-Host ""
Write-Host "Next steps:"
Write-Host "  cd $outputPath"
Write-Host "  git init"
Write-Host "  git remote add origin https://github.com/Ysm-04/DesktopPet.git"
Write-Host "  git add ."
Write-Host "  git commit -m `"Publish GitHub version`""
Write-Host "  git push -u origin main"
