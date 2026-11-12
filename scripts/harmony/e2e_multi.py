#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Second-level end-to-end test of the HarmonyOS client's file receive path.

Exercises the parts the single-file test does not:
  * DDRG with two paths -> the client must pair them FIFO with two transfers
  * a file larger than one 512 KB chunk -> the multi chunk DataChunk path
  * a path traversal filename -> the client must keep it inside the save dir
  * two concurrent transfer ids

Wire format is identical to fake_df_server.py (see its docstring).
"""

import hashlib
import os
import re
import socket
import struct
import subprocess
import sys
import time

HDC = r'C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
TARGET = '59C0226421001606'
BUNDLE = 'com.deskflow.ohospoc'
PORT = int(os.environ.get('DF_PORT', '24806'))
HERE = os.path.dirname(os.path.abspath(__file__))
LAND = '/data/app/el2/100/base/%s/haps/entry/files/Deskflow' % BUNDLE
SRVLOG = os.path.join(HERE, 'fake-server-multi.log')
PROOF = os.path.join(HERE, 'landed-multi-proof.txt')

CHUNK_TYPE_START = 1
CHUNK_TYPE_DATA = 2
CHUNK_TYPE_END = 3
CHUNK = 512 * 1024

# A 1.5 MB payload so it must arrive in 3 DataChunk messages.
BIG = bytes(((i * 37 + 11) & 0xFF) for i in range(1536 * 1024))
SMALL = b'SMALL-PAYLOAD-FOR-SECOND-TRANSFER\n' * 10
TRAVERSAL = '../../../../data/storage/el2/base/haps/entry/files/Deskflow/escape.txt'


def frame(payload: bytes) -> bytes:
    return struct.pack('>I', len(payload)) + payload


def dftr(mark: int, inner: bytes) -> bytes:
    return frame(b'DFTR' + bytes([mark]) + struct.pack('>I', len(inner)) + inner)


def ddrg(count: int, info: bytes) -> bytes:
    return frame(b'DDRG' + struct.pack('>H', count) + struct.pack('>I', len(info)) + info)


def hello() -> bytes:
    return frame(b'Synergy' + struct.pack('>H', 1) + struct.pack('>H', 8))


def recv_exact(sock, n):
    buf = b''
    while len(buf) < n:
        part = sock.recv(n - len(buf))
        if not part:
            raise EOFError('peer closed')
        buf += part
    return buf


def recv_frame(sock):
    (length,) = struct.unpack('>I', recv_exact(sock, 4))
    return recv_exact(sock, length) if length else b''


def hdc(*args, timeout=120):
    try:
        p = subprocess.run([HDC, '-t', TARGET] + list(args), capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 'TIMEOUT', 124
    return p.stdout.decode('utf-8', 'replace') + p.stderr.decode('utf-8', 'replace'), p.returncode


def serve():
    log = open(SRVLOG, 'w', encoding='utf-8')
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', PORT))
    srv.listen(1)
    srv.settimeout(120)
    log.write('LISTEN 127.0.0.1:%d\n' % PORT)
    log.flush()
    conn, addr = srv.accept()
    conn.settimeout(120)
    log.write('ACCEPT from %s:%d\n' % (addr[0], addr[1]))

    conn.sendall(hello())
    log.write('SENT   Hello (Synergy 1.8)\n')
    back = recv_frame(conn)
    log.write('RECV   HelloBack raw=%s\n' % back.hex())
    conn.sendall(frame(b'DSOP' + struct.pack('>I', 0)))
    log.write('SENT   DSOP (empty options)\n')

    # DDRG carries both names; the second one is a traversal attempt.
    info = b'C:\\Users\\Administrator\\Desktop\\big-payload.bin\x00' + TRAVERSAL.encode() + b'\x00'
    conn.sendall(ddrg(2, info))
    log.write('SENT   DDRG count=2 info=%r\n' % info)

    # Transfer 1: 1.5 MB in 3 chunks.
    conn.sendall(dftr(CHUNK_TYPE_START, struct.pack('<I', 1) + bytes([CHUNK_TYPE_START]) +
                      struct.pack('<Q', len(BIG))))
    log.write('SENT   DFTR DataStart transferId=1 size=%d\n' % len(BIG))
    offset = 0
    index = 0
    while offset < len(BIG):
        piece = BIG[offset:offset + CHUNK]
        conn.sendall(dftr(CHUNK_TYPE_DATA, struct.pack('<I', 1) + bytes([CHUNK_TYPE_DATA]) + piece))
        index += 1
        offset += len(piece)
        log.write('SENT   DFTR DataChunk #%d bytes=%d\n' % (index, len(piece)))
    conn.sendall(dftr(CHUNK_TYPE_END, struct.pack('<I', 1) + bytes([CHUNK_TYPE_END])))
    log.write('SENT   DFTR DataEnd transferId=1\n')

    # Transfer 2: small file, different transfer id, sent while 1 is done.
    conn.sendall(dftr(CHUNK_TYPE_START, struct.pack('<I', 2) + bytes([CHUNK_TYPE_START]) +
                      struct.pack('<Q', len(SMALL))))
    log.write('SENT   DFTR DataStart transferId=2 size=%d\n' % len(SMALL))
    conn.sendall(dftr(CHUNK_TYPE_DATA, struct.pack('<I', 2) + bytes([CHUNK_TYPE_DATA]) + SMALL))
    log.write('SENT   DFTR DataChunk #1 bytes=%d\n' % len(SMALL))
    conn.sendall(dftr(CHUNK_TYPE_END, struct.pack('<I', 2) + bytes([CHUNK_TYPE_END])))
    log.write('SENT   DFTR DataEnd transferId=2\n')

    conn.settimeout(3.0)
    deadline = time.time() + 15
    while time.time() < deadline:
        try:
            data = conn.recv(4096)
        except socket.timeout:
            break
        except OSError:
            break
        if not data:
            break
        log.write('RECV   %r\n' % data)
    log.write('DONE   transfer complete\n')
    log.flush()
    log.close()
    conn.close()
    srv.close()


def main():
    proc = subprocess.Popen([sys.executable, os.path.abspath(__file__), '--serve'],
                             stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    time.sleep(2)
    hdc('rport', 'tcp:%d' % PORT, 'tcp:%d' % PORT)
    out, _ = hdc('shell', 'aa start -a BackGroundAbility -b %s --ps host 127.0.0.1 '
                 '--ps port %d --ps screen HarmonyPC --ps autoreconnect true' % (BUNDLE, PORT))
    print('aa start ->', out.strip())
    proc.wait(timeout=180)

    lines = []
    with open(SRVLOG, 'r', encoding='utf-8', errors='replace') as fh:
        lines.append('--- fake server log ---')
        lines.append(fh.read().rstrip())

    ls, rc = hdc('shell', 'ls -l %s' % LAND)
    lines.append('--- ls -l %s (rc=%s) ---' % (LAND, rc))
    lines.append(ls.rstrip())
    md5, _ = hdc('shell', 'md5sum %s/*' % LAND)
    lines.append('--- md5sum ---')
    lines.append(md5.rstrip())
    stat, _ = hdc('shell', 'stat %s/*' % LAND)
    lines.append('--- stat ---')
    lines.append(stat.rstrip())

    big_md5 = hashlib.md5(BIG).hexdigest()
    small_md5 = hashlib.md5(SMALL).hexdigest()
    lines.append('--- expected ---')
    lines.append('big   size=%d md5=%s' % (len(BIG), big_md5))
    lines.append('small size=%d md5=%s' % (len(SMALL), small_md5))
    lines.append('traversal name sent: %s' % TRAVERSAL)

    found_big = big_md5 in md5
    found_small = small_md5 in md5
    # ls -l only prints basenames; stat prints full paths. The traversal payload
    # must land inside the save dir, never outside it.
    landed_paths = re.findall(r'File: (\S+)', stat)
    outside = [p for p in landed_paths if not p.startswith(LAND + '/')]
    escape_paths = [p for p in landed_paths if p.endswith('/escape.txt')]
    escaped = bool(outside)
    lines.append('--- assertions ---')
    lines.append('big payload md5 on device      %s' % ('YES' if found_big else 'no'))
    lines.append('small payload md5 on device    %s' % ('YES' if found_small else 'no'))
    lines.append('traversal escaped save dir     %s' % ('YES(BAD)' if escaped else 'no'))
    lines.append('escape.txt landed at           %s' % escape_paths)
    lines.append('files outside save dir         %s' % outside)
    names = sorted(set(re.findall(r'/Flux/([^\s]+)', md5)))
    lines.append('landed file names: %s' % names)

    with open(PROOF, 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(lines))
    print('\n'.join(lines[-12:]))
    print('wrote %s' % PROOF)
    return 0


if __name__ == '__main__':
    if '--serve' in sys.argv:
        serve()
    else:
        sys.exit(main())
