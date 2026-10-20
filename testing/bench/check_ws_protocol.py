#!/usr/bin/env python3
"""Adversarial RFC 6455 wire checks for the message processing loop."""
import argparse
from contextlib import nullcontext
import socket
import ssl
import struct
import subprocess
import time

from check_ws_echo import frame, receive, upgrade


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server")
    parser.add_argument("--port", type=int, default=19092)
    parser.add_argument("--cert")
    parser.add_argument("--key")
    parser.add_argument("--server-log")
    args = parser.parse_args()
    if bool(args.cert) != bool(args.key):
        parser.error("pass both --cert and --key")
    command = [args.server, str(args.port), "1"]
    if args.cert:
        command += [args.cert, args.key]
    with (open(args.server_log, "wb") if args.server_log else nullcontext(subprocess.DEVNULL)) as output, \
            subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT) as server:
        def connect(extra_headers="", initial_frames=b""):
            result = socket.create_connection(("127.0.0.1", args.port), timeout=3)
            if args.cert:
                result = ssl._create_unverified_context().wrap_socket(result, server_hostname="localhost")
            upgrade(result, args.port, extra_headers, initial_frames)
            return result

        try:
            deadline = time.monotonic() + 10
            while True:
                try:
                    with socket.create_connection(("127.0.0.1", args.port), timeout=1):
                        break
                except OSError:
                    if server.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("server failed to start")
                    time.sleep(0.05)
            malformed = [
                ("unmasked client", b"\x81\x01x", 1002),
                ("RSV", bytes([0xc1]) + frame(1, b"x")[1:], 1002),
                ("reserved data opcode", frame(3, b"x"), 1002),
                ("reserved control opcode", frame(11, b"x"), 1002),
                ("unexpected continuation", frame(0, b"x"), 1002),
                ("new data during fragmentation", frame(1, b"a", False) + frame(2, b"b"), 1002),
                ("oversized ping", frame(9, b"x" * 126), 1002),
                ("fragmented ping", frame(9, b"x", False), 1002),
                ("one byte close", frame(8, b"x"), 1002),
                ("reserved close code", frame(8, struct.pack("!H", 1005)), 1002),
                ("out of range close code", frame(8, struct.pack("!H", 5000)), 1002),
                ("invalid close UTF8", frame(8, struct.pack("!H", 1000) + b"\xff"), 1007),
                ("overlong UTF8", frame(1, b"\xc0\xaf"), 1007),
                ("UTF8 surrogate", frame(1, b"\xed\xa0\x80"), 1007),
                ("UTF8 out of range", frame(1, b"\xf4\x90\x80\x80"), 1007),
                ("truncated UTF8", frame(1, b"\xe2\x82"), 1007),
                ("fragmented invalid UTF8", frame(1, b"\xe2", False) + frame(0, b"a"), 1007),
                ("nonminimal 16 bit length", b"\x82\xfe\x00\x01\x00\x00\x00\x00x", 1002),
                ("nonminimal 64 bit length", b"\x82\xff" + struct.pack("!Q", 1) + b"\0" * 4 + b"x", 1002),
                ("63 bit overflow", b"\x82\xff" + struct.pack("!Q", 1 << 63) + b"\0" * 4, 1002),
                ("message limit before payload", b"\x82\xff" + struct.pack("!Q", 16*1024*1024+1) + b"\0" * 4, 1009),
                ("fragment aggregation limit", frame(1, b"a" * (8*1024*1024), False) +
                 b"\x80\xff" + struct.pack("!Q", 8*1024*1024+1) + b"\0" * 4, 1009),
            ]
            for name, wire, code in malformed:
                with connect() as sock:
                    sock.sendall(wire)
                    opcode, payload = receive(sock)
                    assert opcode == 8 and len(payload) >= 2, name
                    assert struct.unpack("!H", payload[:2])[0] == code, (name, payload)
            with connect("X-Large: " + "a" * 8192 + "\r\n") as sock:
                sock.sendall(frame(2, b"large upgrade header"))
                assert receive(sock) == (2, b"large upgrade header")
            with connect(initial_frames=frame(2, b"coalesced upgrade tail")) as sock:
                assert receive(sock) == (2, b"coalesced upgrade tail")
            with connect() as sock:
                sock.sendall(frame(1, b"\xe2", False) + frame(9, b"ping") + frame(0, b"\x82\xac"))
                assert receive(sock) == (10, b"ping")
                assert receive(sock) == (1, b"\xe2\x82\xac")
                tail = frame(2, b"next")
                sock.sendall(frame(1, b"ready") + tail[:1])
                assert receive(sock) == (1, b"ready"), "partial next header must not delay a reply"
                sock.sendall(tail[1:])
                assert receive(sock) == (2, b"next")
                sock.sendall(frame(1, b"a") + frame(9, b"b") + frame(2, b"c") + frame(8, b""))
                assert [receive(sock) for _ in range(4)] == [(1,b"a"),(10,b"b"),(2,b"c"),(8,b"")]
            print(f"PASS: {len(malformed)} malformed cases; fragmented UTF8, ordered control/data, partial-tail flush")
        finally:
            server.terminate()
            server.wait(timeout=5)


if __name__ == "__main__":
    main()
