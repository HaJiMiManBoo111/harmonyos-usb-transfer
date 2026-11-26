# Flux x Lingxi installer (ASCII only, no BOM)
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File install.ps1 [-Target <dir>]
param(
    [string]$Target = '',
    [string]$Zip = ''
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
if ($Zip -eq '') { $Zip = Join-Path $Root 'dist\flux-1.27.0-flux.zip' }
if ($Target -eq '') {
    $Target = Join-Path $env:LOCALAPPDATA 'Programs\Flux'
    try {
        $probe = 'C:\Program Files\Flux'
        New-Item -ItemType Directory -Path $probe -Force -ErrorAction Stop | Out-Null
        Set-Content -Path (Join-Path $probe '.wtest') -Value 'ok' -ErrorAction Stop | Out-Null
        Remove-Item (Join-Path $probe '.wtest') -Force -ErrorAction Stop
        $Target = $probe
    } catch {
        Remove-Item 'C:\Program Files\Flux' -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# Remove the superseded Deskflow-Lingxi installation (our own earlier build, not upstream Deskflow)
$Legacy = Join-Path $env:LOCALAPPDATA 'Programs\Deskflow-Lingxi'
if (Test-Path $Legacy) { Remove-Item $Legacy -Recurse -Force -ErrorAction SilentlyContinue }
foreach ($lk in @('HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Deskflow-Lingxi', 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Deskflow-Lingxi')) {
    Remove-Item $lk -Recurse -Force -ErrorAction SilentlyContinue
}
foreach ($sp in @([Environment]::GetFolderPath('CommonPrograms'), [Environment]::GetFolderPath('Programs'), [Environment]::GetFolderPath('Desktop'))) {
    if ($sp -ne '') { Remove-Item (Join-Path $sp 'Deskflow Lingxi.lnk') -Force -ErrorAction SilentlyContinue }
}

function Need($p) {
    if (-not (Test-Path $p)) { throw "missing required file: $p" }
}
function Write-FluxConfigs($SettingsDir) {
    # HarmonyOS-compatible portable config: Barrier 1.8 server on 24800,
    # no TLS; external server config so persistServerConfig() will not
    # truncate flux-server.conf; and auto-start core together with the GUI.
    if (-not (Test-Path -LiteralPath $SettingsDir)) {
        New-Item -ItemType Directory -Path $SettingsDir -Force | Out-Null
    }
    $conf = @(
        '[core]',
        'computerName=JARVIS',
        'coreMode=2',
        'port=24800',
        '',
        '[security]',
        'tlsEnabled=false',
        '',
        '[server]',
        'protocol=barrier',
        'externalConfig=true',
        'enableClipboard=true',
        '',
        '[gui]',
        'startCoreWithGui=true',
        'shownServerFirstStartMessage=true',
        'shownFirstConnectedMessage=true',
        'logExpanded=false',
        '',
        '[computer_JARVIS]',
        'name=JARVIS',
        '',
        '[computer_HarmonyPC]',
        'name=HarmonyPC',
        '',
        '[log]',
        'level=Info'
    ) -join "`n"
    [System.IO.File]::WriteAllText((Join-Path $SettingsDir 'Flux.conf'), $conf + "`n", [System.Text.Encoding]::ASCII)
    $tab = "`t"
    $srv = @(
        'section: links',
        ($tab + 'JARVIS:'),
        ($tab + $tab + 'right = HarmonyPC'),
        ($tab + 'HarmonyPC:'),
        ($tab + $tab + 'left = JARVIS'),
        'end',
        '',
        'section: options',
        'end'
    ) -join "`n"
    [System.IO.File]::WriteAllText((Join-Path $SettingsDir 'flux-server.conf'), $srv, [System.Text.Encoding]::ASCII)
}


Need $Zip
$Seven = 'C:\Program Files\7-Zip\7z.exe'
Need $Seven

$Stage = Join-Path $Root 'install-stage'
if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
New-Item -ItemType Directory -Path $Stage | Out-Null
& $Seven x $Zip ("-o" + $Stage) -y | Out-Null
if ($LASTEXITCODE -ne 0) { throw "7z extract failed exit=$LASTEXITCODE" }

$Src = Join-Path $Stage 'dist'
Need (Join-Path $Src 'flux.exe')
Need (Join-Path $Src 'flux-core.exe')
Need (Join-Path $Src 'flux-daemon.exe')
Need (Join-Path $Src 'plugins\platforms\qwindows.dll')

if (Test-Path $Target) { Remove-Item $Target -Recurse -Force }
New-Item -ItemType Directory -Path $Target -Force | Out-Null
Copy-Item -Path (Join-Path $Src '*') -Destination $Target -Recurse -Force
Remove-Item $Stage -Recurse -Force

# Portable settings keeps this build away from %APPDATA%\Roaming\Deskflow
$SettingsDir = Join-Path $Target 'settings'
if (-not (Test-Path $SettingsDir)) { New-Item -ItemType Directory -Path $SettingsDir | Out-Null }
$Conf = Join-Path $SettingsDir 'Flux.conf'
Write-FluxConfigs -SettingsDir $SettingsDir

# Verify the installed binaries actually run (no long-lived instance)
# A harmless Qt qpa warning on stderr must not abort install under Stop.
$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
Push-Location $Target
$null = & '.\flux.exe' --version 2>&1
$GuiExit = $LASTEXITCODE
$null = & '.\flux-core.exe' --version 2>&1
$CoreExit = $LASTEXITCODE
Pop-Location
$ErrorActionPreference = $prevEap
# --version initialises Settings and writes host values back; rewrite configs
Write-FluxConfigs -SettingsDir $SettingsDir
if ($GuiExit -ne 0 -or $CoreExit -ne 0) { throw "binary check failed GUI=$GuiExit CORE=$CoreExit" }

# Uninstall script (copies itself logic into TEMP so the target dir can be removed)
$UnPs1 = Join-Path $Target 'uninstall.ps1'
$UnContent = @'
$ErrorActionPreference = 'SilentlyContinue'
$hklm = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Flux'
$hkcu = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Flux'
Remove-Item $hklm -Recurse -Force
Remove-Item $hkcu -Recurse -Force
Remove-Item (Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\Flux.lnk') -Force
Remove-Item (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Flux.lnk') -Force
Remove-Item (Join-Path $env:USERPROFILE 'Desktop\Flux.lnk') -Force
$tmp = Join-Path $env:TEMP 'dsl-rmdir.cmd'
Set-Content -Path $tmp -Encoding ASCII -Value ('@echo off' + "`r`n" + 'timeout /t 2 >nul' + "`r`n" + 'rmdir /s /q "' + $T + '"')
Start-Process cmd.exe -ArgumentList '/c', $tmp -WindowStyle Hidden
'@
$UnContent = '$T = ' + "'" + $Target + "'" + "`r`n" + $UnContent
Set-Content -Path $UnPs1 -Encoding ASCII -Value $UnContent

$UnCmd = Join-Path $Target 'uninstall.cmd'
Set-Content -Path $UnCmd -Encoding ASCII -Value ('@echo off' + "`r`n" + 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1"')

# Registry uninstall entry: prefer HKLM, fall back to HKCU when not elevated
$KeyHKLM = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Flux'
$KeyHKCU = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Flux'
$Key = $KeyHKCU
try {
    New-Item -Path $KeyHKLM -Force -ErrorAction Stop | Out-Null
    $Key = $KeyHKLM
} catch {
    New-Item -Path $KeyHKCU -Force | Out-Null
}
$SizeKB = [int]((Get-ChildItem $Target -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1KB)
$Vals = [ordered]@{
    DisplayName     = 'Flux 1.27.0-flux'
    DisplayVersion  = '1.27.0.0'
    Publisher       = 'Flux merged build'
    InstallLocation = $Target
    DisplayIcon     = (Join-Path $Target 'flux.exe')
    UninstallString = '"' + (Join-Path $Target 'uninstall.cmd') + '"'
    NoModify        = 1
    NoRepair        = 1
    EstimatedSize   = $SizeKB
}
foreach ($k in $Vals.Keys) {
    $pt = if ($Vals[$k] -is [int]) { 'DWord' } else { 'String' }
    New-ItemProperty -Path $Key -Name $k -Value $Vals[$k] -PropertyType $pt -Force | Out-Null
}

# Shortcuts
$ws = New-Object -ComObject WScript.Shell
$Made = @()
$Dirs = @(
    [Environment]::GetFolderPath('CommonPrograms'),
    [Environment]::GetFolderPath('Programs'),
    [Environment]::GetFolderPath('Desktop')
)
foreach ($d in $Dirs) {
    if ($d -eq '' -or -not (Test-Path $d)) { continue }
    $p = Join-Path $d 'Flux.lnk'
    try {
        $s = $ws.CreateShortcut($p)
        $s.TargetPath = Join-Path $Target 'flux.exe'
        $s.WorkingDirectory = $Target
        $s.IconLocation = (Join-Path $Target 'flux.exe') + ',0'
        $s.Description = 'Flux - KVM and HarmonyOS file transfer'
        $s.Save()
        $Made += $p
    } catch { }
}

# The --version probes above let Qt flush QSettings asynchronously; wait, then
# rewrite the configs so no host-specific value ships in the install.
Start-Sleep -Milliseconds 800
Write-FluxConfigs -SettingsDir $SettingsDir

$Files = (Get-ChildItem $Target -Recurse -File).Count
Write-Output ('INSTALL_PATH=' + $Target)
Write-Output ('UNINSTALL_KEY=' + $Key)
Write-Output ('GUI_EXIT=' + $GuiExit)
Write-Output ('CORE_EXIT=' + $CoreExit)
Write-Output ('INSTALLED_FILES=' + $Files)
Write-Output ('SIZE_KB=' + $SizeKB)
Write-Output ('SHORTCUTS=' + $Made.Count)
foreach ($m in $Made) { Write-Output ('LNK=' + $m) }
Write-Output ('SETTINGS_CONF=' + (Get-Content $Conf -Raw).Trim())
Write-Output 'INSTALL_OK=1'
