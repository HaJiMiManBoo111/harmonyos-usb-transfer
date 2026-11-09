#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Fake Flux *server* used to validate the HarmonyOS client's file receive
path (DFTR / DDRG -> disk) end to end, without depending on the Windows GUI.

Wire format (verified against both implementations):

  frame      = len(4, big endian) | payload
  Hello      = "Synergy" (7) | major(2, BE) | minor(2, BE)
  DDRG       = "DDRG" | fileCount(2, BE) | len(4, BE) | info[len]
               info = NUL separated absolute path strings
  DFTR       = "DFTR" | mark(1) | len(4, BE) | data[len]
               data = transferId(4, little endian) | chunkType(1) | payload
               chunkType: 1=DataStart (payload = size, 8 bytes LE)
                           2=DataChunk (payload = raw bytes)
                           3=DataEnd   (payload = empty)
  DSOP       = "DSOP" | count(4, BE) | values(count * 4, BE)

References (read only):
  harmony  entry/src/main/cpp/protocol/proto_util.cpp:15-114,206-231
  harmony  entry/src/main/cpp/protocol/deskflow_client.cpp:646-820,1274-1291
  windows  deskflow/src/lib/deskflow/ProtocolUtil.cpp:26-66
  windows  deskflow/src/lib/deskflow/FileChunk.cpp:46-105
"""

import argparse
import hashlib
import os
import socket
import struct
import sys
import time

CHUNK_TYPE_START = 1
CHUNK_TYPE_DATA = 2
CHUNK_TYPE_END = 3


def frame(payload: bytes) -> bytes:
    return struct.pack('>I', len(payload)) + payload


def dftr(mark: int, inner: bytes) -> bytes:
    return frame(b'DFTR' + bytes([mark]) + struct.pack('>I', len(inner)) + inner)


def ddrg(count: int, info: bytes) -> bytes:
    return frame(b'DDRG' + struct.pack('>H', count) + struct.pack('>I', len(info)) + info)


def dsop() -> bytes:
    return frame(b'DSOP' + struct.pack('>I', 0))


def hello() -> bytes:
    # kHelloNameLength = 7, kProtocolMajorVersion = 1, kProtocolMinorVersion = 8
    return frame(b'Synergy' + struct.pack('>H', 1) + struct.pack('>H', 8))


def recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = b''
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError('peer closed')
        buf += chunk
    return buf


def recv_frame(sock: socket.socket) -> bytes:
    raw = recv_exact(sock, 4)
    (length,) = struct.unpack('>I', raw)
    return recv_exact(sock, length) if length else b''


def log(msg: str) -> None:
    print(msg, flush=True)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=24801)
    ap.add_argument('--file', required=True, help='local file to transfer')
    ap.add_argument('--name', default=None, help='remote path reported in DDRG')
    ap.add_argument('--chunk', type=int, default=512 * 1024)
    ap.add_argument('--timeout', type=int, default=120)
    args = ap.parse_args()

    with open(args.file, 'rb') as fh:
        blob = fh.read()
    md5 = hashlib.md5(blob).hexdigest()
    sha256 = hashlib.sha256(blob).hexdigest()
    remote_name = args.name or ('C:\\Users\\Administrator\\Desktop\\' +
                                 os.path.basename(args.file))
    log('LOCAL  file=%s size=%d md5=%s sha256=%s' %
        (args.file, len(blob), md5, sha256))
    log('REMOTE path=%s' % remote_name)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(1)
    srv.settimeout(args.timeout)
    log('LISTEN %s:%d' % (args.host, args.port))

    try:
        conn, addr = srv.accept()
    except socket.timeout:
        log('TIMEOUT no client connected within %ds' % args.timeout)
        return 2
    conn.settimeout(args.timeout)
    log('ACCEPT from %s:%d' % addr)

    try:
        conn.sendall(hello())
        log('SENT   Hello (Synergy 1.8)')

        back = recv_frame(conn)
        log('RECV   HelloBack raw=%s' % back.hex())
        log('RECV   HelloBack ascii=%r' % back)

        conn.sendall(dsop())
        log('SENT   DSOP (empty options)')

        info = remote_name.encode('utf-8') + b'\x00'
        conn.sendall(ddrg(1, info))
        log('SENT   DDRG count=1 info=%r' % info)

        transfer_id = 1
        start = (struct.pack('<I', transfer_id) +
                 bytes([CHUNK_TYPE_START]) +
                 struct.pack('<Q', len(blob)))
        conn.sendall(dftr(CHUNK_TYPE_START, start))
        log('SENT   DFTR DataStart transferId=%d size=%d' % (transfer_id, len(blob)))

        offset = 0
        index = 0
        while offset < len(blob):
            piece = blob[offset:offset + args.chunk]
            body = struct.pack('<I', transfer_id) + bytes([CHUNK_TYPE_DATA]) + piece
            conn.sendall(dftr(CHUNK_TYPE_DATA, body))
            index += 1
            offset += len(piece)
            log('SENT   DFTR DataChunk #%d bytes=%d' % (index, len(piece)))
        if not blob:
            log('SENT   (no data chunk, empty file)')

        end = struct.pack('<I', transfer_id) + bytes([CHUNK_TYPE_END])
        conn.sendall(dftr(CHUNK_TYPE_END, end))
        log('SENT   DFTR DataEnd transferId=%d' % transfer_id)

        # keep the socket open and drain whatever the client sends (CNOP ...)
        deadline = time.time() + 20
        conn.settimeout(2.0)
        while time.time() < deadline:
            try:
                data = conn.recv(4096)
            except socket.timeout:
                break
            except OSError:
                break
            if not data:
                break
            log('RECV   %r' % data)
        log('DONE   transfer complete')
        return 0
    except Exception as exc:  # noqa: BLE001
        log('ERROR  %r' % (exc,))
        return 1
    finally:
        try:
            conn.close()
        except Exception:  # noqa: BLE001
            pass
        srv.close()


if __name__ == '__main__':
    sys.exit(main())
