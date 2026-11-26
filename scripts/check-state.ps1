$ErrorActionPreference = 'SilentlyContinue'
Write-Output '--- all deskflow-related processes ---'
Get-CimInstance Win32_Process | Where-Object { $_.Name -like '*deskflow*' -or $_.CommandLine -like '*Flux*' } | ForEach-Object {
    Write-Output ($_.Name + ' PID=' + $_.ProcessId + ' PPID=' + $_.ParentProcessId + ' EXE=' + $_.ExecutablePath)
}
Write-Output '--- port 24800 ---'
$c = Get-NetTCPConnection -LocalPort 24800 -ErrorAction SilentlyContinue
if ($c) { $c | ForEach-Object { Write-Output ($_.State + ' ' + $_.OwningProcess) } } else { Write-Output 'NO_LISTENER' }
Write-Output '--- original config content ---'
$OrigConf = Join-Path $env:APPDATA 'Deskflow\Deskflow.conf'
Get-Content $OrigConf | Select-Object -First 20 | ForEach-Object { Write-Output ('CFG| ' + $_) }
Write-Output '--- installed exe versions (fresh) ---'
$T = Join-Path $env:LOCALAPPDATA 'Programs\Flux'
Push-Location $T
& '.\flux.exe' --version 2>&1 | Select-Object -First 4 | ForEach-Object { Write-Output ('GUI| ' + $_) }
Write-Output ('GUI_EXIT=' + $LASTEXITCODE)
& '.\flux-core.exe' --version 2>&1 | Select-Object -First 4 | ForEach-Object { Write-Output ('CORE| ' + $_) }
Write-Output ('CORE_EXIT=' + $LASTEXITCODE)
Pop-Location
$c2 = Join-Path $T 'settings\Flux.conf'
Start-Sleep -Milliseconds 500
Write-Output ('CONF_AFTER_VERSION=' + ((Get-Content $c2 -Raw) -replace "`r`n", '|'))
Set-Content -Path $c2 -Encoding ASCII -Value '[core]'
Write-Output 'STATE_OK=1'
