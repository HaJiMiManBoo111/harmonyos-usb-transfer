# Install OpenSSL 3.5.4 (MSVC x64, static) into tools/openssl
# Run inside a VS2022 x64 developer environment.
param(
    [string]$Root = 'C:\Users\Administrator\Desktop\Quick\deskflow-lingxi'
)

$ErrorActionPreference = 'Stop'
$src     = Join-Path $Root 'tools\openssl-openssl-3.5.4'
$prefix  = Join-Path $Root 'tools\openssl'
$perl    = 'C:\Program Files\Git\usr\bin\perl.exe'
$nasmDir = Join-Path $Root 'tools\nasm'
$log     = Join-Path $Root 'logs\build-openssl.log'

New-Item -ItemType Directory -Force -Path (Join-Path $Root 'logs') | Out-Null

function Log($m) {
    $line = "[{0}] {1}" -f (Get-Date -Format HH:mm:ss), $m
    $line | Tee-Object -FilePath $log -Append
}

Log "start openssl build"
Log "src=$src"
Log "prefix=$prefix"
Log "perl=$perl exists=$(Test-Path $perl)"

# nasm for x64 assembly
$nasmExe = Get-ChildItem -Path $nasmDir -Filter nasm.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if ($nasmExe) {
    $env:PATH = "$($nasmExe.DirectoryName);$env:PATH"
    Log "nasm=$($nasmExe.FullName)"
} else {
    Log "WARN nasm not found, building no-asm"
}

Set-Location $src

$hasAsm = [bool]$nasmExe
$perlArgs = @(
    'Configure',
    'VC-WIN64A',
    '--prefix=' + ($prefix -replace '\\','/'),
    '--openssldir=' + (($prefix -replace '\\','/') + '/ssl'),
    'no-shared',
    'no-tests',
    'no-docs',
    '--release'
)
if (-not $hasAsm) { $perlArgs += 'no-asm' }

Log ("perl " + ($perlArgs -join ' '))
& $perl @perlArgs 2>&1 | Tee-Object -FilePath $log -Append
Log "Configure exit=$LASTEXITCODE"
if ($LASTEXITCODE -ne 0) { Log 'CONFIGURE FAILED'; exit 1 }

Log 'begin nmake'
$nmake = Get-Command nmake.exe -ErrorAction SilentlyContinue
Log "nmake=$($nmake.Source)"
& nmake 2>&1 | Tee-Object -FilePath $log -Append
Log "nmake exit=$LASTEXITCODE"
if ($LASTEXITCODE -ne 0) { Log 'BUILD FAILED'; exit 1 }

Log 'begin install'
& nmake install_sw 2>&1 | Tee-Object -FilePath $log -Append
Log "install exit=$LASTEXITCODE"

Log "ssl.h exists=$(Test-Path (Join-Path $prefix 'include\openssl\ssl.h'))"
Get-ChildItem (Join-Path $prefix 'lib') -ErrorAction SilentlyContinue | Select-Object Name,Length | Format-Table | Out-String | Tee-Object -FilePath $log -Append
Log 'done'
