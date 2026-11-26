$ErrorActionPreference = 'SilentlyContinue'
$Root = 'C:\Users\Administrator\Desktop\Quick\deskflow-lingxi'
$Install = Join-Path $env:LOCALAPPDATA 'Programs\Flux'
$Hdc = 'C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'

Write-Output '=== deskflow processes ==='
Get-CimInstance Win32_Process | Where-Object { $_.Name -like '*deskflow*' } | ForEach-Object {
    Write-Output ($_.Name + '|PID=' + $_.ProcessId + '|PPID=' + $_.ParentProcessId + '|EXE=' + $_.ExecutablePath + '|CMD=' + $_.CommandLine)
}

Write-Output '=== port 24800 ==='
$c = Get-NetTCPConnection -LocalPort 24800 -ErrorAction SilentlyContinue
if ($c) {
    $c | ForEach-Object { Write-Output ($_.LocalAddress + ':' + $_.LocalPort + ' ' + $_.State + ' OWNER=' + $_.OwningProcess) }
} else { Write-Output 'NO_LISTENER' }

Write-Output '=== IPv4 ==='
Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -ne '127.0.0.1' } | ForEach-Object {
    Write-Output ($_.InterfaceAlias + '|' + $_.IPAddress)
}

Write-Output '=== hdc ==='
Write-Output ('HDC_EXISTS=' + (Test-Path $Hdc))
if (Test-Path $Hdc) {
    Write-Output '--- list targets ---'
    & $Hdc list targets 2>&1 | ForEach-Object { Write-Output ('HDC| ' + $_) }
    Write-Output '--- fport ls ---'
    & $Hdc fport ls 2>&1 | ForEach-Object { Write-Output ('FPORT| ' + $_) }
}

Write-Output '=== installed build ==='
Write-Output ('INSTALL_EXISTS=' + (Test-Path $Install))
if (Test-Path $Install) {
    Write-Output ('EXE_BYTES=' + (Get-Item (Join-Path $Install 'flux.exe')).Length)
    Write-Output ('CORE_BYTES=' + (Get-Item (Join-Path $Install 'flux-core.exe')).Length)
    Write-Output ('CONF=' + ((Get-Content (Join-Path $Install 'settings\Flux.conf') -Raw) -replace "`r`n", '|'))
}

Write-Output '=== original install ==='
Write-Output ('ORIG_EXE=' + (Test-Path 'C:\Program Files\Deskflow\deskflow.exe'))
$oc = Join-Path $env:APPDATA 'Deskflow\Deskflow.conf'
Write-Output ('ORIG_CONF=' + (Test-Path $oc))
if (Test-Path $oc) { Write-Output ('ORIG_CONF_MTIME=' + (Get-Item $oc).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')) }

Write-Output '=== harmony side on device ==='
if (Test-Path $Hdc) {
    Write-Output '--- packages ---'
    & $Hdc shell 'pm list packages' 2>&1 | Select-String -Pattern 'deskflow' | ForEach-Object { Write-Output ('PKG| ' + $_.Line) }
    Write-Output '--- Flux dir ---'
    & $Hdc shell 'ls -la /data/app/el2/100/base/com.deskflow.ohospoc/haps/entry/files/Deskflow' 2>&1 | ForEach-Object { Write-Output ('FILES| ' + $_) }
}

Write-Output '=== harmony helper scripts on disk ==='
Get-ChildItem 'C:\Users\Administrator\Desktop\Quick' -Recurse -File -Include '*.ps1','*.bat' -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match 'harmony|autostart|authclick|build-and-install' } |
    ForEach-Object { Write-Output ('SCRIPT| ' + $_.FullName) }

Write-Output 'CHECK_DONE=1'
