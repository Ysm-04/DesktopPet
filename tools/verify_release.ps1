param(
    [Parameter(Mandatory = $true)]
    [string]$PackageRoot,
    [string]$ReportPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Inspect PE import tables without dumpbin, Python, a compiler or PATH lookup.
function Get-PeDependencies {
    param([string]$FilePath)

    $bytes = [System.IO.File]::ReadAllBytes($FilePath)
    if ($bytes.Length -lt 64 -or [BitConverter]::ToUInt16($bytes, 0) -ne 0x5a4d) {
        throw "Not a Windows executable: $FilePath"
    }
    $pe = [BitConverter]::ToInt32($bytes, 60)
    if ($pe -lt 0 -or $pe + 24 -gt $bytes.Length -or
        [BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550) {
        throw "Invalid PE header: $FilePath"
    }
    if ([BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664) {
        throw "The release requires x64 binaries: $FilePath"
    }
    $optional = $pe + 24
    $optionalSize = [BitConverter]::ToUInt16($bytes, $pe + 20)
    if ($optional + $optionalSize -gt $bytes.Length -or $optionalSize -lt 224 -or
        [BitConverter]::ToUInt16($bytes, $optional) -ne 0x20b) {
        throw "Invalid PE32+ optional header: $FilePath"
    }
    $imageBase = [BitConverter]::ToUInt64($bytes, $optional + 24)
    $sections = @()
    $count = [BitConverter]::ToUInt16($bytes, $pe + 6)
    for ($index = 0; $index -lt $count; ++$index) {
        $position = $optional + $optionalSize + $index * 40
        if ($position + 40 -gt $bytes.Length) { throw "Invalid PE sections: $FilePath" }
        $sections += @{
            Address = [BitConverter]::ToUInt32($bytes, $position + 12)
            Size = [BitConverter]::ToUInt32($bytes, $position + 16)
            Offset = [BitConverter]::ToUInt32($bytes, $position + 20)
        }
    }
    function Convert-RvaToOffset([long]$Address) {
        foreach ($section in $sections) {
            if ($Address -ge $section.Address -and $Address -lt ([long]$section.Address + $section.Size)) {
                $offset = [long]$section.Offset + $Address - $section.Address
                if ($offset -lt $bytes.Length) { return [int]$offset }
            }
        }
        throw "Invalid PE import address in: $FilePath"
    }
    $imports = @()
    # Normal imports and delay imports both contribute runtime requirements.
    foreach ($directory in @(@{Index=1; Stride=20; NameOffset=12}, @{Index=13; Stride=32; NameOffset=4})) {
        $table = $optional + 112 + 8 * $directory.Index
        $address = [BitConverter]::ToUInt32($bytes, $table)
        $size = [BitConverter]::ToUInt32($bytes, $table + 4)
        if ($address -eq 0) { continue }
        $start = Convert-RvaToOffset $address
        $limit = [Math]::Min(4096, [Math]::Floor($size / $directory.Stride) + 1)
        for ($index = 0; $index -lt $limit; ++$index) {
            $position = $start + $index * $directory.Stride
            if ($position + $directory.Stride -gt $bytes.Length) { throw "Truncated import table: $FilePath" }
            $nameAddress = [long][BitConverter]::ToUInt32($bytes, $position + $directory.NameOffset)
            if ($nameAddress -eq 0) { break }
            if ($directory.Index -eq 13 -and ([BitConverter]::ToUInt32($bytes, $position) -band 1) -eq 0) {
                $nameAddress -= $imageBase
            }
            $namePosition = Convert-RvaToOffset $nameAddress
            $end = $namePosition
            while ($end -lt $bytes.Length -and $bytes[$end] -ne 0 -and $end - $namePosition -le 260) { ++$end }
            if ($end -eq $bytes.Length -or $end - $namePosition -gt 260) { throw "Invalid DLL name: $FilePath" }
            $imports += [System.Text.Encoding]::ASCII.GetString($bytes, $namePosition, $end - $namePosition).ToLowerInvariant()
        }
    }
    return @($imports | Sort-Object -Unique)
}

$root = [System.IO.Path]::GetFullPath($PackageRoot).TrimEnd('\', '/')
if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "Package directory not found: $root" }
$requiredFiles = @(
    "QtDesktopPet.exe", "QtDesktopPetBridge.exe",
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Network.dll", "Qt6Multimedia.dll",
    "platforms\qwindows.dll", "tls\qschannelbackend.dll", "qt.conf",
    "resources\characters\shiroko\character.json", "resources\icons\app_icon.ico",
    "resources\sounds\reminders\reminder_default.wav", "LICENSE", "README.md", "使用说明.md", "CHANGELOG.md",
    "docs\character-studio.md", "docs\character-package-v2.md", "examples\character_effects\character.json",
    "examples\agent_bridge\README.md", "examples\agent_bridge\codex.hooks.json",
    "examples\agent_bridge\antigravity.hooks.json", "examples\agent_bridge\dsh-cordis-plugin.js",
    "examples\agent_bridge\test_bridge.ps1", "imageformats\qwebp.dll",
    "third_party\webp\dependency.json", "third_party\webp\NOTICE.txt",
    "third_party\webp\LGPL-3.0-only.txt", "third_party\webp\GPL-3.0-only.txt",
    "third_party\webp\libwebp-COPYING.txt", "third_party\webp\libwebp-PATENTS.txt",
    "third_party\webp\libwebp-AUTHORS.txt", "third_party\webp\libwebp-source.tar.gz",
    "third_party\webp\qtimageformats-source.zip",
    "third_party\webp\build_webp_plugin.ps1",
    "examples\character_webp\README.md", "examples\character_webp\character.json",
    "examples\character_webp\runtime-probe.webp",
    "examples\character_alpha_video\README.md", "examples\character_alpha_video\character.json",
    "examples\character_alpha_video\runtime-probe.mp4"
)
foreach ($relative in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf)) {
        throw "Required release file is missing: $relative"
    }
}
if (@(Get-ChildItem -LiteralPath (Join-Path $root "multimedia") -Filter "*.dll" -File -ErrorAction SilentlyContinue).Count -eq 0) {
    throw "No Qt Multimedia backend was deployed."
}

# These are OS components on supported Windows 10/11 installations, never
# arbitrary DLLs found on the packager's PATH or in its development environment.
$windowsDlls = @(
    "advapi32.dll", "authz.dll", "avrt.dll", "bcrypt.dll", "bcryptprimitives.dll",
    "cfgmgr32.dll", "combase.dll", "comctl32.dll", "comdlg32.dll", "crypt32.dll",
    "cryptbase.dll", "cryptnet.dll", "cryptui.dll", "d2d1.dll", "d3d9.dll",
    "d3d11.dll", "d3d12.dll", "d3dcompiler_47.dll", "dbghelp.dll", "dhcpcsvc.dll",
    "dnsapi.dll", "dsound.dll", "dwmapi.dll", "dwrite.dll", "dxgi.dll", "dxva2.dll",
    "evr.dll", "gdi32.dll", "glu32.dll", "hid.dll", "imagehlp.dll", "imm32.dll",
    "iphlpapi.dll", "kernel32.dll", "kernelbase.dll", "ksuser.dll", "mf.dll",
    "mfplat.dll", "mfreadwrite.dll", "mfuuid.dll", "mmdevapi.dll", "mpr.dll",
    "msacm32.dll", "msimg32.dll", "msvcrt.dll", "ncrypt.dll", "netapi32.dll",
    "normaliz.dll", "ntdll.dll", "ole32.dll", "oleacc.dll", "oleaut32.dll",
    "opengl32.dll", "powrprof.dll", "propsys.dll", "psapi.dll", "rpcrt4.dll",
    "secur32.dll", "setupapi.dll", "shell32.dll", "shcore.dll", "shlwapi.dll",
    "strmiids.dll", "ucrtbase.dll", "urlmon.dll", "user32.dll", "userenv.dll",
    "usp10.dll", "uxtheme.dll", "version.dll", "windowscodecs.dll", "winhttp.dll",
    "wininet.dll", "winmm.dll", "winspool.drv", "wintrust.dll", "wldap32.dll",
    "ws2_32.dll", "wtsapi32.dll"
)
$binaries = @(Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object { $_.Extension -in @(".exe", ".dll") })
$checks = @()
$qtVersions = @()
foreach ($binary in $binaries) {
    $relative = $binary.FullName.Substring($root.Length + 1)
    $imports = @(Get-PeDependencies -FilePath $binary.FullName)
    if ($binary.Name -like "Qt6*.dll") { $qtVersions += $binary.VersionInfo.FileVersion }
    foreach ($dependency in $imports) {
        $local = Join-Path $binary.DirectoryName $dependency
        $shared = Join-Path $root $dependency
        if ((Test-Path -LiteralPath $local -PathType Leaf) -or (Test-Path -LiteralPath $shared -PathType Leaf)) { continue }
        if ($dependency -match '^(api-ms-win-|ext-ms-win-)') { continue }
        if ($dependency -in $windowsDlls -and
            (Test-Path -LiteralPath (Join-Path ([Environment]::SystemDirectory) $dependency) -PathType Leaf)) { continue }
        throw "Missing app-local dependency '$dependency', imported by '$relative'. PATH and globally installed VC runtimes are intentionally ignored."
    }
    $checks += [pscustomobject]@{File=$relative; SHA256=(Get-FileHash -LiteralPath $binary.FullName -Algorithm SHA256).Hash; Imports=$imports}
}
if (@($qtVersions | Sort-Object -Unique).Count -ne 1) {
    throw "The package contains mixed Qt runtime versions."
}
$webpManifest = Get-Content -LiteralPath (Join-Path $root "third_party\webp\dependency.json") -Raw | ConvertFrom-Json
if ($webpManifest.SchemaVersion -ne 1 -or [version]$webpManifest.LibwebpVersion -lt [version]"1.6.0" -or
    -not (@($qtVersions)[0].StartsWith("$($webpManifest.QtVersion)."))) {
    throw "WebP runtime metadata version is incompatible or below the pinned libwebp baseline."
}
foreach ($property in $webpManifest.Files.PSObject.Properties) {
    if ([IO.Path]::GetFileName($property.Name) -ne $property.Name -or $property.Name.Contains('\') -or $property.Name.Contains('/')) {
        throw "Invalid WebP bundle manifest path."
    }
    $file = if ($property.Name -eq "qwebp.dll") { Join-Path $root "imageformats\qwebp.dll" } else { Join-Path $root "third_party\webp\$($property.Name)" }
    if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or
        (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $property.Value) {
        throw "WebP runtime bundle checksum mismatch: $($property.Name)"
    }
}
if ($webpManifest.Files.PSObject.Properties.Name -notcontains "qwebp.dll") { throw "WebP manifest lacks plugin hash." }
if ((Get-FileHash -LiteralPath (Join-Path $root "third_party\webp\libwebp-source.tar.gz")).Hash -ne $webpManifest.LibwebpSourceSHA256) {
    throw "WebP source archive checksum mismatch."
}

# A fresh bridge process must start using only packaged DLLs and Windows.
# --help touches neither user configuration nor the running pet.
$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = Join-Path $root "QtDesktopPetBridge.exe"
$startInfo.Arguments = "--help"
$startInfo.WorkingDirectory = $root
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$startInfo.EnvironmentVariables.Clear()
foreach ($name in @("SystemRoot", "WINDIR", "TEMP", "TMP")) {
    $value = [Environment]::GetEnvironmentVariable($name)
    if ($value) { $startInfo.EnvironmentVariables[$name] = $value }
}
$startInfo.EnvironmentVariables["PATH"] = ""
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $startInfo
try {
    [void]$process.Start()
    if (-not $process.WaitForExit(5000)) {
        $process.Kill()
        throw "Packaged bridge did not exit within 5 seconds."
    }
    $output = $process.StandardOutput.ReadToEnd()
    $errorOutput = $process.StandardError.ReadToEnd()
    if ($process.ExitCode -ne 0 -or $output -notmatch '--packet-stdin') {
        throw "Packaged bridge failed without development PATH. Exit=$($process.ExitCode); $errorOutput"
    }
} finally {
    $process.Dispose()
}

# The actual GUI executable must decode WebP using app-local plugins, before it
# touches single-instance IPC or personal configuration.
$startInfo.FileName = Join-Path $root "QtDesktopPet.exe"
$startInfo.Arguments = "--check-webp-runtime"
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $startInfo
try {
    [void]$process.Start()
    if (-not $process.WaitForExit(10000)) {
        $process.Kill()
        throw "Packaged WebP runtime self-check timed out."
    }
    $output = $process.StandardOutput.ReadToEnd()
    $errorOutput = $process.StandardError.ReadToEnd()
    if ($process.ExitCode -ne 0 -or $output -notmatch "WEBP_RUNTIME_PASS") {
        throw "Packaged WebP decoding failed without PATH. Exit=$($process.ExitCode); $errorOutput $output"
    }
} finally { $process.Dispose() }

$startInfo.Arguments = "--check-video-runtime"
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $startInfo
try {
    [void]$process.Start()
    $videoOutputTask = $process.StandardOutput.ReadToEndAsync()
    $videoErrorTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(12000)) {
        $process.Kill()
        throw "Packaged video runtime self-check timed out."
    }
    $output = $videoOutputTask.GetAwaiter().GetResult()
    $errorOutput = $videoErrorTask.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0 -or $output -notmatch "VIDEO_RUNTIME_PASS") {
        throw "Packaged video decoding failed without PATH. Exit=$($process.ExitCode); $errorOutput $output"
    }
} finally { $process.Dispose() }

$report = [ordered]@{
    CreatedUtc = (Get-Date).ToUniversalTime().ToString("o")
    Result = "PASS"
    Architecture = "x64"
    QtVersion = @($qtVersions | Sort-Object -Unique)[0]
    CheckedBinaries = $binaries.Count
    BridgeWithoutPath = "PASS"
    WebpWithoutPath = "PASS"
    VideoWithoutPath = "PASS"
    WebpDependencyVersion = $webpManifest.LibwebpVersion
    DependencyPolicy = "Package-local DLLs or explicit Windows OS components; normal and delay imports checked; PATH ignored."
    Limitations = "Does not replace clean Windows installation, GUI, sound, HTTPS or actual Agent client acceptance tests."
    Binaries = $checks
}
if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
}
Write-Host "PASS: $($binaries.Count) x64 binaries, Qt $($report.QtVersion), import closure, bridge and WebP/video decoding without PATH."
