# KVM live debug - quick state check (no side effects)
import os, socket, subprocess, sys
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

HDC = r'C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
TARGET = '59C0226421001606'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'kvm-live')

def run(args, timeout=60):
    try:
        p = subprocess.run([HDC] + args, capture_output=True, timeout=timeout)
        return p.returncode, (p.stdout or b'').decode('utf-8', 'replace').strip(), (p.stderr or b'').decode('utf-8', 'replace').strip()
    except Exception as e:
        return -1, '', 'EXC:' + str(e)

print('PIPE_EXISTS=' + str(os.path.exists(r'\\.\pipe\flux-core')))
try:
    s = socket.create_connection(('127.0.0.1', 24800), timeout=3); s.close()
    print('TCP24800_CONNECT=OK')
except Exception as e:
    print('TCP24800_CONNECT=FAIL ' + str(e))

print('--- CORE STDOUT (tail) ---')
p = os.path.join(OUT, 'core-stdout.log')
if os.path.exists(p):
    print(open(p, encoding='utf-8', errors='replace').read()[-4000:])
else:
    print('(missing)')

print('--- HDC SHELL PROBES ---')
for label, cmd in [
    ('PS', 'ps -A'),
    ('LS', 'ls -l /data/app/el2/100/base/com.deskflow.ohospoc/haps/entry/files/Deskflow/'),
    ('BM', 'bm dump -n com.deskflow.ohospoc'),
]:
    rc, out, err = run(['-t', TARGET, 'shell', cmd])
    lines = out.splitlines()
    if label == 'PS':
        lines = [l for l in lines if 'deskflow' in l.lower() or 'ohospoc' in l.lower()]
    print('%s_RC=%s OUT=%s' % (label, rc, (' | '.join(lines))[:2000]))
    if err: print('%s_ERR=%s' % (label, err[:300]))

print('--- TCP 24800 CONNECTIONS ---')
try:
    p = subprocess.run(['powershell.exe', '-NoProfile', '-Command',
        "Get-NetTCPConnection -LocalPort 24800 -ErrorAction SilentlyContinue | ForEach-Object { \"$($_.State) $($_.LocalAddress):$($_.LocalPort)<-$($_.RemoteAddress):$($_.RemotePort) OWNER=$((Get-Process -Id $_.OwningProcess -EA 0).ProcessName)\" }"],
        capture_output=True, timeout=60)
    print((p.stdout or b'').decode('utf-8', 'replace').strip())
except Exception as e:
    print('PS_EXC=' + str(e))

print('STATE_DONE=1')
