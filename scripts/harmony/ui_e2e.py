#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Full UI end-to-end proof.

1. wake + unlock the device (the launcher otherwise owns the foreground)
2. foreground the Flux Index page
3. run one DDRG + DFTR transfer from the fake server
4. dump the ArkUI layout and check the "接收的文件" card shows the file
5. capture the landing directory listing / checksums
"""

import json
import os
import re
import subprocess
import sys
import time

HDC = r'C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
TARGET = '59C0226421001606'
BUNDLE = 'com.deskflow.ohospoc'
PORT = int(os.environ.get('DF_PORT', '24802'))
HERE = os.path.dirname(os.path.abspath(__file__))
PAYLOAD = os.path.join(HERE, 'e2e-payload.bin')
SRVLOG = os.path.join(HERE, 'fake-server-ui.log')
LOG = os.path.join(HERE, 'ui-proof.log')
LAYOUT_JSON = os.path.join(HERE, 'ui-layout.json')
SHOT = os.path.join(HERE, 'ui-screen.png')
LAND = '/data/app/el2/100/base/%s/haps/entry/files/Deskflow' % BUNDLE


def hdc(*args, timeout=120):
    try:
        p = subprocess.run([HDC, '-t', TARGET] + list(args), capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 'TIMEOUT', 124
    return p.stdout.decode('utf-8', 'replace') + p.stderr.decode('utf-8', 'replace'), p.returncode


def ui(command):
    out, _ = hdc('shell', 'uitest uiInput %s' % command)
    return out.strip()


def dump_texts(lines):
    out, _ = hdc('shell', 'uitest dumpLayout')
    lines.append('dumpLayout -> %s' % out.strip())
    m = re.search(r'(/data/local/tmp/layout_[0-9]+\.json)', out)
    if not m:
        return []
    raw, rc = hdc('shell', 'cat %s' % m.group(1))
    lines.append('cat rc=%s bytes=%d' % (rc, len(raw)))
    with open(LAYOUT_JSON, 'w', encoding='utf-8', errors='replace') as fh:
        fh.write(raw)
    texts = []

    def walk(node):
        if isinstance(node, dict):
            for key in ('text', 'value'):
                val = node.get(key)
                if isinstance(val, str) and val.strip():
                    texts.append(val)
            for child in node.values():
                walk(child)
        elif isinstance(node, list):
            for child in node:
                walk(child)

    try:
        walk(json.loads(raw))
    except Exception as exc:
        lines.append('json parse fail: %r' % exc)
    uniq = []
    for t in texts:
        if t not in uniq:
            uniq.append(t)
    return uniq


def main():
    lines = []

    lines.append('wake -> %s' % ui('keyEvent Power'))
    time.sleep(2)
    lines.append('swipe up -> %s' % ui('swipe 1560 1800 1560 400 800'))
    time.sleep(3)

    out, _ = hdc('shell', 'aa force-stop %s' % BUNDLE)
    lines.append('aa force-stop -> %s' % out.strip().replace('\n', ' | '))
    time.sleep(2)

    out, _ = hdc('rport', 'tcp:%d' % PORT, 'tcp:%d' % PORT)
    lines.append('rport tcp:%d -> %s' % (PORT, out.strip()))

    srv = subprocess.Popen(
        [sys.executable, os.path.join(HERE, 'fake_df_server.py'),
         '--port', str(PORT), '--file', PAYLOAD],
        stdout=open(SRVLOG, 'w', encoding='utf-8'), stderr=subprocess.STDOUT)
    time.sleep(2)

    out, _ = hdc('shell', 'aa start -a EntryAbility -b %s' % BUNDLE)
    lines.append('aa start EntryAbility -> %s' % out.strip())
    time.sleep(6)

    out, _ = hdc('shell', 'aa start -a BackGroundAbility -b %s --ps host 127.0.0.1 '
                 '--ps port %d --ps screen HarmonyPC --ps autoreconnect true' % (BUNDLE, PORT))
    lines.append('aa start BackGroundAbility -> %s' % out.strip())
    time.sleep(18)

    lines.append('--- fake server log ---')
    with open(SRVLOG, 'r', encoding='utf-8', errors='replace') as fh:
        lines.append(fh.read().rstrip())

    out, _ = hdc('shell', 'ls -l %s' % LAND)
    lines.append('--- ls -l %s ---' % LAND)
    lines.append(out.rstrip())
    out, _ = hdc('shell', 'md5sum %s/*' % LAND)
    lines.append('--- md5sum ---')
    lines.append(out.rstrip())

    uniq = dump_texts(lines)
    lines.append('--- visible text nodes (%d) ---' % len(uniq))
    for t in uniq:
        lines.append('  %s' % t)

    checks = {
        'card title 接收的文件': any('接收的文件' in t for t in uniq),
        'file name e2e-payload.bin': any('e2e-payload.bin' in t for t in uniq),
        'counter 共 N 个': any(re.search(r'共\s*\d+\s*个', t) for t in uniq),
        'status 已接收文件': any('已接收文件' in t for t in uniq),
        'empty placeholder 暂无收到的文件': any('暂无收到的文件' in t for t in uniq),
    }
    lines.append('--- assertions ---')
    for name, ok in checks.items():
        lines.append('%-34s %s' % (name, 'YES' if ok else 'no'))

    out, rc = hdc('shell', 'uitest screenCap -p /data/local/tmp/deskflow-ui.png')
    lines.append('screenCap -> %s (rc=%s)' % (out.strip(), rc))
    p = subprocess.run([HDC, '-t', TARGET, 'file', 'recv', '/data/local/tmp/deskflow-ui.png', SHOT],
                       capture_output=True, timeout=180)
    lines.append('recv screenshot rc=%s %s' % (p.returncode, (p.stdout + p.stderr).decode('utf-8', 'replace').strip()))

    with open(LOG, 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(lines))
    print('wrote %s' % LOG)
    srv.terminate()
    return 0


if __name__ == '__main__':
    sys.exit(main())
