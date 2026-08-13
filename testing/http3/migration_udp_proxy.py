"""Loopback UDP proxy that changes its upstream source port on command."""

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
    parser.add_argument("--marker", type=Path, required=True)
    parser.add_argument("--observed", type=Path, required=True)
    args = parser.parse_args()

    frontend = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    upstream_one = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    upstream_two = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    for sock in (frontend, upstream_one, upstream_two):
        sock.setblocking(False)
    frontend.bind(("127.0.0.1", args.listen))
    upstream_one.bind(("127.0.0.1", 0))
    upstream_two.bind(("127.0.0.1", 0))

    selector = selectors.DefaultSelector()
    selector.register(frontend, selectors.EVENT_READ, "frontend")
    selector.register(upstream_one, selectors.EVENT_READ, "upstream_one")
    selector.register(upstream_two, selectors.EVENT_READ, "upstream_two")
    target = ("127.0.0.1", args.target_port)
    client = None
    switched = False
    deadline = time.monotonic() + 90
    try:
        while time.monotonic() < deadline:
            if not switched and args.marker.exists():
                switched = True
            for key, _ in selector.select(0.05):
                if key.data == "frontend":
                    try:
                        packet, sender = frontend.recvfrom(65535)
                    except BlockingIOError:
                        continue
                    client = sender
                    upstream = upstream_two if switched else upstream_one
                    upstream.sendto(packet, target)
                else:
                    try:
                        packet, sender = key.fileobj.recvfrom(65535)
                    except BlockingIOError:
                        continue
                    if client is not None:
                        frontend.sendto(packet, client)
                    if key.data == "upstream_two" and switched:
                        args.observed.write_text("path moved\n", encoding="ascii")
    finally:
        selector.close()
        frontend.close()
        upstream_one.close()
        upstream_two.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
