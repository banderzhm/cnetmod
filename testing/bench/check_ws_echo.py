#!/usr/bin/env python3
"""Low-volume wire checks against bench_ws_server (plain WS or WSS)."""
import argparse
from contextlib import nullcontext
import base64
import hashlib
import os
import socket
import ssl
import struct
import subprocess
import time


def frame(opcode, payload, fin=True):
    key = os.urandom(4)
    size = len(payload)
    length = bytes([size]) if size <= 125 else (
        b"\x7e" + struct.pack("!H", size) if size <= 65535 else b"\x7f" + struct.pack("!Q", size))
    return bytes([(0x80 if fin else 0) | opcode, length[0] | 0x80]) + length[1:] + key + bytes(
        value ^ key[index % 4] for index, value in enumerate(payload))


def exact(sock, count):
    data = b""
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise RuntimeError("unexpected EOF")
        data += chunk
    return data


def receive(sock):
    first, second = exact(sock, 2)
    assert first & 0xf0 == 0x80, "server FIN/reserved bits"
    assert not second & 0x80, "server frames must be unmasked"
    size = second & 0x7f
    if size == 126:
        size = struct.unpack("!H", exact(sock, 2))[0]
    elif size == 127:
        size = struct.unpack("!Q", exact(sock, 8))[0]
    return first & 0xf, exact(sock, size)


def upgrade(sock, port, extra_headers="", initial_frames=b""):
    key = base64.b64encode(os.urandom(16)).decode()
    request = (f"GET /ws HTTP/1.1\r\nHost: localhost:{port}\r\nUpgrade: websocket\r\n"
               f"Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\n{extra_headers}\r\n")
    sock.sendall(request.encode() + initial_frames)
    headers = b""
    while not headers.endswith(b"\r\n\r\n"):
        headers += exact(sock, 1)
        assert len(headers) < 16384, "oversized upgrade response"
    assert headers.startswith(b"HTTP/1.1 101 "), headers
    accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
    fields = dict((name.strip().lower(), value.strip())
                  for name, value in (line.split(b":", 1) for line in headers.split(b"\r\n")[1:] if b":" in line))
    assert fields.get(b"sec-websocket-accept") == accept, headers


def check(sock, port):
    upgrade(sock, port)
    # Empty data and both extended-length boundaries, text and binary opcodes.
    for size in (0, 5, 125, 126, 4096, 65535, 65536):
        for opcode in (1, 2):
            payload = b"a" * size if opcode == 1 else bytes(index % 251 for index in range(size))
            wire = frame(opcode, payload)
            # Split header delivery across writes as well as exercising coalesced frames below.
            sock.sendall(wire[:1])
            sock.sendall(wire[1:])
            assert receive(sock) == (opcode, payload), (opcode, size)
    sock.sendall(frame(1, b"first", False) + frame(9, b"probe") + frame(0, b"last"))
    assert receive(sock) == (10, b"probe"), "interleaved ping must receive pong"
    assert receive(sock) == (1, b"firstlast"), "fragment reassembly"
    sock.sendall(b"".join(frame(2, bytes([index])) for index in range(16)))
    for index in range(16):
        assert receive(sock) == (2, bytes([index])), "pipeline ordering/payload"
    close = struct.pack("!H", 1000) + b"done"
    sock.sendall(frame(8, close))
    assert receive(sock) == (8, close), "close response"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server")
    parser.add_argument("--port", type=int, default=19091)
    parser.add_argument("--cert")
    parser.add_argument("--key")
    parser.add_argument("--server-log")
    args = parser.parse_args()
    if bool(args.cert) != bool(args.key):
        parser.error("pass both --cert and --key")
    command = [args.server, str(args.port), "1"]
    if args.cert:
        command.extend([args.cert, args.key])
    with (open(args.server_log, "wb") if args.server_log else nullcontext(subprocess.DEVNULL)) as output, \
            subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT) as server:
        try:
            deadline = time.monotonic() + 10
            while True:
                try:
                    sock = socket.create_connection(("127.0.0.1", args.port), timeout=3)
                    break
                except OSError:
                    if server.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("server failed to start")
                    time.sleep(0.05)
            if args.cert:
                # This is a self-signed test certificate, never a production verifier.
                sock = ssl._create_unverified_context().wrap_socket(sock, server_hostname="localhost")
            with sock:
                check(sock, args.port)
            print("PASS: upgrade, text/binary boundaries, fragmented header, ping, continuation, pipeline and close")
        finally:
            server.terminate()
            server.wait(timeout=5)


if __name__ == "__main__":
    main()
