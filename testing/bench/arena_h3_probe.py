#!/usr/bin/env python3
"""Independent aioquic correctness gate for the baseline-h3 adapter."""
import argparse
import asyncio
import ssl

from aioquic.asyncio import connect
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.h3.connection import H3Connection
from aioquic.h3.events import DataReceived, HeadersReceived
from aioquic.quic.configuration import QuicConfiguration


class Probe(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.http = H3Connection(self._quic)
        self.pending = {}

    def quic_event_received(self, event):
        for received in self.http.handle_event(event):
            if not isinstance(received, (HeadersReceived, DataReceived)) or received.stream_id not in self.pending:
                continue
            future, headers, body = self.pending[received.stream_id]
            if isinstance(received, HeadersReceived):
                headers.update(received.headers)
            else:
                body.extend(received.data)
            if received.stream_ended:
                self.pending.pop(received.stream_id)
                future.set_result((headers, bytes(body)))

    async def request(self, path):
        stream = self._quic.get_next_available_stream_id()
        future = asyncio.get_running_loop().create_future()
        self.pending[stream] = (future, {}, bytearray())
        self.http.send_headers(stream, [(b":method", b"GET"), (b":scheme", b"https"),
                                      (b":authority", b"localhost"), (b":path", path.encode())], end_stream=True)
        self.transmit()
        return await asyncio.wait_for(future, 10)


async def run(port, workload_only=False):
    config = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
    config.verify_mode = ssl.CERT_NONE  # loopback fixture certificate only
    async with connect("127.0.0.1", port, configuration=config, create_protocol=Probe) as client:
        cases = [("/baseline2?a=1&b=1", b"2"), ("/baseline2?a=1&a=2&b=-3", b"0"),
                 ("/baseline2?a=%2B7&b=%2D2", b"5"), ("/baseline2?bad=x&v=12tail&ok=3", b"3"),
                 ("/baseline2", b"0"), ("/baseline2?pad=" + "x" * 6000 + "&a=7", b"7")]
        if workload_only:
            cases = [("/baseline2?a=1&b=1", b"2"), ("/baseline2?a=5&b=7", b"12")]
        for path, expected in cases:
            headers, body = await client.request(path)
            assert headers[b":status"] == b"200" and body == expected, (path, headers, body)
            assert headers[b"content-type"] == b"text/plain"
        headers, _ = await client.request("/missing")
        assert headers[b":status"] == b"404"
        # Cross the initial stream-credit boundary on a reused connection.
        for _ in range(4):
            results = await asyncio.gather(*(client.request("/baseline2?a=1&b=1") for _ in range(64)))
            assert all(h[b":status"] == b"200" and body == b"2" for h, body in results)
    print("PASS: H3 query semantics, 404, 64 concurrent streams, 256 reused streams")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument("--workload-only", action="store_true",
                        help="validate only the unique-key query workload used by HttpArena")
    args = parser.parse_args()
    asyncio.run(run(args.port, args.workload_only))
