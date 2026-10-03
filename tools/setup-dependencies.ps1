<#
Prepare the native build dependencies inside this checkout.
Requires Windows 10/11, PowerShell 5.1+, curl.exe and tar.exe (included in Windows).
No CMake, Ninja, Python, Anaconda or preinstalled compiler is required.
FFmpeg is distributed separately; this script builds the player libraries only.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$cachePath = Join-Path $taskRoot 'dependency-cache'
$toolchainParent = Join-Path $taskRoot 'toolchain'
$toolchainName = 'llvm-mingw-20260616-ucrt-x86_64'
$toolchainPath = Join-Path $toolchainParent $toolchainName
$compiler = Join-Path $toolchainPath 'bin\x86_64-w64-mingw32-clang.exe'
$archiver = Join-Path $toolchainPath 'bin\llvm-ar.exe'
$curl = Join-Path $env:SystemRoot 'System32\curl.exe'
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'

foreach ($tool in @($curl, $tar)) {
    if (!(Test-Path -LiteralPath $tool)) { throw "Required Windows tool is missing: $tool" }
}
New-Item -ItemType Directory -Path $cachePath, $toolchainParent -Force | Out-Null

function Get-SourceArchive([string] $url, [string] $path) {
    if (!(Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -eq 0) {
        Write-Host "Downloading $url"
        $downloadPath = "$path.download"
        & $curl --fail --location --silent --show-error --connect-timeout 30 --max-time 900 --output $downloadPath $url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $url (curl exit $LASTEXITCODE)" }
        Move-Item -LiteralPath $downloadPath -Destination $path -Force
    }
}

function Expand-SourceArchive([string] $archive, [string] $destination) {
    & $tar -xf $archive -C $destination
    if ($LASTEXITCODE -ne 0) { throw "Archive extraction failed: $archive" }
}

if (!(Test-Path -LiteralPath $compiler)) {
    $toolchainArchive = Join-Path $cachePath "$toolchainName.zip"
    Get-SourceArchive "https://github.com/mstorsjo/llvm-mingw/releases/download/20260616/$toolchainName.zip" $toolchainArchive
    Expand-SourceArchive $toolchainArchive $toolchainParent
}
if (!(Test-Path -LiteralPath $compiler) -or !(Test-Path -LiteralPath $archiver)) {
    throw "The pinned LLVM MinGW toolchain was not extracted into $toolchainPath"
}

$zlibArchive = Join-Path $cachePath 'zlib-v1.2.11.tar.gz'
$zlibSource = Join-Path $cachePath 'zlib-1.2.11'
Get-SourceArchive 'https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v1.2.11' $zlibArchive
if (!(Test-Path -LiteralPath (Join-Path $zlibSource 'zlib.h'))) {
    Expand-SourceArchive $zlibArchive $cachePath
}

$freeTypeArchive = Join-Path $cachePath 'freetype-VER-2-12-1.tar.gz'
$freeTypeSource = Join-Path $cachePath 'freetype-VER-2-12-1'
Get-SourceArchive 'https://codeload.github.com/freetype/freetype/tar.gz/refs/tags/VER-2-12-1' $freeTypeArchive
if (!(Test-Path -LiteralPath (Join-Path $freeTypeSource 'include\ft2build.h'))) {
    Expand-SourceArchive $freeTypeArchive $cachePath
}

function Build-Archive([string] $sourceRoot, [string[]] $sources, [string] $objectDirectory,
                       [string[]] $options, [string] $outputArchive) {
    New-Item -ItemType Directory -Path $objectDirectory -Force | Out-Null
    $objects = @()
    foreach ($source in $sources) {
        $sourcePath = Join-Path $sourceRoot $source
        $objectName = ($source -replace '[\\/]', '_') -replace '\.c$', '.o'
        $objectPath = Join-Path $objectDirectory $objectName
        & $compiler @options -c $sourcePath -o $objectPath
        if ($LASTEXITCODE -ne 0) { throw "Dependency compilation failed: $source" }
        $objects += $objectPath
    }
    # Create a fresh temporary archive so an earlier build cannot retain old members.
    $newArchive = "$outputArchive.new"
    if (Test-Path -LiteralPath $newArchive) { Remove-Item -LiteralPath $newArchive }
    & $archiver rcs $newArchive @objects
    if ($LASTEXITCODE -ne 0) { throw "Dependency archiving failed: $outputArchive" }
    Move-Item -LiteralPath $newArchive -Destination $outputArchive -Force
}

$zlibVendor = Join-Path $taskRoot 'vendor\zlib'
$freeTypeVendor = Join-Path $taskRoot 'vendor\freetype'
New-Item -ItemType Directory -Path $zlibVendor, $freeTypeVendor -Force | Out-Null
Write-Host 'Building zlib 1.2.11 (static, x86_64 UCRT)...'
# Source list from upstream win32/Makefile.gcc; no assembly or shared-library option.
$zlibSources = @('adler32.c', 'compress.c', 'crc32.c', 'deflate.c', 'gzclose.c',
                 'gzlib.c', 'gzread.c', 'gzwrite.c', 'infback.c', 'inffast.c',
                 'inflate.c', 'inftrees.c', 'trees.c', 'uncompr.c', 'zutil.c')
Build-Archive $zlibSource $zlibSources (Join-Path $cachePath 'objects\zlib') @('-O2', '-Wno-deprecated-non-prototype', '-I', $zlibSource) (Join-Path $zlibVendor 'libz.a')
Copy-Item -LiteralPath (Join-Path $zlibSource 'zlib.h'), (Join-Path $zlibSource 'zconf.h') -Destination $zlibVendor -Force
Copy-Item -LiteralPath (Join-Path $zlibSource 'README') -Destination (Join-Path $zlibVendor 'README.txt') -Force
@'
zlib 1.2.11: unmodified upstream sources, statically compiled by tools/setup-dependencies.ps1.
Source: https://github.com/madler/zlib/tree/v1.2.11
Archive: https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v1.2.11
Original copyright and license notice: README.txt and zlib.h.
Build: LLVM MinGW 20260616, x86_64 UCRT, -O2; upstream win32/Makefile.gcc source list.
'@ | Set-Content -LiteralPath (Join-Path $zlibVendor 'SOURCE.txt') -Encoding UTF8

Write-Host 'Building FreeType 2.12.1 (static, x86_64 UCRT)...'
# Upstream docs/INSTALL.ANY describes these single-object module entry points.
# Keep the upstream default ftoption.h/ftmodule.h: PCF, BDF, TrueType, CFF,
# Type 1, SFNT and rasterizers are included. Compressed PCF uses libz.a built above.
# PNG/BZip2/Brotli/HarfBuzz/logging are disabled by upstream defaults, avoiding
# external font DLLs; OpenType SVG fetching remains the upstream default.
$freeTypeSources = @(
    'src/base/ftsystem.c', 'src/base/ftinit.c', 'src/base/ftdebug.c',
    'src/base/ftbase.c', 'src/base/ftbbox.c', 'src/base/ftglyph.c',
    'src/base/ftbdf.c', 'src/base/ftbitmap.c', 'src/base/ftcid.c',
    'src/base/ftfstype.c', 'src/base/ftgasp.c', 'src/base/ftgxval.c',
    'src/base/ftmm.c', 'src/base/ftotval.c', 'src/base/ftpatent.c',
    'src/base/ftpfr.c', 'src/base/ftstroke.c', 'src/base/ftsynth.c',
    'src/base/fttype1.c', 'src/base/ftwinfnt.c',
    'src/bdf/bdf.c', 'src/cff/cff.c', 'src/cid/type1cid.c', 'src/pcf/pcf.c',
    'src/pfr/pfr.c', 'src/sfnt/sfnt.c', 'src/truetype/truetype.c',
    'src/type1/type1.c', 'src/type42/type42.c', 'src/winfonts/winfnt.c',
    'src/raster/raster.c', 'src/sdf/sdf.c', 'src/smooth/smooth.c', 'src/svg/svg.c',
    'src/autofit/autofit.c', 'src/cache/ftcache.c', 'src/gzip/ftgzip.c',
    'src/lzw/ftlzw.c', 'src/bzip2/ftbzip2.c', 'src/gxvalid/gxvalid.c',
    'src/otvalid/otvalid.c', 'src/psaux/psaux.c', 'src/pshinter/pshinter.c',
    'src/psnames/psnames.c'
)
$freeTypeInclude = Join-Path $freeTypeSource 'include'
Build-Archive $freeTypeSource $freeTypeSources (Join-Path $cachePath 'objects\freetype') @('-O2', '-Wno-deprecated-non-prototype', '-DFT2_BUILD_LIBRARY', '-DFT_CONFIG_OPTION_SYSTEM_ZLIB', '-I', $freeTypeInclude, '-I', $zlibVendor) (Join-Path $freeTypeVendor 'libfreetype.a')
Copy-Item -LiteralPath $freeTypeInclude -Destination $freeTypeVendor -Recurse -Force
Copy-Item -LiteralPath (Join-Path $freeTypeSource 'LICENSE.TXT') -Destination $freeTypeVendor -Force
Copy-Item -LiteralPath (Join-Path $freeTypeSource 'docs\FTL.TXT') -Destination $freeTypeVendor -Force
Copy-Item -LiteralPath (Join-Path $freeTypeSource 'src\bdf\README') -Destination (Join-Path $freeTypeVendor 'BDF_NOTICE.txt') -Force
Copy-Item -LiteralPath (Join-Path $freeTypeSource 'src\pcf\README') -Destination (Join-Path $freeTypeVendor 'PCF_NOTICE.txt') -Force
@'
FreeType 2.12.1: unmodified upstream sources, statically compiled by tools/setup-dependencies.ps1.
Source: https://github.com/freetype/freetype/tree/VER-2-12-1
Archive: https://codeload.github.com/freetype/freetype/tar.gz/refs/tags/VER-2-12-1
License selected for redistribution: FreeType License, FTL.TXT; see LICENSE.TXT.
This software is based in part on the work of the FreeType Team.
Build: LLVM MinGW 20260616, x86_64 UCRT, -O2; upstream docs/INSTALL.ANY module procedure.
All upstream registered font modules are preserved. FT_CONFIG_OPTION_SYSTEM_ZLIB is enabled.
Compressed PCF uses the player's static zlib 1.2.11; PNG/BZip2/Brotli/HarfBuzz/logging are disabled.
Additional BDF/PCF copyright notices are included in BDF_NOTICE.txt and PCF_NOTICE.txt.
'@ | Set-Content -LiteralPath (Join-Path $freeTypeVendor 'SOURCE.txt') -Encoding UTF8

Write-Host 'Build dependencies are ready. Run .\build.ps1 next.'
