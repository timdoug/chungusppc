#!/usr/bin/env python3
"""Run before mklinux-serial-test.c in the guest; pass one serial socket path."""
import argparse
import hashlib
import socket
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('socket', help='chungussocket for ttyS0, chungussocket-b for ttyS1')
args = parser.parse_args()
expected = bytes((i * 73 + 19) & 255 for i in range(16384))
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as peer:
    peer.connect(args.socket)
    peer.settimeout(1)
    print('Connected; run serial-test in the guest.', flush=True)
    received = bytearray()
    sent = False
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        try:
            data = peer.recv(65536)
        except socket.timeout:
            continue
        if not data:
            raise SystemExit('FAIL: serial socket closed before the transfer finished')
        received.extend(data)
        if not sent and b'GO0' in received:
            received.clear()
            peer.settimeout(30)
            peer.sendall(expected)
            peer.settimeout(1)
            sent = True
        if sent and len(received) >= len(expected):
            if received != expected:
                raise SystemExit('FAIL: returned bytes differ')
            print('PASS:', len(received), 'bytes, MD5', hashlib.md5(received).hexdigest())
            break
    else:
        raise SystemExit('FAIL: serial transfer timed out after %d bytes' % len(received))
