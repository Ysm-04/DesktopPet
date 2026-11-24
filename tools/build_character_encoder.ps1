param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [string]$GitBash = 'D:\Git\bin\bash.exe',
    [string]$MingwDirectory = 'D:\QT\Tools\mingw1120_64',
    [string]$CmakeDirectory = 'D:\QT\Tools\CMake_64\bin',
    [string]$NinjaDirectory = 'D:\QT\Tools\Ninja'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$build = [IO.Path]::GetFullPath($BuildDirectory)
if ($build -match '[^\x00-\x7F]' -or $build -match '\s') { throw 'Use an ASCII build path without spaces.' }
$sources = Join-Path $build 'sources'
New-Item -ItemType Directory -Path $sources -Force | Out-Null
$sourceLockPath = Join-Path $PSScriptRoot 'author/encoder-sources.json'
$sourceLock = Get-Content -LiteralPath $sourceLockPath -Raw | ConvertFrom-Json
foreach ($entry in $sourceLock) {
    $target = Join-Path $sources $entry.file
    if (-not (Test-Path -LiteralPath $target)) {
        & curl.exe --fail --location --retry 2 --connect-timeout 20 --max-time 240 --output ($target + '.part') $entry.url
        if ($LASTEXITCODE -ne 0) { throw "Source download failed: $($entry.file)" }
        Move-Item -LiteralPath ($target + '.part') -Destination $target
    }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Source hash mismatch: $($entry.file)" }
}
Copy-Item -LiteralPath $sourceLockPath -Destination (Join-Path $sources 'sources.json') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'author/build-encoder.sh') -Destination $build -Force
New-Item -ItemType Directory -Path (Join-Path $build 'dev-bin'),(Join-Path $build 'compiler') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $MingwDirectory 'bin/mingw32-make.exe') -Destination (Join-Path $build 'dev-bin/make.exe') -Force
$helperRoot = Get-ChildItem -LiteralPath (Join-Path $MingwDirectory 'libexec/gcc/x86_64-w64-mingw32') -Directory | Select-Object -First 1
foreach ($exe in @('cc1.exe','cc1plus.exe','collect2.exe')) {
    Copy-Item -LiteralPath (Join-Path $helperRoot.FullName $exe) -Destination (Join-Path $build 'compiler') -Force
}
Copy-Item -LiteralPath (Join-Path $MingwDirectory 'bin/libwinpthread-1.dll') -Destination (Join-Path $build 'compiler') -Force
$oldPath = $env:PATH
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
try {
    $env:PATH = "$MingwDirectory\bin;$CmakeDirectory;$NinjaDirectory;$(Split-Path -Parent $GitBash)\..\usr\bin;$oldPath"
    $env:TEMP = $build
    $env:TMP = $build
    Push-Location -LiteralPath $build
    try { & $GitBash './build-encoder.sh' } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw 'Encoder build failed.' }
} finally { $env:PATH = $oldPath; $env:TEMP = $oldTemp; $env:TMP = $oldTmp }
$bundle = Join-Path $build 'bundle'
foreach ($entry in $sourceLock) {
    if ((Get-FileHash -LiteralPath (Join-Path $bundle "sources/$($entry.file)") -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Packaged source hash mismatch: $($entry.file)"
    }
}
$files = @{}
Get-ChildItem -LiteralPath $bundle -File -Recurse | Where-Object Name -ne 'dependency-lock.json' | ForEach-Object {
    $files[$_.FullName.Substring($bundle.Length + 1).Replace('\','/')] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
}
@{schemaVersion=1;ffmpegVersion='7.1.5';files=$files} | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $bundle 'dependency-lock.json') -Encoding utf8
Write-Output "Fixed, source-complete encoder bundle: $bundle"
