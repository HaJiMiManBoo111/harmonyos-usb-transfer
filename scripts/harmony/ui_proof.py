#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Wake + unlock the device, then bring the Flux Index page forward.

The device screen was locked, so the scene board owned the foreground window
and `uitest dumpLayout` returned the launcher instead of the app.
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
HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, 'ui-proof.log')
LAYOUT_JSON = os.path.join(HERE, 'ui-layout.json')
SHOT = os.path.join(HERE, 'ui-screen.png')


def hdc(*args, timeout=120):
    try:
        p = subprocess.run([HDC, '-t', TARGET] + list(args), capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 'TIMEOUT', 124
    return p.stdout.decode('utf-8', 'replace') + p.stderr.decode('utf-8', 'replace'), p.returncode


def ui(command):
    out, rc = hdc('shell', 'uitest uiInput %s' % command)
    return out.strip()


def dump_texts(lines, extra_args):
    out, _ = hdc('shell', 'uitest dumpLayout %s' % extra_args)
    lines.append('dumpLayout %s -> %s' % (extra_args, out.strip()))
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
    time.sleep(3)
    out, _ = hdc('shell', 'aa start -a EntryAbility -b %s' % BUNDLE)
    lines.append('aa start EntryAbility -> %s' % out.strip())
    time.sleep(8)

    out, _ = hdc('shell', 'ps -A')
    lines.append('--- ps -A (deskflow/ohospoc lines) ---')
    for line in out.splitlines():
        if 'deskflow' in line.lower() or 'ohospoc' in line.lower():
            lines.append(line)

    uniq = dump_texts(lines, '')
    lines.append('--- visible text nodes (%d) ---' % len(uniq))
    for t in uniq:
        lines.append('  %s' % t)

    checks = {
        'card title 接收的文件': any('接收的文件' in t for t in uniq),
        'file name e2e-payload.bin': any('e2e-payload.bin' in t for t in uniq),
        'file name e2e-payload (3).bin': any('e2e-payload (3).bin' in t for t in uniq),
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
    return 0


if __name__ == '__main__':
    sys.exit(main())
