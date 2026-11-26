#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Prepare the installed Flux build for a real KVM/DFTR live test.

Does NOT touch the original Flux installation. Everything written lives
under the installed Lingxi directory (portable settings) or this script's
kvm-live scratch dir.
"""
import hashlib
import os
import shutil
import sys

ROOT = r'C:\Users\Administrator\Desktop\Quick\deskflow-lingxi'
INST = r'C:\Users\Administrator\AppData\Local\Programs\Flux'
SETTINGS = os.path.join(INST, 'settings')
CONF = os.path.join(SETTINGS, 'Flux.conf')
SRVCONF = os.path.join(SETTINGS, 'flux-server.conf')
HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(HERE, 'kvm-live')
BACKUP = os.path.join(WORK, 'conf-original.txt')

CONF_BODY = """[core]
computerName=JARVIS
coreMode=2
port=24800

[security]
tlsEnabled=false

[server]
protocol=barrier
externalConfig=false
enableClipboard=false

[computer_JARVIS]
name=JARVIS

[computer_HarmonyPC]
name=HarmonyPC
"""

SRVCONF_BODY = """section: links
\tJARVIS:
\t\tright = HarmonyPC
\tHarmonyPC:
\t\tleft = JARVIS
end

section: options
end
"""


def make_files():
    os.makedirs(WORK, exist_ok=True)
    out = []
    small = bytes(((i * 73 + 19) & 0xFF) for i in range(4096))
    multi = bytes(((i * 37 + 11) & 0xFF) for i in range(1536 * 1024))
    for name, blob in (('kvm-live-small.bin', small), ('kvm-live-multi.bin', multi)):
        p = os.path.join(WORK, name)
        with open(p, 'wb') as fh:
            fh.write(blob)
        out.append('FILE|%s|%d|%s|%s' % (p, len(blob),
                   hashlib.md5(blob).hexdigest(), hashlib.sha256(blob).hexdigest()))
    return out


def main():
    os.makedirs(WORK, exist_ok=True)
    with open(CONF, 'r', encoding='ascii', errors='replace') as fh:
        original = fh.read()
    with open(BACKUP, 'w', encoding='ascii') as fh:
        fh.write(original)
    print('CONF_ORIGINAL=%r' % original)

    with open(CONF, 'w', encoding='ascii', newline='\n') as fh:
        fh.write(CONF_BODY)
    with open(SRVCONF, 'w', encoding='ascii', newline='\n') as fh:
        fh.write(SRVCONF_BODY)
    print('CONF_WRITTEN=%d bytes' % os.path.getsize(CONF))
    print('SRVCONF_WRITTEN=%d bytes' % os.path.getsize(SRVCONF))
    with open(CONF, 'r', encoding='ascii') as fh:
        print('CONF_NOW=%r' % fh.read())

    for line in make_files():
        print(line)
    print('SETUP_DONE=1')
    return 0


if __name__ == '__main__':
    sys.exit(main())
