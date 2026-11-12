# KVM live debug - stage A: preflight + start new core detached + wait for listener
# All ASCII. Run: powershell -NoProfile -ExecutionPolicy Bypass -File kvm_live_start_core.ps1
$ErrorActionPreference = 'Stop'

$Dir   = 'C:\Users\Administrator\AppData\Local\Programs\Flux'
$Core  = Join-Path $Dir 'flux-core.exe'
$Conf  = Join-Path $Dir 'settings\Flux.conf'
$SConf = Join-Path $Dir 'settings\flux-server.conf'
$Out   = 'C:\Users\Administrator\Desktop\Quick\deskflow-lingxi\scripts\harmony\kvm-live'
$PidFile = Join-Path $Out 'core.pid'
$Stdout  = Join-Path $Out 'core-stdout.log'
$Stderr  = Join-Path $Out 'core-stderr.log'

Write-Output ('CORE_EXISTS=' + (Test-Path $Core))
Write-Output ('CONF_EXISTS=' + (Test-Path $Conf))
Write-Output ('SRVCONF_EXISTS=' + (Test-Path $SConf))
if (-not (Test-Path $Core)) { throw 'missing core exe' }

# preflight: no listener on 24800, no other core process
$listen = Get-NetTCPConnection -LocalPort 24800 -State Listen -ErrorAction SilentlyContinue
Write-Output ('PRE_LISTENER=' + [bool]$listen)
$cores = @(Get-Process -Name 'flux-core' -ErrorAction SilentlyContinue)
Write-Output ('PRE_CORE_PROCS=' + $cores.Count)

# exact package check on device
$Hdc = 'C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
$Target = '59C0226421001606'
$pkg = & $Hdc -t $Target shell 'pm list packages com.deskflow.ohospoc' 2>&1
Write-Output ('PKG_EXACT=' + ($pkg -join ' ').Trim())
Write-Output ('HDC_TARGETS=' + ((& $Hdc list targets 2>&1) -join ' ').Trim())
Write-Output ('FPORT=' + ((& $Hdc -t $Target fport ls 2>&1) -join ' | ').Trim())

Write-Output ('CONF_NOW=' + ((Get-Content $Conf -Raw) -replace "`r?`n", '\n'))
Write-Output ('SRVCONF_NOW=' + ((Get-Content $SConf -Raw) -replace "`r?`n", '\n'))

# start detached
$args = @('server', '--new-instance')
try {
  $p = Start-Process -FilePath $Core -ArgumentList $args -WorkingDirectory $Dir -PassThru -WindowStyle Hidden -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
} catch {
  Write-Output ('REDIRECT_FAILED=' + $_.Exception.Message)
  $p = Start-Process -FilePath $Core -ArgumentList $args -WorkingDirectory $Dir -PassThru -WindowStyle Hidden
}
Set-Content -Path $PidFile -Value $p.Id -Encoding ASCII
Write-Output ('CORE_PID=' + $p.Id)

# wait up to 20s for TCP 24800 LISTEN
$ok = $false
for ($i = 0; $i -lt 40; $i++) {
  Start-Sleep -Milliseconds 500
  $l = Get-NetTCPConnection -LocalPort 24800 -State Listen -ErrorAction SilentlyContinue
  if ($l) { $ok = $true; break }
  if ($p.HasExited) { break }
}
Write-Output ('LISTENER_UP=' + $ok)
Write-Output ('CORE_EXITED=' + $p.HasExited)
if ($p.HasExited) { Write-Output ('CORE_EXITCODE=' + $p.ExitCode) }
Write-Output ('PIPE_EXISTS=' + (Test-Path '\\.\pipe\flux-core'))

# connections on 24800
$conns = Get-NetTCPConnection -LocalPort 24800 -ErrorAction SilentlyContinue | ForEach-Object { "$($_.LocalAddress):$($_.LocalPort)<-$($_.RemoteAddress):$($_.RemotePort) $($_.State)" }
Write-Output ('CONNS=' + ($conns -join ' | '))
Write-Output ('STDOUT_HEAD=' + ((Get-Content $Stdout -TotalCount 20 -ErrorAction SilentlyContinue) -join ' | '))
Write-Output ('STDERR_HEAD=' + ((Get-Content $Stderr -TotalCount 20 -ErrorAction SilentlyContinue) -join ' | '))
Write-Output 'STAGE_A_DONE=1'
