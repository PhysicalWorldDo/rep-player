param(
    [string]$Version = '1.0.0',
    [Parameter(Mandatory = $true)][string]$FFmpegDirectory
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot
$packageName = "rep-player-v$Version-windows-x64"
$distRoot = Join-Path $taskRoot 'dist'
$packageRoot = Join-Path $distRoot $packageName
if (Test-Path -LiteralPath $packageRoot) { throw "Package directory already exists: $packageRoot. Use a fresh version or preserve it under a different name before rerunning." }
New-Item -ItemType Directory -Path (Join-Path $packageRoot 'build') -Force | Out-Null
$files = @('rep_player.exe', 'rep_export.exe', 'rep_gpu.exe', 'rep_validate.exe', 'rep_resources.exe', 'rep_catalog.exe')
foreach ($name in $files) {
    Copy-Item -LiteralPath (Join-Path $taskRoot "build\$name") -Destination (Join-Path $packageRoot "build\$name")
}
# Pass a directory containing the chosen FFmpeg binary and all of its runtime DLLs.
Copy-Item -LiteralPath (Join-Path $FFmpegDirectory 'ffmpeg.exe') -Destination (Join-Path $packageRoot 'build\ffmpeg.exe')
Get-ChildItem -LiteralPath $FFmpegDirectory -File -Filter '*.dll' | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $packageRoot 'build')
}
foreach ($name in @('Start.cmd', 'README.md', 'README.txt', 'BUILDING.md', 'RELEASE_NOTES.md', 'THIRD_PARTY.txt')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot $name) -Destination (Join-Path $packageRoot $name)
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'licenses') -Destination (Join-Path $packageRoot 'licenses') -Recurse
New-Item -ItemType Directory -Path (Join-Path $packageRoot 'assets\shaders') -Force | Out-Null
$shaderManifest = Get-Content -LiteralPath (Join-Path $taskRoot 'assets\shaders\manifest.json') -Raw | ConvertFrom-Json
$shaderNames = $shaderManifest | ForEach-Object { $_.vs; $_.ps } | Sort-Object -Unique
foreach ($name in $shaderNames) {
    Copy-Item -LiteralPath (Join-Path $taskRoot "assets\shaders\$name") -Destination (Join-Path $packageRoot 'assets\shaders')
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'assets\shaders\manifest.json') -Destination (Join-Path $packageRoot 'assets\shaders')
Copy-Item -LiteralPath (Join-Path $taskRoot 'assets\icon') -Destination (Join-Path $packageRoot 'assets\icon') -Recurse
New-Item -ItemType Directory -Path (Join-Path $packageRoot 'ui_design\data') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'ui_design\data\skill-name-map.json') -Destination (Join-Path $packageRoot 'ui_design\data')
$manifest = Get-ChildItem -LiteralPath $packageRoot -File -Recurse | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName.Substring($packageRoot.Length + 1).Replace('\', '/'); bytes = $_.Length }
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $packageRoot 'PACKAGE_FILES.json') -Encoding utf8
$zipPath = Join-Path $distRoot "$packageName.zip"
if (Test-Path -LiteralPath $zipPath) { throw "Archive already exists: $zipPath" }
Compress-Archive -LiteralPath $packageRoot -DestinationPath $zipPath -CompressionLevel Optimal
Write-Output $zipPath
