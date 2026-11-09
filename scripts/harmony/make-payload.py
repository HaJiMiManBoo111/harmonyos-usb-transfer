#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Create the deterministic payload used by the HarmonyOS file-receive E2E test."""
import hashlib
import os
import sys

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'e2e-payload.bin')

HEADER = b'HARMONY-EOF-DFTR-PAYLOAD\n'
TAIL = b'\nEND-OF-PAYLOAD\n'

parts = [HEADER]
for i in range(2000):
    parts.append(b'line %05d: %s\n' % (i, (b'x' * (i % 97))))
parts.append(TAIL)
blob = b''.join(parts)

with open(OUT, 'wb') as fh:
    fh.write(blob)

print('WROTE %s' % OUT)
print('SIZE  %d' % len(blob))
print('MD5   %s' % hashlib.md5(blob).hexdigest())
print('SHA256 %s' % hashlib.sha256(blob).hexdigest())
sys.exit(0)
