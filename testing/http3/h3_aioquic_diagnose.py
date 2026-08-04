#!/usr/bin/env python3
"""Event-level aioquic probe for a cnetmod HTTP/3 endpoint."""

import argparse
import asyncio
import json
from pathlib import Path

from aioquic.asyncio import connect
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.h3.connection import H3Connection
from aioquic.h3.events import DataReceived, HeadersReceived
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import ConnectionTerminated, HandshakeCompleted, ProtocolNegotiated, StreamDataReceived
from aioquic.quic.logger import QuicLogger


class Probe(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.http = None
        self.replies = {}
        self.bodies = {}
        self.terminated = None
        self._verbose = False

    def quic_event_received(self, event):
        if self._verbose:
            print(f"QUIC {event!r}", flush=True)
        if isinstance(event, ProtocolNegotiated):
            self.http = H3Connection(self._quic)
        elif isinstance(event, HandshakeCompleted):
            print(f"ALPN {event.alpn_protocol}", flush=True)
        elif isinstance(event, ConnectionTerminated):
            self.terminated = RuntimeError(
                f"peer close: error_code=0x{event.error_code:x}, "
                f"frame_type={event.frame_type}, reason={event.reason_phrase!r}")
            for reply in self.replies.values():
                if not reply.done():
                    reply.set_exception(self.terminated)
        elif isinstance(event, StreamDataReceived) and self.http is not None:
            for h3_event in self.http.handle_event(event):
                if self._verbose:
                    print(f"H3 {h3_event!r}", flush=True)
                if isinstance(h3_event, HeadersReceived):
                    continue
                if isinstance(h3_event, DataReceived):
                    body = self.bodies.setdefault(h3_event.stream_id, bytearray())
                    body.extend(h3_event.data)
                    reply = self.replies.get(h3_event.stream_id)
                    if h3_event.stream_ended and reply is not None and not reply.done():
                        reply.set_result(bytes(body))

    def configure(self, verbose):
        self._verbose = verbose

    def request(self, authority, path):
        stream_id = self._quic.get_next_available_stream_id()
        reply = asyncio.get_running_loop().create_future()
        self.replies[stream_id] = reply
        self.http.send_headers(stream_id, [
            (b":method", b"GET"),
            (b":scheme", b"https"),
            (b":authority", authority.encode()),
            (b":path", path.encode()),
            (b"user-agent", b"cnetmod-aioquic-diagnose"),
        ], end_stream=True)
        self.transmit()
        return reply


async def run(args):
    configuration = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
    configuration.verify_mode = False
    logger = QuicLogger()
    configuration.quic_logger = logger

    def write_qlog():
        if args.qlog:
            Path(args.qlog).write_text(json.dumps(logger.to_dict(), indent=2), encoding="utf-8")

    try:
        async with connect(args.host, args.port, configuration=configuration, create_protocol=Probe) as protocol:
            protocol.configure(args.verbose)
            try:
                await asyncio.wait_for(protocol.wait_connected(), timeout=args.timeout)
            except BaseException:
                # aioquic waits for the QUIC closing period when unwinding a
                # failed handshake. Persist the trace before that wait so a
                # protocol failure cannot hide the packet-level evidence.
                write_qlog()
                raise
            completed = 0
            authority = f"{args.host}:{args.port}"
            while completed < args.requests:
                count = min(args.parallel, args.requests - completed)
                replies = [protocol.request(authority, args.path) for _ in range(count)]
                bodies = await asyncio.wait_for(asyncio.gather(*replies), timeout=args.timeout)
                if any(body != b"ok\n" for body in bodies):
                    raise RuntimeError("unexpected response body")
                completed += count
                if args.verbose or completed == args.requests or completed % 100 == 0:
                    print(f"COMPLETED {completed}/{args.requests}", flush=True)
    finally:
        write_qlog()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("port", type=int)
    parser.add_argument("path", nargs="?", default="/health")
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--requests", type=int, default=1)
    parser.add_argument("--parallel", type=int, default=1)
    parser.add_argument("--qlog", help="write aioquic qlog JSON even when the handshake fails")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
