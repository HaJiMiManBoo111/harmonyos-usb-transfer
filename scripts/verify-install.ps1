$ErrorActionPreference = 'SilentlyContinue'
$T = Join-Path $env:LOCALAPPDATA 'Programs\Flux'
$c = Join-Path $T 'settings\Flux.conf'
$OrigConf = Join-Path $env:APPDATA 'Deskflow\Deskflow.conf'

Write-Output ('INSTALL_PATH=' + $T)
Write-Output '--- wait then re-read portable conf (stability check) ---'
Start-Sleep -Seconds 3
Write-Output ('CONF_NOW=' + ((Get-Content $c -Raw) -replace "`r`n", '|'))
if ((Get-Content $c -Raw) -notmatch '^\[core\]') {
    Set-Content -Path $c -Encoding ASCII -Value '[core]'
    Write-Output 'CONF_RESET=1'
}
Write-Output ('CONF_FINAL=' + ((Get-Content $c -Raw) -replace "`r`n", '|'))

Write-Output '--- running deskflow processes ---'
Get-CimInstance Win32_Process | Where-Object {
    $_.Name -eq 'flux.exe' -or $_.Name -eq 'flux-core.exe' -or $_.Name -eq 'flux-daemon.exe'
} | ForEach-Object {
    Write-Output ($_.Name + ' PID=' + $_.ProcessId + ' PATH=' + $_.ExecutablePath)
}

Write-Output '--- original install intact ---'
Write-Output ('ORIGINAL_EXE=' + (Test-Path 'C:\Program Files\Deskflow\deskflow.exe'))
Write-Output ('ORIGINAL_CORE=' + (Test-Path 'C:\Program Files\Deskflow\deskflow-core.exe'))
Write-Output ('ORIGINAL_CONF=' + (Test-Path $OrigConf))
if (Test-Path $OrigConf) {
    Write-Output ('ORIGINAL_CONF_BYTES=' + (Get-Item $OrigConf).Length)
    Write-Output ('ORIGINAL_CONF_MTIME=' + (Get-Item $OrigConf).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))
}

Write-Output '--- installed files ---'
Write-Output ('EXE=' + (Test-Path (Join-Path $T 'flux.exe')))
Write-Output ('CORE=' + (Test-Path (Join-Path $T 'flux-core.exe')))
Write-Output ('DAEMON=' + (Test-Path (Join-Path $T 'flux-daemon.exe')))
Write-Output ('QWINDOWS=' + (Test-Path (Join-Path $T 'plugins\platforms\qwindows.dll')))
Write-Output ('LIBCRYPTO=' + (Test-Path (Join-Path $T 'libcrypto-3-x64.dll')))
Write-Output ('LIBSSL=' + (Test-Path (Join-Path $T 'libssl-3-x64.dll')))
Write-Output ('README=' + (Test-Path (Join-Path $T 'README-dist.md')))
Write-Output ('UNINSTALL_CMD=' + (Test-Path (Join-Path $T 'uninstall.cmd')))
Write-Output ('UNINSTALL_PS1=' + (Test-Path (Join-Path $T 'uninstall.ps1')))
Write-Output ('SHORTCUT_DESKTOP=' + (Test-Path (Join-Path $env:USERPROFILE 'Desktop\Flux.lnk')))
Write-Output ('SHORTCUT_STARTMENU=' + (Test-Path (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Flux.lnk')))
Write-Output ('UNINSTALL_KEY_HKCU=' + (Test-Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Flux'))
$reg = Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Flux'
Write-Output ('REG_DISPLAYNAME=' + $reg.DisplayName)
Write-Output ('REG_UNINSTALL=' + $reg.UninstallString)
Write-Output 'VERIFY_OK=1'
