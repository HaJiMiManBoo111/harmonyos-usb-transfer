# KVM live debug - independent probe (no shell quoting issues)
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
        out = (p.stdout or b'').decode('utf-8', 'replace').strip()
        err = (p.stderr or b'').decode('utf-8', 'replace').strip()
        return p.returncode, out, err
    except Exception as e:
        return -1, '', 'EXC:' + str(e)

print('PIPE_EXISTS=' + str(os.path.exists(r'\\.\pipe\flux-core')))
try:
    s = socket.create_connection(('127.0.0.1', 24800), timeout=3)
    print('TCP24800_CONNECT=OK')
    s.close()
except Exception as e:
    print('TCP24800_CONNECT=FAIL ' + str(e))

print('--- CORE STDOUT ---')
p = os.path.join(OUT, 'core-stdout.log')
if os.path.exists(p):
    print(open(p, encoding='utf-8', errors='replace').read()[-3000:])
else:
    print('(missing)')
print('--- CORE STDERR ---')
p = os.path.join(OUT, 'core-stderr.log')
if os.path.exists(p):
    print(open(p, encoding='utf-8', errors='replace').read()[-1500:])
print('--- PID FILE ---')
p = os.path.join(OUT, 'core.pid')
print(open(p).read().strip() if os.path.exists(p) else '(missing)')

for label, args in [
    ('TARGETS', ['list', 'targets']),
    ('FPORT', ['-t', TARGET, 'fport', 'ls']),
    ('BM_DUMP', ['-t', TARGET, 'shell', 'bm', 'dump', '-n', 'com.deskflow.ohospoc']),
    ('PM_LIST', ['-t', TARGET, 'shell', 'pm', 'list', 'packages', 'com.deskflow.ohospoc']),
    ('PM_ALL', ['-t', TARGET, 'shell', 'pm', 'list', 'packages']),
]:
    rc, out, err = run(args)
    print('%s_RC=%s' % (label, rc))
    print('%s_OUT=%s' % (label, out[:1500].replace('\n', ' | ')))
    print('%s_ERR=%s' % (label, err[:500].replace('\n', ' | ')))

print('PROBE_DONE=1')
