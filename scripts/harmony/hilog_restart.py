#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Capture hilog while the Flux app is restarted, to see why the Index
page does not render after a force-stop."""

import os
import re
import subprocess
import sys
import time

HDC = r'C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
TARGET = '59C0226421001606'
BUNDLE = 'com.deskflow.ohospoc'
HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, 'hilog-restart.log')


def hdc(*args, timeout=90):
    p = subprocess.run([HDC, '-t', TARGET] + list(args), capture_output=True, timeout=timeout)
    return p.stdout.decode('utf-8', 'replace') + p.stderr.decode('utf-8', 'replace'), p.returncode


def main():
    stream = subprocess.Popen([HDC, '-t', TARGET, 'shell', 'hilog'],
                               stdout=open(LOG, 'wb'), stderr=subprocess.STDOUT)
    time.sleep(3)
    hdc('shell', 'aa force-stop %s' % BUNDLE)
    time.sleep(2)
    hdc('shell', 'aa start -a EntryAbility -b %s' % BUNDLE)
    time.sleep(12)
    stream.terminate()
    try:
        stream.wait(timeout=20)
    except Exception:
        stream.kill()

    raw = open(LOG, 'rb').read().decode('utf-8', 'replace')
    hits = [ln for ln in raw.splitlines()
            if any(k in ln for k in ('testTag', 'loadContent', 'Flux', 'ArkUI', 'ohospoc',
                                     'Ability onCreate', 'loadContent', 'JANK'))]
    out = os.path.join(HERE, 'hilog-restart-hits.txt')
    with open(out, 'w', encoding='utf-8', errors='replace') as fh:
        fh.write('\n'.join(hits))
    print('total lines=%d hits=%d -> %s' % (len(raw.splitlines()), len(hits), out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
