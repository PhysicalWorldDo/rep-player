param(
    [string]$Version = '1.0.2',
    [Parameter(Mandatory = $true)][string]$FFmpegDirectory
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot
$packageName = "rep-player-v$Version-windows-x64"
$distRoot = Join-Path $taskRoot 'dist'
$packageRoot = Join-Path $distRoot $packageName
if (Test-Path -LiteralPath $packageRoot) { throw "Package directory already exists: $packageRoot. Use a fresh version or preserve it under a different name before rerunning." }
$zipPath = Join-Path $distRoot "$packageName.zip"
if (Test-Path -LiteralPath $zipPath) { throw "Archive already exists: $zipPath" }
New-Item -ItemType Directory -Path (Join-Path $packageRoot 'resources\licenses') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'build\rep_player.exe') -Destination (Join-Path $packageRoot 'rep_player.exe')
# The release uses the selected static FFmpeg build; no external runtime DLLs.
Copy-Item -LiteralPath (Join-Path $FFmpegDirectory 'ffmpeg.exe') -Destination (Join-Path $packageRoot 'resources\ffmpeg.exe')
foreach ($name in @('FREETYPE_FTL.txt', 'FREETYPE_LICENSE.txt', 'FREETYPE_BDF_NOTICE.txt', 'FREETYPE_PCF_NOTICE.txt', 'ZLIB_1.2.11.txt', 'FFMPEG_GPLv3.txt', 'FFMPEG_BUILD.txt')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot "licenses\$name") -Destination (Join-Path $packageRoot 'resources\licenses')
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'licenses\LLVM_MINGW') -Destination (Join-Path $packageRoot 'resources\licenses\LLVM_MINGW') -Recurse
Copy-Item -LiteralPath (Join-Path $taskRoot 'THIRD_PARTY.txt') -Destination (Join-Path $packageRoot 'resources\licenses\THIRD_PARTY.txt')
$manifest = Get-ChildItem -LiteralPath $packageRoot -File -Recurse | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName.Substring($packageRoot.Length + 1).Replace('\', '/'); bytes = $_.Length }
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $distRoot "$packageName.files.json") -Encoding utf8
Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $zipPath -CompressionLevel Optimal
Write-Output $zipPath
