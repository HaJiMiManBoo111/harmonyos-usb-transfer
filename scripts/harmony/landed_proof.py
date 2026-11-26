#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Capture the raw evidence of the files that landed on the HarmonyOS device."""

import os
import subprocess
import sys

HDC = r'C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
TARGET = '59C0226421001606'
BUNDLE = 'com.deskflow.ohospoc'
LAND = '/data/app/el2/100/base/%s/haps/entry/files/Deskflow' % BUNDLE
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'landed-proof.txt')

CMDS = [
    ('ls -l', 'ls -l %s' % LAND),
    ('md5sum', 'md5sum %s/*' % LAND),
    ('sha256sum', 'sha256sum %s/*' % LAND),
    ('stat', 'stat %s/*' % LAND),
    ('head -3', 'head -n 3 %s/e2e-payload.bin' % LAND),
    ('tail -2', 'tail -n 2 %s/e2e-payload.bin' % LAND),
]


def hdc(*args, timeout=60):
    p = subprocess.run([HDC, '-t', TARGET] + list(args), capture_output=True, timeout=timeout)
    return p.stdout.decode('utf-8', 'replace') + p.stderr.decode('utf-8', 'replace'), p.returncode


def main():
    lines = []
    for title, cmd in CMDS:
        out, rc = hdc('shell', cmd)
        lines.append('=== %s === (hdc shell "%s" rc=%s)' % (title, cmd, rc))
        lines.append(out.rstrip())
        lines.append('')
    text = '\n'.join(lines)
    with open(OUT, 'w', encoding='utf-8', errors='replace') as fh:
        fh.write(text)
    print('wrote %s' % OUT)
    return 0


if __name__ == '__main__':
    sys.exit(main())
