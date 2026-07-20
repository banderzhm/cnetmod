#!/usr/bin/env python3
"""Minimal python-h2 server used to verify cnetmod's native HTTP/2 client."""
from __future__ import annotations

import socket
import sys

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import DataReceived, RequestReceived, StreamEnded


def main(port: int) -> int:
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", port))
        listener.listen(1)
        print(f"READY {port}", flush=True)
        with listener.accept()[0] as client:
            connection = H2Connection(H2Configuration(client_side=False, header_encoding="utf-8"))
            connection.initiate_connection()
            client.sendall(connection.data_to_send())
            while True:
                data = client.recv(65535)
                if not data:
                    return 0
                for event in connection.receive_data(data):
                    if isinstance(event, DataReceived):
                        connection.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                    elif isinstance(event, RequestReceived):
                        assert (":method", "GET") in event.headers or (":method", "POST") in event.headers
                    elif isinstance(event, StreamEnded):
                        connection.send_headers(event.stream_id, [(":status", "200"), ("server", "python-h2")], end_stream=True)
                pending = connection.data_to_send()
                if pending:
                    client.sendall(pending)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(int(sys.argv[1])))
