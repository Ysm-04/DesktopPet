param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$EncoderDirectory,
    [Parameter(Mandatory)][string]$WindeployQtPath,
    [Parameter(Mandatory)][string]$MsvcRuntimeDir,
    [Parameter(Mandatory)][string]$WebpRuntimeDir,
    [Parameter(Mandatory)][string]$OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$encoder = (Resolve-Path -LiteralPath $EncoderDirectory).Path
$webp = (Resolve-Path -LiteralPath $WebpRuntimeDir).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Use a new author-kit output directory.' }
$lockFile = Join-Path $encoder 'dependency-lock.json'
if (-not (Test-Path -LiteralPath $lockFile)) { throw 'Missing fixed encoder dependency lock.' }
$lock = Get-Content -LiteralPath $lockFile -Raw | ConvertFrom-Json
foreach ($entry in $lock.files.PSObject.Properties) {
    $path = [IO.Path]::GetFullPath((Join-Path $encoder $entry.Name))
    if (-not $path.StartsWith($encoder + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe encoder material path.' }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.Value) { throw "Encoder material hash mismatch: $($entry.Name)" }
}
foreach ($required in @('ffmpeg.exe','ffprobe.exe','build-encoder.sh','sources.json','licenses','sources')) {
    if (-not (Test-Path -LiteralPath (Join-Path $encoder $required))) { throw "Missing encoder distribution material: $required" }
}
New-Item -ItemType Directory -Path $output | Out-Null
foreach ($exe in @('QtDesktopPetCharacterStudio.exe','QtDesktopPetCharacterTool.exe')) {
    Copy-Item -LiteralPath (Join-Path $build $exe) -Destination $output
    & $WindeployQtPath --no-translations --no-compiler-runtime (Join-Path $output $exe)
    if ($LASTEXITCODE -ne 0) { throw "Qt deployment failed: $exe" }
}
foreach ($name in @('msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll','vcruntime140.dll','vcruntime140_1.dll','concrt140.dll')) {
    Copy-Item -LiteralPath (Join-Path $MsvcRuntimeDir $name) -Destination $output
}
Copy-Item -LiteralPath $encoder -Destination (Join-Path $output 'encoder') -Recurse
Copy-Item -LiteralPath (Join-Path $webp 'qwebp.dll') -Destination (Join-Path $output 'imageformats/qwebp.dll') -Force
Copy-Item -LiteralPath $webp -Destination (Join-Path $output 'licenses-webp') -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'docs/character-studio.md') -Destination (Join-Path $output 'README-作者工具.md')
Copy-Item -LiteralPath (Join-Path $repo 'docs/character-package-v2.md') -Destination $output
Copy-Item -LiteralPath (Join-Path $repo 'examples/character_effects') -Destination (Join-Path $output 'examples/character_effects') -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'examples/character_tool') -Destination (Join-Path $output 'examples/character_tool') -Recurse
@("[Paths]", "Prefix=.", "Plugins=.") | Set-Content -LiteralPath (Join-Path $output 'qt.conf') -Encoding ASCII
Copy-Item -LiteralPath (Join-Path $repo 'LICENSE') -Destination $output
Copy-Item -LiteralPath (Join-Path $repo 'CHANGELOG.md') -Destination $output
$savedPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    & (Join-Path $output 'QtDesktopPetCharacterTool.exe') self-check
    if ($LASTEXITCODE -ne 0) { throw 'Author kit failed with an empty development PATH.' }
} finally { $env:PATH = $savedPath }
$manifest = @{}
Get-ChildItem -LiteralPath $output -File -Recurse | ForEach-Object {
    $manifest[$_.FullName.Substring($output.Length + 1).Replace('\','/')] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'package-hashes.json') -Encoding utf8
Compress-Archive -LiteralPath $output -DestinationPath ($output + '.zip') -CompressionLevel Optimal
Write-Output "Author kit created and dependency-checked: $output.zip"
