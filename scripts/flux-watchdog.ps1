# Flux logon watchdog (persistent). Single autostart entry.
# ASCII only, no BOM (PowerShell 5.1 reads BOM-less UTF-8 as ANSI).
# Keeps two things true forever, every 20 s:
#   1) flux.exe is running and TCP 24800 is LISTENing (server core).
#   2) the hdc USB reverse tunnel tcp:24800 exists. It is transient: dropped
#      on device re-enumeration, sleep, or hdc server restart. This was the
#      "cannot connect after reboot" root cause.
# It deliberately does NOT launch the HarmonyOS ability or wake its screen:
# doing so every time the client idled popped the device window back up after
# the user minimized it. The device reconnects on its own (autoreconnect=true)
# as soon as the tunnel is back. A named mutex makes duplicate launch harmless.

param([int]$IntervalSec = 20)

$ErrorActionPreference = 'SilentlyContinue'

$created = $false
$mutex = New-Object System.Threading.Mutex($true, 'Global\FluxWatchdog2', [ref]$created)
if (-not $created) { Write-Output 'watchdog already running'; exit 0 }

$Dir     = 'C:\Users\Administrator\AppData\Local\Programs\Flux'
$Exe     = Join-Path $Dir 'flux.exe'
$Hdc     = 'C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
$Log     = 'C:\Users\Administrator\Desktop\Quick\_flux_upload\flux-watchdog.log'
$Port    = 24800
$Bundle  = 'com.deskflow.ohospoc'
$Ability = 'BackGroundAbility'

function Write-Log([string]$msg) {
    $line = "[{0}] {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg
    try { Add-Content -Path $Log -Value $line -Encoding ASCII } catch {}
}

function Get-Listen {
    # A bound server socket has RemotePort 0. The State enum is unreliable
    # for wildcard listeners on some Windows builds.
    return @(Get-NetTCPConnection -LocalPort $Port -EA 0 |
             Where-Object { $_.RemotePort -eq 0 })
}

function Get-Established {
    return @(Get-NetTCPConnection -LocalPort $Port -EA 0 |
             Where-Object { $_.RemotePort -ne 0 -and $_.State -eq 'Established' })
}

function Get-UsbTarget {
    if (-not (Test-Path -LiteralPath $Hdc)) { return $null }
    $v = (& $Hdc list targets -v 2>&1 | Out-String)
    foreach ($line in ($v -split "`r?`n")) {
        if ($line -match '^\s*(\S+)\s+.*USB') { return $Matches[1] }
    }
    $p = (& $Hdc list targets 2>&1 | Out-String)
    foreach ($line in ($p -split "`r?`n")) {
        $s = $line.Trim()
        if ($s -match '^[A-Za-z0-9._:-]+$') { return $s }
    }
    return $null
}

Write-Log '=== Flux watchdog started ==='
$lastRepair = Get-Date '2000-01-01'
$cycle = 0

while ($true) {
    $cycle++

    # Keep the log bounded.
    try {
        $fi = Get-Item -LiteralPath $Log -EA 0
        if ($fi -and $fi.Length -gt 400000) {
            $keep = @(Get-Content -LiteralPath $Log -Tail 200)
            [System.IO.File]::WriteAllText($Log, ($keep -join "`n") + "`n",
                                           [System.Text.Encoding]::ASCII)
        }
    } catch {}

    # --- 1) PC server ----------------------------------------------------
    $listen = Get-Listen
    $est = Get-Established
    if ($listen.Count -eq 0 -and $est.Count -eq 0) {
        if (-not (Get-Process -Name 'flux' -EA 0)) {
            if (Test-Path -LiteralPath $Exe) {
                Start-Process -FilePath $Exe -WorkingDirectory $Dir -WindowStyle Minimized
                Write-Log 'started flux.exe (no 24800 listener)'
            } else {
                Write-Log 'ERROR flux.exe missing'
            }
        } else {
            Write-Log 'flux.exe running but 24800 not listening yet'
        }
    }

    # --- 2) USB reverse tunnel (PC side only) ----------------------------
    # Do NOT touch the device here: no `aa start`, no `power-shell wakeup`.
    # Repeatedly launching the ability / waking the screen used to pop the
    # HarmonyOS window back up every minute after the user minimized it
    # (minimizing drops the client, which looked like "needs a refresh").
    # The device client already runs with autoreconnect=true; once the tunnel
    # is back its own reconnect loop reconnects without any window being shown.
    if (Test-Path -LiteralPath $Hdc) {
        $t = Get-UsbTarget
        if ($t) {
            $rev = (& $Hdc -t $t fport ls 2>&1 | Out-String)
            $hasTunnel = ($rev -match 'tcp:24800')
            if (-not $hasTunnel) {
                $now = Get-Date
                if (($now - $lastRepair).TotalSeconds -ge 45) {
                    Write-Log ('reverse tunnel missing for ' + $t + ', re-adding')
                    & $Hdc -t $t rport "tcp:$Port" "tcp:$Port" 2>&1 | Out-Null
                    Write-Log ('re-added reverse tunnel for ' + $t)
                    $lastRepair = $now
                }
            } elseif ($cycle % 15 -eq 0) {
                Write-Log 'healthy: reverse tunnel present (device reconnect is device-side)'
            }
        } elseif ($cycle % 15 -eq 0) {
            Write-Log 'no USB hdc target yet'
        }
    } elseif ($cycle % 15 -eq 0) {
        Write-Log 'hdc.exe not found'
    }

    Start-Sleep -Seconds $IntervalSec
}
