# ===========================================================================
#  flux 1.27.0-flux  --  repeatable portable packaging
#  Invoked by scripts\package.cmd. Pure ASCII on purpose: PowerShell 5.1
#  reads a BOM-less UTF-8 .ps1 as ANSI, which would garble non-ASCII text.
#
#  NEVER runs build.cmd / configure.cmd / cmake --build.
#  cmake --install is NOT used (measured on cmake 4.4.3):
#    * `--dry-run` -> `Unknown argument --dry-run`
#    * real run -> `file Could not resolve runtime dependencies:
#                   libcrypto-3-x64.dll libssl-3-x64.dll`
#    * gui install rule has `REGEX "/dx[^/]*\\.dll$" EXCLUDE`
#      -> dxcompiler.dll / dxil.dll would be dropped
#    * it also writes build\install_manifest.txt (outside our write scope)
#  windeployqt is NOT used; Qt/OpenSSL DLLs are copied directly. TMP/TEMP
#  still point at <root>\tmp (same fix build.cmd uses for windeployqt).
# ===========================================================================

$ErrorActionPreference = 'Stop'

$Root    = Split-Path -Parent $PSScriptRoot
$Dist    = Join-Path $Root 'dist'
$Rel     = Join-Path $Root 'build\bin\Release'
$BuildTr = Join-Path $Root 'build\translations'
$QtBin   = Join-Path $Root 'tools\Qt\6.8.3\msvc2022_64\bin'
$QtPlat  = Join-Path $Root 'tools\Qt\6.8.3\msvc2022_64\plugins\platforms'
$OsslBin = Join-Path $Root 'tools\openssl\Library\bin'
$SrcDocs = Join-Path $Root 'deskflow\docs'
$SrcLic  = Join-Path $Root 'deskflow\LICENSE'
$SrcLicEx= Join-Path $Root 'deskflow\LICENSES\LicenseRef-OpenSSL-Exception.txt'
$Readme  = Join-Path $Root 'README-dist.md'
$TmpDir  = Join-Path $Root 'tmp'
$SevenZip= 'C:\Program Files\7-Zip\7z.exe'
$ZipName = 'flux-1.27.0-flux.zip'
$Zip     = Join-Path $Dist $ZipName

function Need-File($p) { if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Missing file: $p  (build the source first, then re-run this script)" } }
function Need-Dir($p)  { if (-not (Test-Path -LiteralPath $p -PathType Container)) { throw "Missing dir: $p" } }

Write-Host "ROOT = $Root"
Write-Host "DIST = $Dist"

# Save the pristine system PATH. The --version check below uses ONLY
# dist + this original PATH, so it truly proves dist is self-contained.
$origPath = $env:PATH

# --- 0. defensive TMP/TEMP (windeployqt compatibility) ----------------------
if (-not (Test-Path -LiteralPath $TmpDir)) { New-Item -ItemType Directory -Path $TmpDir -Force | Out-Null }
$env:TMP  = $TmpDir
$env:TEMP = $TmpDir

# --- 1. preflight ------------------------------------------------------------
foreach ($e in @('flux.exe','flux-core.exe','flux-daemon.exe')) { Need-File (Join-Path $Rel $e) }
foreach ($d in @('Qt6Core.dll','Qt6Gui.dll','Qt6Network.dll','Qt6Svg.dll','Qt6Widgets.dll')) { Need-File (Join-Path $Rel $d) }
foreach ($d in @('dxcompiler.dll','dxil.dll')) { Need-File (Join-Path $Rel $d) }
Need-Dir (Join-Path $Rel 'plugins')
Need-Dir $BuildTr
Need-Dir $QtBin
Need-Dir $QtPlat
Need-Dir $OsslBin
Need-File (Join-Path $QtPlat 'qwindows.dll')
Need-File (Join-Path $OsslBin 'libcrypto-3-x64.dll')
Need-File (Join-Path $OsslBin 'libssl-3-x64.dll')
Need-File $SevenZip
Need-File $Readme

# --- 2. clean dist (removes stale _stage / _probe / older zip) --------------
if (Test-Path -LiteralPath $Dist) { Remove-Item -LiteralPath $Dist -Recurse -Force }
New-Item -ItemType Directory -Path $Dist -Force | Out-Null
Write-Host 'Cleaned dist (including stale _stage / _probe / old zip).'

# --- 3. self-built copy logic ------------------------------------------------
# 3.1 every top-level .exe / .dll from Release (3 exes + 5 Qt DLLs + dxcompiler + dxil)
Get-ChildItem -LiteralPath $Rel -File |
    Where-Object { $_.Extension -in '.exe','.dll' } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $Dist -Force }

# 3.2 plugins\ full tree (explicit dir creation avoids PS 5.1 Copy-Item -Recurse
#     bug when the destination directory does not exist yet)
$dstPlugins = Join-Path $Dist 'plugins'
New-Item -ItemType Directory -Path $dstPlugins -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $Rel 'plugins') -Force | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $dstPlugins -Recurse -Force
}
# ensure the platform plugin from the Qt SDK too (identical to Release copy)
New-Item -ItemType Directory -Path (Join-Path $dstPlugins 'platforms') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $QtPlat 'qwindows.dll') -Destination (Join-Path $dstPlugins 'platforms\qwindows.dll') -Force

# 3.3 translations\ (real .qm files live in build\translations\)
$dstTr = Join-Path $Dist 'translations'
New-Item -ItemType Directory -Path $dstTr -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $Rel 'translations') -Filter '*.qm' -ErrorAction SilentlyContinue |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $dstTr -Force }
Get-ChildItem -LiteralPath $BuildTr -Filter '*.qm' |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $dstTr -Force }

# 3.4 supplement Qt runtime DLLs from the Qt SDK bin. Only ADD files that are
#     not already present, so the build-matched Release DLLs are never
#     overwritten by an SDK copy.
Get-ChildItem -LiteralPath $QtBin -Filter 'Qt6*.dll' |
    Where-Object { $_.Name -notmatch 'd\.dll$' } |
    ForEach-Object {
        $target = Join-Path $Dist $_.Name
        if (-not (Test-Path -LiteralPath $target)) { Copy-Item -LiteralPath $_.FullName -Destination $Dist -Force }
    }

# 3.5 OpenSSL runtime (absent from Release; cmake --install fails on these)
Copy-Item -LiteralPath (Join-Path $OsslBin 'libcrypto-3-x64.dll') -Destination $Dist -Force
Copy-Item -LiteralPath (Join-Path $OsslBin 'libssl-3-x64.dll')     -Destination $Dist -Force

# 3.6 docs / licenses / README
New-Item -ItemType Directory -Path (Join-Path $Dist 'docs') -Force | Out-Null
foreach ($n in @('Readme.md','HelpMain.md','Configuration.md','Issues.md','Security.md')) {
    $src = Join-Path $SrcDocs $n
    if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src -Destination (Join-Path $Dist 'docs') -Force }
}
if (Test-Path -LiteralPath $SrcLic)   { Copy-Item -LiteralPath $SrcLic   -Destination (Join-Path $Dist 'LICENSE') -Force }
if (Test-Path -LiteralPath $SrcLicEx) { Copy-Item -LiteralPath $SrcLicEx -Destination (Join-Path $Dist 'LICENSE_EXCEPTION') -Force }
Copy-Item -LiteralPath $Readme -Destination (Join-Path $Dist 'README-dist.md') -Force

# 3.7 portable settings. Upstream enables portable mode when
#     <exeDir>\settings\Flux.conf exists (Settings.cpp:511-515, 435-438),
#     which keeps this build away from %APPDATA%\Roaming\Deskflow.
$dstSettings = Join-Path $Dist 'settings'
New-Item -ItemType Directory -Path $dstSettings -Force | Out-Null
Set-Content -LiteralPath (Join-Path $dstSettings 'Flux.conf') -Encoding ASCII -Value '[core]'

# --- 4. verify: --version only, exit 0 and output contains the version ------
# Strict PATH: dist + original system PATH. No Qt SDK / OpenSSL bin added.
$env:PATH = "$Dist;$origPath"
function Check-Version($exePath, $label) {
    $out = (& $exePath --version 2>&1 | Out-String)
    $rc  = $LASTEXITCODE
    Write-Host ''
    Write-Host "---- $label : $(Split-Path -Leaf $exePath) --version  (exit=$rc) ----"
    Write-Host $out.TrimEnd()
    if ($rc -ne 0)                 { throw "$label --version exit code $rc (dist not self-contained?)" }
    if ($out -notmatch '1\.27\.0') { throw "$label --version output lacks version 1.27.0" }
}
Check-Version (Join-Path $Dist 'flux.exe')      'flux.exe'
Check-Version (Join-Path $Dist 'flux-core.exe') 'flux-core.exe'
# --version initializes Settings and may write host-specific defaults into the
# portable conf; reset it so the shipped zip has a clean minimal config.
Set-Content -LiteralPath (Join-Path $dstSettings 'Flux.conf') -Encoding ASCII -Value '[core]'

# --- 5. zip, with dist\ as the single top-level folder -----------------------
Push-Location $Root
try {
    & $SevenZip a -tzip $Zip 'dist' "-xr!$ZipName" -y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "7z failed, exit=$LASTEXITCODE" }
} finally { Pop-Location }
Need-File $Zip

Add-Type -AssemblyName System.IO.Compression.FileSystem
$z = [System.IO.Compression.ZipFile]::OpenRead($Zip)
$entryCount = $z.Entries.Count
$z.Dispose()
$zipBytes = (Get-Item -LiteralPath $Zip).Length

# --- 6. report ---------------------------------------------------------------
Write-Host ''
Write-Host '==== dist top-level files (name + bytes) ===='
Get-ChildItem -LiteralPath $Dist -File | Sort-Object Name | ForEach-Object {
    '{0,-40} {1,12} B' -f $_.Name, $_.Length
}
Write-Host ''
Write-Host '==== dist subdirectories ===='
Get-ChildItem -LiteralPath $Dist -Directory | Sort-Object Name | ForEach-Object {
    $cnt = (Get-ChildItem -LiteralPath $_.FullName -Recurse -File | Measure-Object).Count
    '{0,-40} {1,5} files' -f $_.Name, $cnt
}
Write-Host ''
Write-Host "ZIP      = $Zip"
Write-Host "ZIP_SIZE = $zipBytes B"
Write-Host "ZIP_ENTRIES = $entryCount"
Write-Host 'PACKAGE_OK'
