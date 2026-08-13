"""Loopback UDP proxy that drops datagrams larger than a path MTU ceiling."""

from __future__ import annotations

import argparse
import selectors
import socket
import time
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--listen", type=int, required=True)
    parser.add_argument("--target-port", type=int, required=True)
    parser.add_argument("--maximum", type=int, default=1200)
    parser.add_argument("--dropped-marker", type=Path, required=True)
    parser.add_argument("--arm-marker", type=Path, required=True)
    args = parser.parse_args()

    frontend = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    frontend.setblocking(False)
    frontend.bind(("127.0.0.1", args.listen))
    selector = selectors.DefaultSelector()
    selector.register(frontend, selectors.EVENT_READ, "client")
    # Keep one upstream UDP socket per downstream endpoint. This preserves
    # the distinct server-observed four-tuples required by Multipath QUIC.
    clients: dict[tuple[str, int], socket.socket] = {}
    upstream_clients: dict[socket.socket, tuple[str, int]] = {}
    primary_client: tuple[str, int] | None = None
    target = ("127.0.0.1", args.target_port)
    try:
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            for key, _ in selector.select(0.05):
                try:
                    packet, sender = key.fileobj.recvfrom(65535)
                except BlockingIOError:
                    continue
                if key.data == "client":
                    path_client = sender
                else:
                    path_client = upstream_clients[key.fileobj]
                # The client arms the black hole after PATH_RESPONSE validates
                # Path 1. Restrict it to the independently bound secondary
                # path: a PMTU failure must not stall unrelated Path 0
                # control traffic while testing Path 1's fallback ceiling.
                if (args.arm_marker.exists() and primary_client is not None and
                        path_client != primary_client and len(packet) > args.maximum):
                    args.dropped_marker.write_text("oversized UDP datagram dropped\n", encoding="ascii")
                    continue
                if key.data == "client":
                    if primary_client is None:
                        primary_client = sender
                    upstream = clients.get(sender)
                    if upstream is None:
                        upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        upstream.setblocking(False)
                        upstream.bind(("127.0.0.1", 0))
                        clients[sender] = upstream
                        upstream_clients[upstream] = sender
                        selector.register(upstream, selectors.EVENT_READ, "server")
                    upstream.sendto(packet, target)
                else:
                    frontend.sendto(packet, path_client)
    finally:
        selector.close()
        frontend.close()
        for upstream in clients.values():
            upstream.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
