#!/usr/bin/env python3
"""Semantic cleartext HTTP/2 interoperability test using python-h2."""
from __future__ import annotations

import socket
import subprocess
import sys
import time
from contextlib import closing

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import ConnectionTerminated, DataReceived, PingAckReceived, ResponseReceived, StreamEnded, SettingsAcknowledged


def free_port() -> int:
    with closing(socket.socket()) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def flush(sock: socket.socket, connection: H2Connection) -> None:
    data = connection.data_to_send()
    if data:
        sock.sendall(data)


def main(server: str) -> int:
    port = free_port()
    process = subprocess.Popen([server, str(port)], stdout=subprocess.PIPE, text=True)
    try:
        assert process.stdout is not None
        if process.stdout.readline().strip() != f"READY {port}":
            raise RuntimeError("HTTP/2 test server did not become ready")
        with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
            sock.settimeout(5)
            h2 = H2Connection(H2Configuration(client_side=True, header_encoding="utf-8"))
            h2.initiate_connection()
            flush(sock, h2)
            streams = (1, 3, 5)
            for stream, path in zip(streams, ("/", "/huffman/long/header", "/post")):
                h2.send_headers(stream, [
                    (":method", "POST" if stream == 5 else "GET"),
                    (":authority", "www.example.com"),
                    (":scheme", "http"),
                    (":path", path),
                    ("x-huffman", "semantic interoperability dynamic table"),
                ], end_stream=stream != 5)
            h2.send_data(5, b"payload", end_stream=True)
            h2.send_headers(7, [
                (":method", "POST"), (":authority", "www.example.com"),
                (":scheme", "http"), (":path", "/cancelled"),
            ], end_stream=False)
            h2.reset_stream(7)
            h2.ping(b"cnetmod2")
            flush(sock, h2)
            seen, complete, settings_ack, ping_ack = set(), set(), False, False
            bodies: dict[int, bytearray] = {}
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline and complete != set(streams):
                payload = sock.recv(65535)
                if not payload:
                    break
                for event in h2.receive_data(payload):
                    if isinstance(event, SettingsAcknowledged):
                        settings_ack = True
                    elif isinstance(event, PingAckReceived):
                        ping_ack = event.ping_data == b"cnetmod2"
                    elif isinstance(event, ResponseReceived):
                        assert (":status", "200") in event.headers
                        seen.add(event.stream_id)
                    elif isinstance(event, DataReceived):
                        bodies.setdefault(event.stream_id, bytearray()).extend(event.data)
                        h2.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                    elif isinstance(event, StreamEnded):
                        complete.add(event.stream_id)
                flush(sock, h2)
            assert settings_ack, "server did not ACK client SETTINGS"
            assert ping_ack, "server did not ACK PING"
            assert seen == set(streams), f"missing response headers: {seen}"
            assert complete == set(streams), f"incomplete streams: {complete}"
            assert bytes(bodies.get(1, b"")) == b"Hello, World!"
            assert bytes(bodies.get(3, b"")) == b"Hello, World!"
            assert bytes(bodies.get(5, b"")) == b"received: payload"

        # DATA on stream 0 is invalid in RFC 9113.  The peer must terminate
        # the connection with GOAWAY rather than crash or silently continue.
        with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
            sock.settimeout(5)
            invalid = H2Connection(H2Configuration(client_side=True, header_encoding="utf-8"))
            invalid.initiate_connection()
            sock.sendall(invalid.data_to_send() + b"\x00\x00\x00\x00\x00\x00\x00\x00\x00")
            received_goaway = False
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline and not received_goaway:
                payload = sock.recv(65535)
                if not payload:
                    break
                for event in invalid.receive_data(payload):
                    received_goaway |= isinstance(event, ConnectionTerminated)
            assert received_goaway, "malformed frame did not receive GOAWAY"
        print("python-h2 semantic interoperability: PASS")
        return 0
    finally:
        process.terminate()
        process.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1]))
