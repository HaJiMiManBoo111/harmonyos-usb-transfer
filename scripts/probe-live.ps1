$Hdc = 'C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
$T = '59C0226421001606'
Write-Output '=== FLUXSRC PROCESSES ==='
Get-CimInstance Win32_Process | Where-Object { $_.Name -like 'deskflow*' } | ForEach-Object {
  Write-Output ("{0} PID={1} PPID={2} EXE={3}" -f $_.Name, $_.ProcessId, $_.ParentProcessId, $_.ExecutablePath)
}
Write-Output '=== 24800 LISTENER ==='
$c = Get-NetTCPConnection -LocalPort 24800 -EA 0
if ($c) { $c | ForEach-Object { Write-Output ("{0}:{1} -> {2}:{3} {4}" -f $_.LocalAddress,$_.LocalPort,$_.RemoteAddress,$_.RemotePort,$_.State) } } else { Write-Output 'NO_LISTENER' }
Write-Output '=== HDC TARGETS ==='
& $Hdc list targets 2>&1 | ForEach-Object { Write-Output $_ }
Write-Output '=== FPORT ==='
& $Hdc -t $T fport ls 2>&1 | ForEach-Object { Write-Output $_ }
Write-Output '=== HARMONY PACKAGE ==='
$p = & $Hdc -t $T shell "pm list packages" 2>&1
$hits = @($p | Where-Object { $_ -match 'deskflow|ohospoc' })
Write-Output ("PKG_MATCH_COUNT=" + $hits.Count)
$hits | ForEach-Object { Write-Output ("PKG=" + $_.Trim()) }
Write-Output '=== FILES DIR ==='
& $Hdc -t $T shell "ls -la /data/app/el2/100/base/com.deskflow.ohospoc/haps/entry/files/Deskflow/" 2>&1 | ForEach-Object { Write-Output $_ }
Write-Output '=== ORIGINAL SERVER CONFIG ==='
if (Test-Path 'C:\ProgramData\Deskflow\deskflow-server.conf') { Get-Content 'C:\ProgramData\Deskflow\deskflow-server.conf' -Raw } else { Write-Output 'NO_CONFIG' }
Write-Output '=== DAEMON PARENT ==='
Get-CimInstance Win32_Process -Filter "ProcessId=6052" | ForEach-Object { Write-Output ("DAEMON PPID=" + $_.ParentProcessId) }
Get-CimInstance Win32_Process -Filter "ProcessId=1548" | ForEach-Object { Write-Output ("PPID1548 NAME=" + $_.Name + " EXE=" + $_.ExecutablePath) }
Write-Output 'PROBE_DONE'
