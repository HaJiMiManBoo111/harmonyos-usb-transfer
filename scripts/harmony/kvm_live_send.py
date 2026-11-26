# KVM live debug - stage C: trigger sendFiles over named pipe, then verify landing
import ctypes, hashlib, os, re, subprocess, sys, time
from ctypes import wintypes
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

HDC = r'C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
TARGET = '59C0226421001606'
BUNDLE = 'com.deskflow.ohospoc'
LAND = '/data/app/el2/100/base/%s/haps/entry/files/Deskflow' % BUNDLE
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'kvm-live')
SMALL = os.path.join(OUT, 'kvm-live-small.bin')
MULTI = os.path.join(OUT, 'kvm-live-multi.bin')
PIPE = r'\\.\pipe\flux-core'

def hdc(args, timeout=120):
    try:
        p = subprocess.run([HDC] + args, capture_output=True, timeout=timeout)
        return p.returncode, (p.stdout or b'').decode('utf-8', 'replace').strip(), (p.stderr or b'').decode('utf-8', 'replace').strip()
    except Exception as e:
        return -1, '', 'EXC:' + str(e)

# ---- source file fingerprints ----
src = {}
for p in (SMALL, MULTI):
    b = open(p, 'rb').read()
    src[os.path.basename(p)] = (len(b), hashlib.md5(b).hexdigest(), hashlib.sha256(b).hexdigest())
    print('SRC|%s|%d|%s|%s' % (os.path.basename(p), len(b), src[os.path.basename(p)][1], src[os.path.basename(p)][2]))

# ---- connect named pipe ----
k32 = ctypes.windll.kernel32
GENERIC_READ, GENERIC_WRITE, OPEN_EXISTING = 0x80000000, 0x40000000, 3
INVALID = ctypes.c_void_p(-1).value
h = k32.CreateFileW(PIPE, GENERIC_READ | GENERIC_WRITE, 0, None, OPEN_EXISTING, 0, None)
if h == INVALID or h == 0:
    print('PIPE_CONNECT=FAIL GetLastError=%d' % k32.GetLastError())
    sys.exit(2)
print('PIPE_CONNECT=OK handle=%s' % h)

def write(msg):
    data = msg.encode('utf-8')
    n = wintypes.DWORD()
    ok = k32.WriteFile(h, data, len(data), ctypes.byref(n), None)
    return ok, n.value

def drain(seconds):
    collected = []
    end = time.time() + seconds
    while time.time() < end:
        avail = wintypes.DWORD()
        total = wintypes.DWORD()
        if not k32.PeekNamedPipe(h, None, 0, None, ctypes.byref(avail), ctypes.byref(total)):
            break
        if avail.value:
            buf = ctypes.create_string_buffer(avail.value)
            got = wintypes.DWORD()
            if k32.ReadFile(h, buf, avail.value, ctypes.byref(got), None):
                collected.append(buf.raw[:got.value].decode('utf-8', 'replace'))
        time.sleep(0.2)
    return collected

# ---- sendFiles command ----
sep = '\x1f'
cmd = 'sendFiles=' + SMALL + sep + MULTI + '\n'
ok, n = write(cmd)
print('WRITE_OK=%s BYTES=%d CMD=%r' % (bool(ok), n, cmd))
resp = drain(12)
print('PIPE_RESPONSES=' + ' || '.join(x.replace('\n', '\\n') for x in resp))
progress = [l for l in '\n'.join(resp).splitlines() if 'fileTransferProgress' in l]
print('PROGRESS_LINES=' + str(len(progress)))
for l in progress: print('PROGRESS=' + l)

k32.CloseHandle(h)
time.sleep(2)

# ---- core log check ----
p = os.path.join(OUT, 'core-stdout.log')
if os.path.exists(p):
    txt = open(p, encoding='utf-8', errors='replace').read()
    tail = txt.splitlines()[-30:]
    print('CORE_LOG_TAIL=' + ' | '.join(tail))
    print('NO_TARGET_WARN=' + str('no connected client supports file transfer' in txt))

# ---- device landing check ----
rc, out, err = hdc(['-t', TARGET, 'shell', 'ls -l %s' % LAND])
print('DEVICE_LS_RC=%s' % rc)
print('DEVICE_LS=' + out[:2500].replace('\n', ' | '))
if err: print('DEVICE_LS_ERR=' + err[:300])

for name in ('kvm-live-small.bin', 'kvm-live-multi.bin'):
    remote = LAND + '/' + name
    local = os.path.join(OUT, 'recv-' + name)
    rc, out2, err2 = hdc(['-t', TARGET, 'file', 'recv', remote, local])
    print('RECV|%s|RC=%s|OUT=%s|ERR=%s' % (name, rc, out2[:200].replace('\n', ' '), err2[:200].replace('\n', ' ')))
    if os.path.exists(local):
        b = open(local, 'rb').read()
        md5, sha = hashlib.md5(b).hexdigest(), hashlib.sha256(b).hexdigest()
        match = (len(b), md5, sha) == src.get(name)
        print('VERIFY|%s|size=%d|md5=%s|sha256=%s|MATCH=%s' % (name, len(b), md5, sha, match))
    else:
        print('VERIFY|%s|MISSING_LOCAL' % name)

print('STAGE_C_DONE=1')
