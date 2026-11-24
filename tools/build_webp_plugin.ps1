param(
    [Parameter(Mandatory = $true)][string]$QtKit,
    [Parameter(Mandatory = $true)][string]$QtImageFormatsSource,
    [string]$OutputRoot = "build/webp-runtime",
    [string]$LibwebpArchive = "",
    [string]$CMakePath = "cmake",
    [string]$NinjaPath = "ninja"
)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
function Run-CMake([string[]]$Arguments) {
    & $CMakePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "CMake failed: $Arguments" }
}
$kit = (Resolve-Path -LiteralPath $QtKit).Path.Replace('\', '/')
$qtSource = (Resolve-Path -LiteralPath $QtImageFormatsSource).Path
$root = [IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $root -Force | Out-Null
$versionText = Get-Content -LiteralPath (Join-Path $kit "mkspecs/qconfig.pri") -Raw
$qtVersion = [regex]::Match($versionText, '(?m)^QT_VERSION\s*=\s*([0-9.]+)').Groups[1].Value
$sourceVersion = [regex]::Match((Get-Content -LiteralPath (Join-Path $qtSource ".cmake.conf") -Raw),
    'QT_REPO_MODULE_VERSION\s+"([0-9.]+)"').Groups[1].Value
if (-not $qtVersion -or $sourceVersion -ne $qtVersion) { throw "Qt Kit and QtImageFormats source versions must match exactly." }

# Pinned upstream source, not an unversioned/latest download. Compiler must be
# initialized by the caller's x64 VS developer prompt. No global Kit changes.
$libVersion = "1.6.0"
$url = "https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-$libVersion.tar.gz"
$expectedHash = "E4AB7009BF0629FD11982D4C2AA83964CF244CFFBA7347ECD39019A9E38C4564"
$archive = if ($LibwebpArchive) { (Resolve-Path -LiteralPath $LibwebpArchive).Path } else { Join-Path $root "libwebp-$libVersion.tar.gz" }
if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $url -OutFile $archive }
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) { throw "libwebp source checksum mismatch." }
$libSource = Join-Path $root "libwebp-$libVersion"
if (-not (Test-Path -LiteralPath $libSource)) {
    Push-Location $root
    try { Run-CMake @("-E", "tar", "xzf", $archive) } finally { Pop-Location }
}
$prefix = (Join-Path $root "lib-prefix").Replace('\', '/')
$libBuild = Join-Path $root "lib-build"
$off = @("ANIM_UTILS", "CWEBP", "DWEBP", "GIF2WEBP", "IMG2WEBP", "VWEBP", "WEBPINFO", "WEBPMUX", "EXTRAS")
$configure = @("-S", $libSource, "-B", $libBuild, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_INSTALL_PREFIX=$prefix", "-DCMAKE_MAKE_PROGRAM=$NinjaPath", "-DBUILD_SHARED_LIBS=OFF")
foreach ($feature in $off) { $configure += "-DWEBP_BUILD_$feature=OFF" }
Run-CMake $configure
Run-CMake @("--build", $libBuild, "--parallel", "4")
Run-CMake @("--install", $libBuild)
$pluginBuild = Join-Path $root "plugin-build"
Run-CMake @("-S", $qtSource, "-B", $pluginBuild, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_PREFIX_PATH=$kit", "-DWebP_DIR=$prefix/share/WebP/cmake", "-DFEATURE_system_webp=ON",
    "-DCMAKE_INSTALL_PREFIX=$root/unused-install", "-DCMAKE_MAKE_PROGRAM=$NinjaPath",
    "-DQT_BUILD_EXAMPLES=OFF", "-DQT_BUILD_TESTS=OFF")
Run-CMake @("--build", $pluginBuild, "--target", "QWebpPlugin", "--parallel", "4")
$bundle = Join-Path $root "bundle"
New-Item -ItemType Directory -Path $bundle -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $pluginBuild "plugins/imageformats/qwebp.dll") -Destination $bundle -Force
foreach ($name in @("COPYING", "PATENTS", "AUTHORS")) {
    Copy-Item -LiteralPath (Join-Path $libSource $name) -Destination (Join-Path $bundle "libwebp-$name.txt") -Force
}
Copy-Item -LiteralPath (Join-Path $qtSource "LICENSES/LGPL-3.0-only.txt") -Destination $bundle -Force
Copy-Item -LiteralPath (Join-Path $qtSource "LICENSES/GPL-3.0-only.txt") -Destination $bundle -Force
Copy-Item -LiteralPath $archive -Destination (Join-Path $bundle "libwebp-source.tar.gz") -Force
Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $bundle "build_webp_plugin.ps1") -Force
$qtArchive = Join-Path $bundle "qtimageformats-source.zip"
if (-not (Test-Path -LiteralPath $qtArchive)) {
    Compress-Archive -LiteralPath $qtSource -DestinationPath $qtArchive -CompressionLevel Optimal
}
@"
Qt WebP runtime: QtImageFormats $qtVersion + libwebp $libVersion
Qt plugin dynamically links the application's matching Qt runtime; libwebp is
statically linked into qwebp.dll. This bundle uses LGPL-3.0 for the Qt plugin
and BSD-3-Clause for libwebp; see included license texts, copyright notices,
patent grant, and corresponding source archives. No plugin source modifications.
Build recipe: included build_webp_plugin.ps1 (also tools/ in application source).
Build: x64 MSVC Release, system_webp=ON, BUILD_SHARED_LIBS=OFF for libwebp.
Upstream libwebp source: $url
Qt source: matching Qt installer source module qtimageformats $qtVersion.
Replacing qwebp.dll with a compatible modified LGPL build is permitted.
Application MIT license does not replace these third-party licenses.
"@ | Set-Content -LiteralPath (Join-Path $bundle "NOTICE.txt") -Encoding UTF8
$hashes = [ordered]@{}
foreach ($file in Get-ChildItem -LiteralPath $bundle -File | Where-Object Name -ne "dependency.json") {
    $hashes[$file.Name] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
}
[ordered]@{ SchemaVersion=1; QtVersion=$qtVersion; LibwebpVersion=$libVersion;
    LibwebpSourceSHA256=$expectedHash; Files=$hashes } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $bundle "dependency.json") -Encoding UTF8
Write-Output "WebP runtime bundle: $bundle"
