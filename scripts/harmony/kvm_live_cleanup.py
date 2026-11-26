# KVM live debug - stage D: restore installed version to its pre-debug state
import os, subprocess, sys
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

DIR = r'C:\Users\Administrator\AppData\Local\Programs\Flux'
CONF = os.path.join(DIR, 'settings', 'Flux.conf')
SRVCONF = os.path.join(DIR, 'settings', 'flux-server.conf')
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'kvm-live')
PIDFILE = os.path.join(OUT, 'core.pid')

# 1) stop the new core (only the PID we started)
pid = None
if os.path.exists(PIDFILE):
    try:
        pid = int(open(PIDFILE).read().strip())
    except Exception:
        pass
print('CORE_PID=' + str(pid))
if pid:
    p = subprocess.run(['powershell.exe', '-NoProfile', '-Command',
        "$p = Get-Process -Id %d -ErrorAction SilentlyContinue; if ($p) { "
        "if ($p.Path -like '*Flux*') { Stop-Process -Id %d -Force; 'STOPPED_BY_PATH' } "
        "else { 'SKIP_PATH_MISMATCH=' + $p.Path } } else { 'ALREADY_GONE' }" % (pid, pid)],
        capture_output=True, timeout=60)
    print('CORE_STOP=' + (p.stdout or b'').decode('utf-8', 'replace').strip())

# 2) restore portable conf to the installed default '[core]' and remove temp server config
with open(CONF, 'w', encoding='ascii', newline='\n') as f:
    f.write('[core]\n')
print('CONF_RESTORED=' + repr(open(CONF, 'r', encoding='ascii').read()))
if os.path.exists(SRVCONF):
    os.remove(SRVCONF)
    print('SRVCONF_REMOVED=True')
else:
    print('SRVCONF_REMOVED=already-absent')

# 3) verify original install still intact, no listener left by us
checks = [
    ('ORIGINAL_EXE', r'C:\Program Files\Deskflow\deskflow.exe'),
    ('ORIGINAL_CORE', r'C:\Program Files\Deskflow\deskflow-core.exe'),
    ('ORIGINAL_CONF', r'C:\Users\Administrator\AppData\Roaming\Deskflow\Deskflow.conf'),
    ('INSTALLED_EXE', os.path.join(DIR, 'flux.exe')),
]
for label, p in checks:
    print('%s=%s%s' % (label, os.path.exists(p), (' size=' + str(os.path.getsize(p))) if os.path.exists(p) else ''))

p = subprocess.run(['powershell.exe', '-NoProfile', '-Command',
    "Get-Process -Name deskflow,flux-core,flux-daemon -ErrorAction SilentlyContinue | "
    "ForEach-Object { \"$($_.ProcessName) PID=$($_.Id) PATH=$($_.Path)\" }; "
    "Write-Output '---LISTEN---'; "
    "Get-NetTCPConnection -LocalPort 24800 -State Listen -ErrorAction SilentlyContinue | "
    "ForEach-Object { \"$($_.LocalAddress):$($_.LocalPort) OWNER=$((Get-Process -Id $_.OwningProcess -EA 0).ProcessName)\" }"],
    capture_output=True, timeout=60)
print('PROCS_AND_LISTEN=' + (p.stdout or b'').decode('utf-8', 'replace').strip().replace('\n', ' | '))

# 4) keep debug evidence files (do not delete kvm-live dir)
print('EVIDENCE_DIR_KEPT=' + OUT)
print('CLEANUP_DONE=1')
