#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""UI-level proof for the HarmonyOS Flux client.

Brings the Index page to the foreground, runs one DFTR/DDRG transfer from the
fake server and then checks that the page shows the received file.

Everything is driven through hdc; no cmd.exe quoting involved.
"""

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
LAYOUT = os.path.join(HERE, 'ui-layout-raw.txt')
LAND_DIR = '/data/app/el2/100/base/%s/haps/entry/files/Deskflow' % BUNDLE


def hdc(*args, timeout=60):
    cmd = [HDC, '-t', TARGET] + list(args)
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 'TIMEOUT', 124
    out = p.stdout.decode('utf-8', 'replace') + p.stderr.decode('utf-8', 'replace')
    return out.strip(), p.returncode


def step(title, *args, **kw):
    out, rc = hdc(*args, **kw)
    print('=== %s ===' % title)
    print(out)
    print('--- rc=%s' % rc)
    return out


def main():
    if not os.path.exists(PAYLOAD):
        print('missing payload %s' % PAYLOAD)
        return 2

    step('clean landing dir', 'shell', 'rm -f %s/*' % LAND_DIR)
    step('landing dir before', 'shell', 'ls -l %s' % LAND_DIR)
    step('reverse port', 'rport', 'tcp:%d' % PORT, 'tcp:%d' % PORT)

    srv = subprocess.Popen(
        [sys.executable, os.path.join(HERE, 'fake_df_server.py'),
         '--port', str(PORT), '--file', PAYLOAD],
        stdout=open(SRVLOG, 'w', encoding='utf-8'), stderr=subprocess.STDOUT)
    time.sleep(2)

    step('start Index page (EntryAbility)', 'shell',
         'aa start -a EntryAbility -b %s' % BUNDLE)
    time.sleep(5)

    step('start background ability against fake server', 'shell',
         'aa start -a BackGroundAbility -b %s --ps host 127.0.0.1 --ps port %d '
         '--ps screen HarmonyPC --ps autoreconnect true' % (BUNDLE, PORT))
    time.sleep(15)

    print('=== fake server log ===')
    with open(SRVLOG, 'r', encoding='utf-8', errors='replace') as fh:
        print(fh.read())

    step('landing dir after', 'shell', 'ls -l %s' % LAND_DIR)
    step('md5', 'shell', 'md5sum %s/*' % LAND_DIR)

    out, rc = hdc('shell', 'uitest dumpLayout', timeout=90)
    with open(LAYOUT, 'w', encoding='utf-8', errors='replace') as fh:
        fh.write(out)
    print('=== uitest dumpLayout rc=%s len=%d ===' % (rc, len(out)))
    print(out[:1500])

    print('=== UI assertions ===')
    checks = {
        'card title 接收的文件': '接收的文件' in out,
        'file name e2e-payload.bin': 'e2e-payload.bin' in out,
        'counter 共 1 个': ('共 1 个' in out) or ('共1个' in out),
        'status 已接收文件': '已接收文件' in out,
        'empty placeholder 暂无收到的文件': '暂无收到的文件' in out,
    }
    for name, ok in checks.items():
        print('%-34s %s' % (name, 'YES' if ok else 'no'))
    # dump every text-ish node so the raw evidence is in the log
    for m in re.finditer(r'text="([^"]{0,80})"', out):
        if m.group(1).strip():
            print('  node text: %s' % m.group(1))

    srv.terminate()
    return 0


if __name__ == '__main__':
    sys.exit(main())
