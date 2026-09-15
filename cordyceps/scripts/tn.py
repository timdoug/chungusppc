import socket, sys, time, re

HOST, PORT = "127.0.0.1", 2324
IAC, DONT, DO, WONT, WILL, SB, SE = 255,254,253,252,251,250,240

def negotiate(sock, data, out):
    # respond to telnet IAC negotiations, strip them from the data stream
    i = 0; res = bytearray()
    while i < len(data):
        b = data[i]
        if b == IAC and i+1 < len(data):
            cmd = data[i+1]
            if cmd in (DO, DONT) and i+2 < len(data):
                opt = data[i+2]; sock.sendall(bytes([IAC, WONT, opt])); i += 3; continue
            if cmd in (WILL, WONT) and i+2 < len(data):
                opt = data[i+2]; sock.sendall(bytes([IAC, DONT, opt])); i += 3; continue
            if cmd == SB:
                j = data.find(bytes([IAC, SE]), i)
                i = (j+2) if j>=0 else len(data); continue
            i += 2; continue
        res.append(b); i += 1
    return bytes(res)

def read_until(sock, patterns, timeout, buf):
    end = time.time()+timeout
    while time.time() < end:
        sock.settimeout(max(0.2, end-time.time()))
        try: chunk = sock.recv(4096)
        except socket.timeout: break
        if not chunk: break
        clean = negotiate(sock, chunk, buf)
        buf.extend(clean)
        text = buf.decode('latin1')
        for p in patterns:
            if re.search(p, text): return text
    return buf.decode('latin1')

def main():
    user, pw, cmd, tmo = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]) if len(sys.argv)>4 else 60
    s = socket.create_connection((HOST, PORT), timeout=10)
    buf = bytearray()
    read_until(s, [r'ogin:'], 20, buf)
    s.sendall(user.encode()+b'\r\n')
    read_until(s, [r'assword:'], 15, buf)
    s.sendall(pw.encode()+b'\r\n')
    read_until(s, [r'[\$#] ', r'incorrect', r'\$$', r'#$'], 15, buf)
    marker = "ENDCMD_%d" % int(time.time())
    s.sendall((cmd + "; echo " + marker + "\r\n").encode())
    text = read_until(s, [marker+r'\r?\n'], tmo, buf)
    s.close()
    # print from after login
    print(text)
main()
