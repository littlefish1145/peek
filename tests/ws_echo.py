#!/usr/bin/env python3
import socket, threading, hashlib, base64, struct, sys

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8731


def rdex(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            return None
        b += c
    return b


def frame(op, payload):
    n = len(payload)
    head = bytes([0x80 | op])
    if n < 126:
        head += bytes([n])
    elif n < 65536:
        head += bytes([126]) + struct.pack(">H", n)
    else:
        head += bytes([127]) + struct.pack(">Q", n)
    return head + payload


def handle(s):
    data = b""
    while b"\r\n\r\n" not in data:
        c = s.recv(4096)
        if not c:
            s.close()
            return
        data += c
    key = None
    for line in data.decode("latin1").split("\r\n"):
        if line.lower().startswith("sec-websocket-key:"):
            key = line.split(":", 1)[1].strip()
    if not key:
        s.close()
        return
    acc = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
    s.sendall(("HTTP/1.1 101 Switching Protocols\r\n"
               "Upgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Accept: %s\r\n\r\n" % acc).encode())
    frag = b""
    fragop = None
    while True:
        h = rdex(s, 2)
        if not h:
            break
        fin, op = h[0] & 0x80, h[0] & 0x0F
        ln = h[1] & 0x7F
        if ln == 126:
            ln = struct.unpack(">H", rdex(s, 2))[0]
        elif ln == 127:
            ln = struct.unpack(">Q", rdex(s, 8))[0]
        mask = rdex(s, 4)
        pay = rdex(s, ln) if ln else b""
        if pay is None or mask is None:
            break
        pay = bytes(b ^ mask[i % 4] for i, b in enumerate(pay))
        if op == 8:
            s.sendall(frame(8, pay[:128]))
            break
        if op == 9:
            s.sendall(frame(10, pay))
            continue
        if op == 10:
            continue
        if op in (1, 2):
            if fin:
                s.sendall(frame(op, pay))
            else:
                frag, fragop = pay, op
        elif op == 0:
            frag += pay
            if fin:
                s.sendall(frame(fragop, frag))
                frag, fragop = b"", None
    try:
        s.shutdown(socket.SHUT_RDWR)
    except OSError:
        pass
    s.close()


srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", PORT))
srv.listen(8)
print("ready", flush=True)
while True:
    try:
        c, _ = srv.accept()
    except OSError:
        break
    threading.Thread(target=handle, args=(c,), daemon=True).start()
