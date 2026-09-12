#!/usr/bin/env python3
"""Run commands in a guest over telnet and print what they wrote.

Once a guest has networking, this is a far better way to work with it than
typing at its screen. Point --enet_hostfwd at the guest's telnet port:

    chungusppc ... --enet_backend=slirp --enet_hostfwd=tcp:2323:23
    chungusppc-shell.py 'uname -a' 'ifconfig eth0'

Many hosts no longer ship a telnet client and Python dropped telnetlib, so this
speaks just enough of the protocol itself: refuse every option the server
offers, then drive the login and the shell as a plain byte stream.

CHUNGUSPPC_USER, CHUNGUSPPC_PASS, CHUNGUSPPC_PORT and CHUNGUSPPC_TIMEOUT override the defaults below.
"""

import os
import socket
import sys
import time

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240
MARK = "__CHUNGUSPPC_DONE__"


def strip_telnet(data, sock, carry):
    """Pull IAC sequences out of data, answering each with a refusal."""
    data = carry + data
    clean, reply, i = bytearray(), bytearray(), 0
    while i < len(data):
        if data[i] != IAC:
            clean.append(data[i])
            i += 1
            continue
        if i + 1 >= len(data):
            return bytes(clean), data[i:]
        cmd = data[i + 1]
        if cmd in (DO, DONT, WILL, WONT):
            if i + 2 >= len(data):
                return bytes(clean), data[i:]
            opt = data[i + 2]
            if cmd == DO:
                reply += bytes([IAC, WONT, opt])
            elif cmd == WILL:
                reply += bytes([IAC, DONT, opt])
            i += 3
        elif cmd == SB:                       # skip subnegotiation to IAC SE
            j = data.find(bytes([IAC, SE]), i)
            if j < 0:
                return bytes(clean), data[i:]
            i = j + 2
        else:
            i += 2
    if reply:
        sock.sendall(bytes(reply))
    return bytes(clean), b""


def run(cmds, host="127.0.0.1", port=None, user=None, pw=None, timeout=None):
    port = port if port is not None else int(os.environ.get("CHUNGUSPPC_PORT", "2323"))
    user = user if user is not None else os.environ.get("CHUNGUSPPC_USER", "root")
    pw = pw if pw is not None else os.environ.get("CHUNGUSPPC_PASS", "root")
    timeout = timeout if timeout is not None else int(
        os.environ.get("CHUNGUSPPC_TIMEOUT", "180"))
    sock = socket.create_connection((host, port), timeout=15)
    sock.settimeout(1.0)

    out, carry = "", b""
    stage = "login"
    deadline = time.time() + timeout

    while time.time() < deadline:
        try:
            data = sock.recv(4096)
            if not data:
                break
        except socket.timeout:
            data = b""

        text, carry = strip_telnet(data, sock, carry)
        out += text.decode("latin-1")

        low = out.lower()
        if stage == "login" and "login:" in low:
            sock.sendall((user + "\n").encode())
            stage = "password"
        elif stage == "password" and "password" in low.rsplit("login:", 1)[-1]:
            sock.sendall((pw + "\n").encode())
            stage = "shell"
            time.sleep(2)
            # Split the marker in the command so the shell's echo of the
            # command line can't be mistaken for the marker in the output.
            script = "; ".join(cmds) + '; echo __CHUNGUSPPC""_DONE__' + "\n"
            sock.sendall(script.encode())
            stage = "running"
        elif stage == "running" and MARK in out:
            break

    sock.close()
    return out


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    print(run(sys.argv[1:]))
