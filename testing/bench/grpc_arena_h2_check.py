#!/usr/bin/env python3
"""Bounded HTTP/2 multiplexing regression: three batches on one connection."""
import argparse
import socket
import ssl
import time

from h2.connection import H2Connection
from h2.events import (ConnectionTerminated, DataReceived, ResponseReceived,
                       StreamEnded, StreamReset, TrailersReceived)


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
    with transport:
        transport.sendall(connection.data_to_send())
        for batch in range(3):
            pending = set()
            bodies, statuses, trailers = {}, {}, {}
            for _ in range(100):
                stream = connection.get_next_available_stream_id()
                pending.add(stream)
                connection.send_headers(stream, [
                    (":method", "POST"), (":scheme", "https" if args.cert else "http"),
                    (":authority", "localhost"),
                    (":path", "/benchmark.BenchmarkService/GetSum"),
                    ("content-type", "application/grpc"), ("te", "trailers")])
                connection.send_data(stream, bytes.fromhex("000000000408011002"), end_stream=True)
            transport.sendall(connection.data_to_send())
            while pending:
                data = transport.recv(65536)
                assert data, f"EOF in batch {batch}: {len(pending)} unfinished streams"
                for event in connection.receive_data(data):
                    if isinstance(event, (ConnectionTerminated, StreamReset)):
                        raise AssertionError(event)
                    if isinstance(event, ResponseReceived):
                        statuses[event.stream_id] = dict(event.headers)
                    if isinstance(event, DataReceived):
                        bodies.setdefault(event.stream_id, bytearray()).extend(event.data)
                        connection.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                    if isinstance(event, TrailersReceived):
                        trailers[event.stream_id] = dict(event.headers)
                    if isinstance(event, StreamEnded):
                        stream = event.stream_id
                        assert statuses[stream][b":status"] == b"200"
                        assert statuses[stream][b"content-type"].startswith(b"application/grpc")
                        assert trailers[stream][b"grpc-status"] == b"0"
                        assert bodies[stream] == bytes.fromhex("00000000020803")
                        pending.remove(stream)
                outgoing = connection.data_to_send()
                if outgoing:
                    transport.sendall(outgoing)
    print("PASS: 300 RPCs, three 100-stream batches, one persistent HTTP/2 connection")


if __name__ == "__main__":
    main()
