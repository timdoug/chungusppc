#!/usr/bin/env python3
"""Connect to a ChungusPPC serial socket. Ctrl-] disconnects."""
import argparse
import os
import select
import socket
import sys
import termios
import tty

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('socket', help='chungussocket for port A, chungussocket-b for port B')
args = parser.parse_args()
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as peer:
    peer.connect(args.socket)
    saved = termios.tcgetattr(sys.stdin)
    try:
        tty.setraw(sys.stdin)
        while True:
            ready, _, _ = select.select([peer, sys.stdin], [], [])
            if peer in ready:
                data = peer.recv(65536)
                if not data:
                    break
                os.write(sys.stdout.fileno(), data)
            if sys.stdin in ready:
                data = os.read(sys.stdin.fileno(), 4096)
                if not data or b'\x1d' in data:
                    break
                peer.sendall(data)
    finally:
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, saved)
