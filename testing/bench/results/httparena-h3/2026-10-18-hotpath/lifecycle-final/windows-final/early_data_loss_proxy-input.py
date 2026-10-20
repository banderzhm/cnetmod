"""UDP fault fixture: early data before Retry, then loss after the new DCID."""

import argparse
import heapq
import json
from pathlib import Path
import selectors
import socket
import time

from aioquic.buffer import Buffer
from aioquic.quic.crypto import CryptoPair
from aioquic.quic.packet import pull_quic_header


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--listen", type=int, required=True)
    parser.add_argument("--target-port", type=int, required=True)
    parser.add_argument("--stats", type=Path, required=True)
    args = parser.parse_args()
    selector = selectors.DefaultSelector()
    front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    front.bind(("127.0.0.1", args.listen))
    front.setblocking(False)
    selector.register(front, selectors.EVENT_READ, None)
    clients = {}
    pending = []
    sequence = 0
    stats = dict(retries_delayed=0, early_before_retry=0,
                 early_dropped_after_retry=0, initial_pn_regressions=0,
                 source_address_reuse=0, initial_pn_regression_events=[])

    def persist():
        temporary = args.stats.with_suffix(".tmp")
        temporary.write_text(json.dumps(stats), encoding="utf-8")
        temporary.replace(args.stats)

    persist()
    while True:
        now = time.monotonic()
        while pending and pending[0][0] <= now:
            _, _, data, state, retry = heapq.heappop(pending)
            if retry:
                state["retry"] = True
            try:
                front.sendto(data, state["address"])
            except ConnectionResetError:
                pass  # A previous test client can already have closed UDP.
        for key, _ in selector.select(0.001):
            try:
                data, address = key.fileobj.recvfrom(65536)
            except (ConnectionResetError, BlockingIOError):
                continue
            if key.data is None:
                kind = data[0] & 0xF0
                header = None
                if kind == 0xC0:
                    buffer = Buffer(data=data)
                    header = pull_quic_header(buffer, host_cid_length=8)
                state = clients.get(address)
                new_connection = (state is not None and header is not None and
                    state["cid"] is not None and state["cid"] != header.source_cid)
                if new_connection:
                    stats["source_address_reuse"] += 1
                if state is None or new_connection:
                    backend = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    backend.bind(("127.0.0.1", 0))
                    backend.setblocking(False)
                    state = dict(address=address, backend=backend, retry=False,
                                 dropped=False, initial_pn=-1, cid=None,
                                 original_dcid=None, retry_scid=None)
                    clients[address] = state
                    selector.register(backend, selectors.EVENT_READ, state)
                if kind == 0xC0:
                    state["cid"] = header.source_cid
                    if state["original_dcid"] is None:
                        state["original_dcid"] = header.destination_cid.hex()
                    crypto = CryptoPair()
                    crypto.setup_initial(header.destination_cid, is_client=False,
                                         version=header.version)
                    _, _, pn = crypto.decrypt_packet(data, buffer.tell(),
                                                      state["initial_pn"] + 1)
                    if pn <= state["initial_pn"]:
                        stats["initial_pn_regressions"] += 1
                        stats["initial_pn_regression_events"].append(dict(
                            client_cid=header.source_cid.hex(), address=address,
                            previous=state["initial_pn"], packet_number=pn,
                            retry_delivered=state["retry"],
                            original_dcid=state["original_dcid"],
                            packet_dcid=header.destination_cid.hex(),
                            retry_scid=state["retry_scid"]))
                    state["initial_pn"] = max(pn, state["initial_pn"])
                if kind == 0xD0:
                    if not state["retry"]:
                        stats["early_before_retry"] += 1
                    elif not state["dropped"]:
                        state["dropped"] = True
                        stats["early_dropped_after_retry"] += 1
                        persist()
                        continue
                state["backend"].sendto(data, ("127.0.0.1", args.target_port))
                persist()
            else:
                state = key.data
                kind = data[0] & 0xF0
                retry = kind == 0xF0
                if retry:
                    stats["retries_delayed"] += 1
                    header = pull_quic_header(Buffer(data=data), host_cid_length=8)
                    state["retry_scid"] = header.source_cid.hex()
                # Keep the post-Retry TLS flight behind the recovered 0-RTT
                # submission so the injected loss is deterministic on IOCP.
                delay = 0.05 if retry else (0.02 if kind in (0xC0, 0xE0) else 0)
                sequence += 1
                heapq.heappush(pending, (time.monotonic() + delay, sequence,
                                        data, state, retry))
                persist()


if __name__ == "__main__":
    main()
