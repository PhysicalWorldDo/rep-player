$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$compiler = Join-Path $taskRoot 'toolchain\llvm-mingw-20260616-ucrt-x86_64\bin\clang++.exe'
$resourceCompiler = Join-Path (Split-Path $compiler) 'llvm-windres.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw 'Run tools\setup-dependencies.ps1 first to prepare the pinned compiler.' }
$buildPath = Join-Path $taskRoot 'build'
New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
& (Join-Path $taskRoot 'tools\generate-app-resources.ps1')
$options = @('-std=c++20','-O2','-Wall','-Wextra','-DNOMINMAX','-DUNICODE','-D_UNICODE','-municode','-static','-I',(Join-Path $taskRoot 'vendor\zlib'),'-I',(Join-Path $taskRoot 'vendor\freetype\include'))
$sources = @('protocol','resources','gpu','bindings','fonts','movies','engine','catalog','export','ui_playback','validate','resource_validate','gpu_validate','catalog_validate','export_validate','app')
$headerTime = (Get-ChildItem -LiteralPath (Join-Path $taskRoot 'src') -Filter '*.hpp' | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
foreach ($source in $sources) {
    $sourcePath = Join-Path $taskRoot "src\$source.cpp"
    $objectPath = Join-Path $buildPath "$source.o"
    if (!(Test-Path -LiteralPath $objectPath) -or (Get-Item -LiteralPath $objectPath).LastWriteTime -lt (Get-Item -LiteralPath $sourcePath).LastWriteTime -or (Get-Item -LiteralPath $objectPath).LastWriteTime -lt $headerTime) {
        & $compiler @options -c $sourcePath -o $objectPath
        if ($LASTEXITCODE -ne 0) { throw "Native build failed: $source" }
    }
}
$zlib = Join-Path $taskRoot 'vendor\zlib\libz.a'
$freetype = Join-Path $taskRoot 'vendor\freetype\libfreetype.a'
if (!(Test-Path -LiteralPath $freetype)) { throw 'Run tools\setup-dependencies.ps1 to build static FreeType.' }
$gpuSources = @('protocol','resources','gpu','bindings','fonts','movies','engine') | ForEach-Object { Join-Path $buildPath "$_.o" }
Push-Location (Join-Path $taskRoot 'src')
try {
    & $resourceCompiler -i 'app.rc' -o (Join-Path $buildPath 'app.res.o')
    if ($LASTEXITCODE -ne 0) { throw 'Windows resource compilation failed' }
} finally { Pop-Location }
& $compiler -municode -static (Join-Path $buildPath 'protocol.o') (Join-Path $buildPath 'validate.o') $zlib -o (Join-Path $buildPath 'rep_validate.exe')
if ($LASTEXITCODE -ne 0) { throw 'Protocol link failed' }
& $compiler -municode -static (Join-Path $buildPath 'protocol.o') (Join-Path $buildPath 'resources.o') (Join-Path $buildPath 'resource_validate.o') $zlib -o (Join-Path $buildPath 'rep_resources.exe')
if ($LASTEXITCODE -ne 0) { throw 'Resource link failed' }
& $compiler -municode -static @gpuSources (Join-Path $buildPath 'gpu_validate.o') $freetype $zlib -ld3d11 -ldxgi -ld3dcompiler -ldxguid -o (Join-Path $buildPath 'rep_gpu.exe')
if ($LASTEXITCODE -ne 0) { throw 'GPU link failed' }
& $compiler -municode -static (Join-Path $buildPath 'protocol.o') (Join-Path $buildPath 'catalog.o') (Join-Path $buildPath 'catalog_validate.o') $zlib -o (Join-Path $buildPath 'rep_catalog.exe')
if ($LASTEXITCODE -ne 0) { throw 'Catalog link failed' }
& $compiler -municode -static @gpuSources (Join-Path $buildPath 'export.o') (Join-Path $buildPath 'export_validate.o') $freetype $zlib -ld3d11 -ldxgi -ld3dcompiler -ldxguid -o (Join-Path $buildPath 'rep_export.exe')
if ($LASTEXITCODE -ne 0) { throw 'Export link failed' }
& $compiler -municode -mwindows -static @gpuSources (Join-Path $buildPath 'catalog.o') (Join-Path $buildPath 'export.o') (Join-Path $buildPath 'ui_playback.o') (Join-Path $buildPath 'app.o') (Join-Path $buildPath 'app.res.o') $freetype $zlib -ld3d11 -ldxgi -ld3dcompiler -ldxguid -lcomctl32 -lshell32 -lole32 -luuid -luxtheme -o (Join-Path $buildPath 'rep_player.exe')
if ($LASTEXITCODE -ne 0) { throw 'Player link failed' }
Write-Output 'Built native REP player and validation tools.'
