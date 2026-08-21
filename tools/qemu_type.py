import socket, time, sys

HOST, PORT = "127.0.0.1", 44455

def recv_all(s, t=0.4):
    s.settimeout(t)
    data = b""
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    return data

def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "shtest"
    s = socket.create_connection((HOST, PORT), timeout=5)
    recv_all(s, 1.0)
    # QEMU human monitor: sendkey per character, then ret
    for ch in cmd:
        if ch == ' ':
            key = "spc"
        elif ch == '|':
            key = "shift-backslash"
        elif ch == '>':
            key = "shift-dot"
        elif ch == '&':
            key = "shift-7"
        elif ch == '%':
            key = "shift-5"
        elif ch.isupper():
            key = "shift-" + ch.lower()
        else:
            key = ch
        s.sendall(("sendkey %s\n" % key).encode())
        recv_all(s)
        time.sleep(0.18)
    s.sendall(b"sendkey ret\n")
    recv_all(s)
    s.close()

if __name__ == "__main__":
    main()
