#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Definitive UI end-to-end proof for the HarmonyOS Flux client.

Order matters: the Index page is started and verified *before* the transfer,
because commonEvent notifications are transient -- a page instance created
after the event was published never sees it.

The screen-sharing authorisation popup raised by BackGroundAbility steals the
foreground window, so it is dismissed by tapping its "不允许" button (found in
the dumped layout tree) before the final dump.
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
PORT = int(os.environ.get('DF_PORT', '24805'))
HERE = os.path.dirname(os.path.abspath(__file__))
PAYLOAD = os.path.join(HERE, 'e2e-payload.bin')
SRVLOG = os.path.join(HERE, 'fake-server-final.log')
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


def dump_layout(lines, tag):
    """Return (list of text nodes, raw json) for the current foreground window."""
    out, _ = hdc('shell', 'uitest dumpLayout')
    lines.append('[%s] dumpLayout -> %s' % (tag, out.strip()))
    m = re.search(r'(/data/local/tmp/layout_[0-9]+\.json)', out)
    if not m:
        return [], {}
    raw, rc = hdc('shell', 'cat %s' % m.group(1))
    lines.append('[%s] cat rc=%s bytes=%d' % (tag, rc, len(raw)))
    with open(LAYOUT_JSON, 'w', encoding='utf-8', errors='replace') as fh:
        fh.write(raw)
    texts = []
    nodes = []

    def walk(node):
        if isinstance(node, dict):
            for key in ('text', 'value'):
                val = node.get(key)
                if isinstance(val, str) and val.strip():
                    texts.append(val)
                    nodes.append((val, node.get('bounds')))
            for child in node.values():
                walk(child)
        elif isinstance(node, list):
            for child in node:
                walk(child)

    data = {}
    try:
        data = json.loads(raw)
    except Exception as exc:
        lines.append('[%s] json parse fail: %r' % (tag, exc))
    walk(data)
    uniq = []
    for t in texts:
        if t not in uniq:
            uniq.append(t)
    lines.append('[%s] visible text nodes (%d) ---' % (tag, len(uniq)))
    for t in uniq:
        lines.append('  %s' % t)
    return uniq, nodes


def dismiss_popup(lines, nodes):
    """Tap the popup's negative button so the app window comes back to front."""
    target = None
    for text, bounds in nodes:
        if text.strip() in ('不允许', '取消') and bounds:
            target = (text, bounds)
            break
    if not target:
        lines.append('[popup] no dismiss button found in dump')
        return False
    text, bounds = target
    nums = [int(v) for v in re.findall(r'-?\d+', str(bounds))]
    if len(nums) < 4:
        lines.append('[popup] cannot parse bounds %r' % (bounds,))
        return False
    x1, y1, x2, y2 = nums[0], nums[1], nums[2], nums[3]
    cx, cy = (x1 + x2) // 2, (y1 + y2) // 2
    lines.append('[popup] tapping %r bounds=%r -> click %d %d' % (text, bounds, cx, cy))
    lines.append('[popup] click -> %s' % ui('click %d %d' % (cx, cy)))
    time.sleep(3)
    return True


def main():
    lines = []

    lines.append('wake -> %s' % ui('keyEvent Power'))
    time.sleep(2)
    lines.append('swipe up -> %s' % ui('swipe 1560 1800 1560 400 800'))
    time.sleep(3)

    out, _ = hdc('shell', 'aa start -a EntryAbility -b %s' % BUNDLE)
    lines.append('aa start EntryAbility -> %s' % out.strip())
    time.sleep(8)
    pre, _ = dump_layout(lines, 'BEFORE')
    lines.append('--- page alive before transfer: %s ---' %
                 ('YES' if any('接收的文件' in t for t in pre) else 'NO'))

    out, _ = hdc('rport', 'tcp:%d' % PORT, 'tcp:%d' % PORT)
    lines.append('rport tcp:%d -> %s' % (PORT, out.strip()))

    srv = subprocess.Popen(
        [sys.executable, os.path.join(HERE, 'fake_df_server.py'),
         '--port', str(PORT), '--file', PAYLOAD],
        stdout=open(SRVLOG, 'w', encoding='utf-8'), stderr=subprocess.STDOUT)
    time.sleep(2)

    out, _ = hdc('shell', 'aa start -a BackGroundAbility -b %s --ps host 127.0.0.1 '
                 '--ps port %d --ps screen HarmonyPC --ps autoreconnect true' % (BUNDLE, PORT))
    lines.append('aa start BackGroundAbility -> %s' % out.strip())

    deadline = time.time() + 45
    while time.time() < deadline:
        time.sleep(2)
        try:
            body = open(SRVLOG, 'r', encoding='utf-8', errors='replace').read()
        except OSError:
            body = ''
        if 'DONE' in body:
            break
    lines.append('--- fake server log ---')
    with open(SRVLOG, 'r', encoding='utf-8', errors='replace') as fh:
        lines.append(fh.read().rstrip())

    out, _ = hdc('shell', 'ls -l %s' % LAND)
    lines.append('--- ls -l %s ---' % LAND)
    lines.append(out.rstrip())
    out, _ = hdc('shell', 'md5sum %s/*' % LAND)
    lines.append('--- md5sum ---')
    lines.append(out.rstrip())

    # The injection authorisation popup owns the foreground window; dismiss it.
    _, nodes = dump_layout(lines, 'POPUP')
    dismiss_popup(lines, nodes)
    out, _ = hdc('shell', 'aa start -a EntryAbility -b %s' % BUNDLE)
    lines.append('aa start EntryAbility (again) -> %s' % out.strip())
    time.sleep(4)

    post, _ = dump_layout(lines, 'AFTER')

    checks = {
        'card title 接收的文件': any('接收的文件' in t for t in post),
        'file name e2e-payload.bin': any('e2e-payload.bin' in t for t in post),
        'file entry with byte size': any(re.search(r'e2e-payload[^|]*\d+ B', t) for t in post),
        'counter 共 N 个': any(re.search(r'共\s*\d+\s*个', t) for t in post),
        'counter 共 1 个': any(re.search(r'共\s*1\s*个', t) for t in post),
        'status 已接收文件': any('已接收文件' in t for t in post),
    }
    lines.append('--- assertions (AFTER) ---')
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
