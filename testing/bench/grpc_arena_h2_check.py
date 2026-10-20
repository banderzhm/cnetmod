#!/usr/bin/env python3
"""Bounded HTTP/2 multiplexing regression: three batches on one connection."""
import argparse
import socket
import ssl
import time

from h2.connection import H2Connection
from h2.events import (ConnectionTerminated, DataReceived, RemoteSettingsChanged, ResponseReceived,
                       StreamEnded, StreamReset, TrailersReceived)
from h2.settings import SettingCodes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("port", type=int)
    parser.add_argument("--cert")
    args = parser.parse_args()
    deadline = time.monotonic() + 10
    while True:
        try:
            transport = socket.create_connection((args.host, args.port), timeout=3)
            break
        except OSError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.05)
    if args.cert:
        context = ssl.create_default_context(cafile=args.cert)
        context.set_alpn_protocols(["h2"])
        transport = context.wrap_socket(transport, server_hostname="localhost")
        assert transport.selected_alpn_protocol() == "h2"
    connection = H2Connection()
    connection.initiate_connection()
    connection.update_settings({SettingCodes.INITIAL_WINDOW_SIZE: 0})
    with transport:
        transport.sendall(connection.data_to_send())
        settings_received = False
        while not settings_received:
            data = transport.recv(65536)
            assert data, "EOF before server SETTINGS"
            for event in connection.receive_data(data):
                if isinstance(event, ConnectionTerminated):
                    raise AssertionError(event)
                settings_received |= isinstance(event, RemoteSettingsChanged)
            transport.sendall(connection.data_to_send())
        capacity = min(100, connection.remote_settings.max_concurrent_streams)
        assert capacity > 0, "Server does not admit any RPC streams"
        for batch in range(3):
            pending = set()
            bodies, statuses, trailers = {}, {}, {}
            sent = 0
            while sent < 100 or pending:
                while sent < 100 and len(pending) < capacity:
                    stream = connection.get_next_available_stream_id()
                    pending.add(stream)
                    connection.send_headers(stream, [
                        (":method", "POST"), (":scheme", "https" if args.cert else "http"),
                        (":authority", "localhost"),
                        (":path", "/benchmark.BenchmarkService/GetSum"),
                        ("content-type", "application/grpc"), ("te", "trailers")])
                    connection.send_data(stream, bytes.fromhex("000000000408011002"), end_stream=True)
                    sent += 1
                transport.sendall(connection.data_to_send())
                data = transport.recv(65536)
                assert data, f"EOF in batch {batch}: {len(pending)} unfinished streams"
                for event in connection.receive_data(data):
                    if isinstance(event, (ConnectionTerminated, StreamReset)):
                        raise AssertionError(event)
                    if isinstance(event, ResponseReceived):
                        statuses[event.stream_id] = dict(event.headers)
                        # A response must wait for this stream credit. The H2
                        # parser rejects DATA received against the zero window.
                        connection.increment_flow_control_window(7, event.stream_id)
                    if isinstance(event, DataReceived):
                        bodies.setdefault(event.stream_id, bytearray()).extend(event.data)
                        connection.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                    if isinstance(event, TrailersReceived):
                        trailers[event.stream_id] = dict(event.headers)
                    if isinstance(event, StreamEnded):
                        stream = event.stream_id
                        assert statuses[stream][b":status"] == b"200"
                        assert statuses[stream][b"content-type"].startswith(b"application/grpc")
                        assert b"content-length" not in statuses[stream]
                        assert trailers[stream][b"grpc-status"] == b"0"
                        assert bodies[stream] == bytes.fromhex("00000000020803")
                        pending.remove(stream)
                outgoing = connection.data_to_send()
                if outgoing:
                    transport.sendall(outgoing)
    print(f"PASS: 300 RPCs, zero-window then per-stream credit, three 100-RPC batches, up to {capacity} streams, one persistent HTTP/2 connection")


if __name__ == "__main__":
    main()
