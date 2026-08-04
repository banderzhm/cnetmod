#!/usr/bin/env python3
"""Small aioquic HTTP/3 server used only as an interoperability peer."""

import argparse
import asyncio
import sys


def main() -> int:
    try:
        from aioquic.asyncio import serve
        from aioquic.asyncio.protocol import QuicConnectionProtocol
        from aioquic.h3.connection import H3Connection
        from aioquic.h3.events import HeadersReceived
        from aioquic.quic.configuration import QuicConfiguration
        from aioquic.quic.events import ConnectionTerminated, HandshakeCompleted, ProtocolNegotiated, StreamDataReceived
    except ImportError as exc:
        print(f"SKIP: aioquic unavailable: {exc}")
        return 77

    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    args = parser.parse_args()

    class Peer(QuicConnectionProtocol):
        def quic_event_received(self, event):
            print(f"QUIC {event!r}", file=sys.stderr, flush=True)
            if isinstance(event, ProtocolNegotiated):
                self.http = H3Connection(self._quic)
            elif isinstance(event, HandshakeCompleted):
                print(f"ALPN {event.alpn_protocol}", file=sys.stderr, flush=True)
            elif isinstance(event, ConnectionTerminated):
                return
            if isinstance(event, StreamDataReceived):
                for http_event in self.http.handle_event(event):
                    print(f"H3 {http_event!r}", file=sys.stderr, flush=True)
                    if isinstance(http_event, HeadersReceived):
                        self.http.send_headers(http_event.stream_id, [(b":status", b"200"), (b"content-type", b"text/plain")])
                        self.http.send_data(http_event.stream_id, b"ok\n", end_stream=True)
                self.transmit()

    configuration = QuicConfiguration(is_client=False, alpn_protocols=["h3"])
    configuration.load_cert_chain(args.cert, args.key)

    async def run() -> None:
        await serve("127.0.0.1", args.port, configuration=configuration, create_protocol=Peer)
        await asyncio.Future()

    asyncio.run(run())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
