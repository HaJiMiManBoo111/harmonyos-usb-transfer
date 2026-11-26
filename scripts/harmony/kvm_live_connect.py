# KVM live debug - stage B: connect harmony client to the new core
import os, re, subprocess, sys, time, json
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

HDC = r'C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
TARGET = '59C0226421001606'
BUNDLE = 'com.deskflow.ohospoc'
ABILITY = 'BackGroundAbility'
PORT = 24800
TMP = '/data/local/tmp/df_kvm_live.json'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'kvm-live')

def run(args, timeout=90):
    try:
        p = subprocess.run([HDC] + args, capture_output=True, timeout=timeout)
        return p.returncode, (p.stdout or b'').decode('utf-8', 'replace').strip(), (p.stderr or b'').decode('utf-8', 'replace').strip()
    except Exception as e:
        return -1, '', 'EXC:' + str(e)

def sh(cmd, timeout=90):
    return run(['-t', TARGET, 'shell', cmd], timeout)

def log(tag, rc, out, err=''):
    print('%s_RC=%s' % (tag, rc))
    print('%s_OUT=%s' % (tag, out[:1200].replace('\n', ' | ')))
    if err: print('%s_ERR=%s' % (tag, err[:400].replace('\n', ' | ')))

# 0) ensure reverse port forward exists
rc, out, err = run(['-t', TARGET, 'fport', 'ls'])
log('FPORT_BEFORE', rc, out, err)
if 'Reverse' not in out:
    rc, out, err = run(['-t', TARGET, 'rport', 'tcp:%d' % PORT, 'tcp:%d' % PORT])
    log('RPORT_ADD', rc, out, err)
    rc, out, err = run(['-t', TARGET, 'fport', 'ls'])
    log('FPORT_AFTER', rc, out, err)

# 1) wake screen, then start ability
rc, out, err = sh('power-shell wakeup')
log('WAKEUP', rc, out, err)
time.sleep(0.8)
start_cmd = ('aa start -a %s -b %s --ps host 127.0.0.1 --ps port %d '
             '--ps screen HarmonyPC --ps autoreconnect true' % (ABILITY, BUNDLE, PORT))
rc, out, err = sh(start_cmd)
log('AA_START', rc, out, err)
time.sleep(2)

# 2) dump layout, look for the injection permission dialog, click 允许
for attempt in range(1, 9):
    rc, out, err = sh('uitest dumpLayout -p %s' % TMP, timeout=90)
    local = os.path.join(OUT, 'layout.json')
    rc2, out2, err2 = run(['-t', TARGET, 'file', 'recv', TMP, local], timeout=90)
    raw = ''
    if os.path.exists(local):
        raw = open(local, encoding='utf-8', errors='replace').read()
    has_dialog = ('允许使用您的屏幕' in raw) or ('允许' in raw and 'bounds' in raw)
    print('DIALOG_TRY=%d DUMP_RC=%s RECV_RC=%s RAW_LEN=%d HAS=%s' %
          (attempt, rc, rc2, len(raw), has_dialog))
    if has_dialog:
        # find the node containing 允许 and its bounds
        m = None
        for mm in re.finditer(r'\{[^{}]*"text"\s*:\s*"[^"]*允许[^{}]*\}', raw):
            if '"bounds"' in mm.group(0):
                m = mm
                break
        if m:
            bd = re.search(r'"bounds"\s*:\s*"\[(\d+),(\d+)\]\[(\d+),(\d+)\]"', m.group(0))
            if bd:
                x1, y1, x2, y2 = [int(v) for v in bd.groups()]
                cx, cy = (x1 + x2) // 2, (y1 + y2) // 2
                rc3, out3, err3 = sh('uitest uiInput click %d %d' % (cx, cy))
                log('CLICK_ALLOW', rc3, out3, err3)
                print('CLICKED_AT=%d,%d' % (cx, cy))
                time.sleep(1.5)
                break
        # fallback: dump raw snippet for diagnosis
        print('RAW_SNIPPET=' + raw[:600].replace('\n', ' '))
    time.sleep(1.5)

# 3) check device process + connection list
rc, out, err = sh('ps -A')
ps_lines = [l for l in out.splitlines() if 'deskflow' in l.lower() or 'ohospoc' in l.lower()]
print('DEVICE_PS=' + (' | '.join(ps_lines))[:800])

# 4) check Windows-side ESTABLISHED TCP on 24800
try:
    p = subprocess.run(['powershell.exe', '-NoProfile', '-Command',
        "Get-NetTCPConnection -LocalPort 24800 -ErrorAction SilentlyContinue | ForEach-Object { \"$($_.State) $($_.LocalAddress):$($_.LocalPort)<-$($_.RemoteAddress):$($_.RemotePort)\" }"],
        capture_output=True, timeout=60)
    print('TCP24800=' + (p.stdout or b'').decode('utf-8', 'replace').strip().replace('\n', ' | '))
except Exception as e:
    print('TCP_EXC=' + str(e))

# 5) core log tail (look for "client ... has connected")
p = os.path.join(OUT, 'core-stdout.log')
if os.path.exists(p):
    txt = open(p, encoding='utf-8', errors='replace').read()
    print('CORE_LOG_TAIL=' + ' | '.join(txt.splitlines()[-14:]))
    print('CORE_HAS_CONNECTED=' + str('has connected' in txt))

print('STAGE_B_DONE=1')
