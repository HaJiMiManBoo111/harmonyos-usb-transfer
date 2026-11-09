$ErrorActionPreference = 'Stop'
# Self-contained packaging. Independent filename so it never collides with
# the earlier packaging subagent's package.ps1. No windeployqt / cmake install.

$Root    = Split-Path -Parent $PSScriptRoot
$Rel     = Join-Path $Root 'build\bin\Release'
$BuildTr = Join-Path $Root 'build\translations'
$Ossl    = Join-Path $Root 'tools\openssl\Library\bin'
$Docs    = Join-Path $Root 'deskflow\docs'
$Lic     = Join-Path $Root 'deskflow\LICENSE'
$LicEx   = Join-Path $Root 'deskflow\LICENSES\LicenseRef-OpenSSL-Exception.txt'
$Dist    = Join-Path $Root 'dist'
$Tmp     = Join-Path $Root 'tmp'
$Seven   = 'C:\Program Files\7-Zip\7z.exe'
$Zip     = Join-Path $Root 'flux-1.27.0-flux.zip'
$Readme  = Join-Path $Root 'README-dist.md'

function Need($p) { if (-not (Test-Path -LiteralPath $p)) { throw "Missing: $p" } }
foreach ($e in @('flux.exe','flux-core.exe','flux-daemon.exe')) { Need (Join-Path $Rel $e) }
Need (Join-Path $Rel 'plugins\platforms\qwindows.dll')
Need (Join-Path $Ossl 'libcrypto-3-x64.dll')
Need (Join-Path $Ossl 'libssl-3-x64.dll')
Need $Seven
if (-not (Test-Path -LiteralPath $Tmp)) { New-Item -ItemType Directory -Path $Tmp -Force | Out-Null }
$env:TMP = $Tmp; $env:TEMP = $Tmp
$env:PATH = "$Rel;$Ossl;$env:PATH"

# fresh dist
if (Test-Path -LiteralPath $Dist) { Remove-Item -Recurse -Force $Dist }
New-Item -ItemType Directory -Path $Dist -Force | Out-Null
if (Test-Path -LiteralPath $Zip) { Remove-Item -Force $Zip }

# 1. all exes + top-level dlls from Release
Get-ChildItem -LiteralPath $Rel -File | Where-Object { $_.Extension -in '.exe','.dll' } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $Dist -Force }

# 2. OpenSSL runtime dlls
Copy-Item -LiteralPath (Join-Path $Ossl 'libcrypto-3-x64.dll') -Destination $Dist -Force
Copy-Item -LiteralPath (Join-Path $Ossl 'libssl-3-x64.dll')    -Destination $Dist -Force

# 3. Qt plugins (full tree)
Copy-Item -LiteralPath (Join-Path $Rel 'plugins') -Destination (Join-Path $Dist 'plugins') -Recurse -Force

# 4. translations (.qm live in build\translations)
New-Item -ItemType Directory -Path (Join-Path $Dist 'translations') -Force | Out-Null
if (Test-Path -LiteralPath $BuildTr) {
    Get-ChildItem -LiteralPath $BuildTr -Filter '*.qm' |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $Dist 'translations') -Force }
}

# 5. docs + licenses
if (Test-Path -LiteralPath $Docs) {
    New-Item -ItemType Directory -Path (Join-Path $Dist 'docs') -Force | Out-Null
    Get-ChildItem -LiteralPath $Docs -Filter '*.md' |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $Dist 'docs') -Force }
}
if (Test-Path -LiteralPath $Lic)   { Copy-Item -LiteralPath $Lic   -Destination (Join-Path $Dist 'LICENSE') -Force }
if (Test-Path -LiteralPath $LicEx) { Copy-Item -LiteralPath $LicEx -Destination (Join-Path $Dist 'LICENSE_EXCEPTION') -Force }

# 6. portable settings: isolate this build from the installed Flux config
New-Item -ItemType Directory -Path (Join-Path $Dist 'settings') -Force | Out-Null
Set-Content -Path (Join-Path $Dist 'settings\Flux.conf') -Encoding ASCII -Value '[core]'

# 7. README
if (Test-Path -LiteralPath $Readme) { Copy-Item -LiteralPath $Readme -Destination (Join-Path $Dist 'README-dist.md') -Force }

# 8. verify both exes resolve all DLLs
Push-Location $Dist
try {
    $o1 = (& '.\flux.exe' --version 2>&1 | Out-String); $r1 = $LASTEXITCODE
    $o2 = (& '.\flux-core.exe' --version 2>&1 | Out-String); $r2 = $LASTEXITCODE
} finally { Pop-Location }
Write-Output "GUI_EXIT=$r1"
Write-Output "CORE_EXIT=$r2"
if ($r1 -ne 0 -or $r2 -ne 0) { throw "exe --version failed (missing DLL?)" }

# 8b. --version initializes Settings and rewrites the portable conf with
# machine-specific defaults (e.g. computerName=JARVIS). Reset it to a clean
# minimal conf so the shipped zip has no host-specific values.
Set-Content -Path (Join-Path $Dist 'settings\Flux.conf') -Encoding ASCII -Value '[core]'

# 9. zip with dist/ as the top-level folder
Push-Location $Root
try {
    & $Seven a -tzip $Zip 'dist' -y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "7z failed exit=$LASTEXITCODE" }
} finally { Pop-Location }
Need $Zip

Add-Type -AssemblyName System.IO.Compression.FileSystem
$z = [System.IO.Compression.ZipFile]::OpenRead($Zip)
$n = $z.Entries.Count
$z.Dispose()
$files = (Get-ChildItem -LiteralPath $Dist -Recurse -File | Measure-Object).Count
Write-Output "DIST_FILES=$files"
Write-Output "ZIP_BYTES=$((Get-Item -LiteralPath $Zip).Length)"
Write-Output "ZIP_ENTRIES=$n"
Write-Output "ZIP_PATH=$Zip"
Write-Output "PACKAGE_OK=1"
