#!/usr/bin/env python3
"""RFC 9220 WebTransport probe: aioquic client -> cnetmod HTTP/3 server.

This is intentionally a small executable interoperability check, rather than
a mock: it opens a real QUIC connection, performs Extended CONNECT, sends a
WebTransport child stream and an HTTP Datagram, and expects both to be echoed
by the cnetmod ``h3_interop_server --webtransport`` fixture.
"""

from __future__ import annotations

import argparse
import asyncio
import ssl
import sys

try:
    from aioquic.asyncio import connect
    from aioquic.asyncio.protocol import QuicConnectionProtocol
    from aioquic.h3.connection import H3Connection
    from aioquic.h3.events import DataReceived, DatagramReceived, HeadersReceived, WebTransportStreamDataReceived
    from aioquic.quic.configuration import QuicConfiguration
except ImportError as error:
    print(f"SKIP: aioquic unavailable: {error}")
    raise SystemExit(77)


def decode_varint(data: bytes, offset: int = 0) -> tuple[int, int]:
    if offset >= len(data):
        raise ValueError("truncated QUIC variable-length integer")
    size = 1 << (data[offset] >> 6)
    end = offset + size
    if end > len(data):
        raise ValueError("truncated QUIC variable-length integer")
    return int.from_bytes(data[offset:end], "big") & ((1 << (size * 8 - 2)) - 1), end


def decode_close_capsule(data: bytes) -> tuple[int, str]:
    capsule_type, offset = decode_varint(data)
    if capsule_type != 0x2843:
        raise ValueError(f"expected CLOSE_WEBTRANSPORT_SESSION capsule, got {capsule_type:#x}")
    payload_size, offset = decode_varint(data, offset)
    end = offset + payload_size
    if end != len(data):
        raise ValueError("invalid Close Capsule length")
    error_code, offset = decode_varint(data, offset)
    return error_code, data[offset:end].decode("utf-8", errors="strict")


class WebTransportProbeProtocol(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self.h3 = H3Connection(self._quic, enable_webtransport=True)
        loop = asyncio.get_running_loop()
        self.health_response: asyncio.Future[tuple[int, bytes]] = loop.create_future()
        self.health_body = bytearray()
        self.health_stream_id: int | None = None
        self.connect_response: asyncio.Future[int] = loop.create_future()
        self.stream_echo: asyncio.Future[bytes] = loop.create_future()
        self.datagram_echo: asyncio.Future[bytes] = loop.create_future()
        self.close_info: asyncio.Future[tuple[int, str]] = loop.create_future()
        self.session_id: int | None = None
        self.events: list[str] = []
        self.close_wire = bytearray()

    def quic_event_received(self, event) -> None:
        for h3_event in self.h3.handle_event(event):
            stream_id = getattr(h3_event, "stream_id", "-")
            self.events.append(f"{type(h3_event).__name__}(stream={stream_id})")
            if isinstance(h3_event, HeadersReceived) and self.health_stream_id == h3_event.stream_id:
                status = next((value for name, value in h3_event.headers if name == b":status"), b"0")
                if h3_event.stream_ended and not self.health_response.done():
                    self.health_response.set_result((int(status), bytes(self.health_body)))
            elif isinstance(h3_event, HeadersReceived) and self.session_id == h3_event.stream_id:
                status = next((value for name, value in h3_event.headers if name == b":status"), b"0")
                if not self.connect_response.done():
                    self.connect_response.set_result(int(status))
            elif isinstance(h3_event, WebTransportStreamDataReceived):
                if h3_event.session_id == self.session_id and not self.stream_echo.done():
                    self.stream_echo.set_result(h3_event.data)
            elif isinstance(h3_event, DatagramReceived):
                if h3_event.stream_id == self.session_id and not self.datagram_echo.done():
                    self.datagram_echo.set_result(h3_event.data)
            elif isinstance(h3_event, DataReceived) and h3_event.stream_id == self.health_stream_id:
                self.health_body.extend(h3_event.data)
                if h3_event.stream_ended and not self.health_response.done():
                    self.health_response.set_result((200, bytes(self.health_body)))
            elif isinstance(h3_event, DataReceived) and h3_event.stream_id == self.session_id:
                self.close_wire.extend(h3_event.data)
                if h3_event.stream_ended and not self.close_info.done():
                    try:
                        self.close_info.set_result(decode_close_capsule(bytes(self.close_wire)))
                    except ValueError as error:
                        self.close_info.set_exception(error)

    def open_session(self, authority: str, path: str) -> int:
        session_id = self._quic.get_next_available_stream_id()
        self.session_id = session_id
        self.h3.send_headers(
            session_id,
            [
                (b":method", b"CONNECT"),
                (b":scheme", b"https"),
                (b":authority", authority.encode()),
                (b":path", path.encode()),
                (b":protocol", b"webtransport"),
                (b"sec-webtransport-http3-draft02", b"1"),
            ],
            end_stream=False,
        )
        self.transmit()
        return session_id

    def get_health(self, authority: str) -> int:
        stream_id = self._quic.get_next_available_stream_id()
        self.health_stream_id = stream_id
        self.h3.send_headers(
            stream_id,
            [
                (b":method", b"GET"),
                (b":scheme", b"https"),
                (b":authority", authority.encode()),
                (b":path", b"/health"),
            ],
            end_stream=True,
        )
        self.transmit()
        return stream_id

async def run(host: str, port: int, path: str, timeout: float) -> None:
    configuration = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
    configuration.verify_mode = ssl.CERT_NONE
    # RFC 9221 transport-parameter negotiation is separate from HTTP/3's
    # SETTINGS_H3_DATAGRAM. Both are required before aioquic emits DATAGRAM
    # frames.
    configuration.max_datagram_frame_size = 1200
    async with connect(
        host,
        port,
        configuration=configuration,
        create_protocol=WebTransportProbeProtocol,
        wait_connected=True,
    ) as protocol:
        assert isinstance(protocol, WebTransportProbeProtocol)
        authority = f"{host}:{port}"
        # First prove ordinary HTTP/3 request/response interoperability. It
        # consumes stream 0, making the WebTransport CONNECT use stream 4;
        # that validates RFC 9297's quarter-stream-id DATAGRAM context.
        if protocol.get_health(authority) != 0:
            raise RuntimeError("expected health request stream 0")
        try:
            health_status, health_body = await asyncio.wait_for(protocol.health_response, timeout)
        except asyncio.TimeoutError as error:
            raise RuntimeError("timed out waiting for ordinary HTTP/3 response") from error
        if health_status != 200 or health_body != b"ok\n":
            raise RuntimeError(
                f"ordinary HTTP/3 GET /health failed: status={health_status}, body={health_body!r}"
            )
        session_id = protocol.open_session(authority, path)
        if session_id != 4:
            raise RuntimeError(f"expected WebTransport session stream 4, got {session_id}")
        try:
            status = await asyncio.wait_for(protocol.connect_response, timeout)
        except asyncio.TimeoutError as error:
            raise RuntimeError("timed out waiting for Extended CONNECT response") from error
        if status < 200 or status >= 300:
            raise RuntimeError(f"Extended CONNECT rejected with HTTP {status}")

        stream_id = protocol.h3.create_webtransport_stream(session_id)
        # The shared cnetmod fixture validates the bidirectional payload and
        # accepts a second (unidirectional) WebTransport child stream before
        # echoing the Datagram.
        protocol._quic.send_stream_data(stream_id, b"strm", end_stream=True)
        unidirectional_stream = protocol.h3.create_webtransport_stream(
            session_id, is_unidirectional=True
        )
        protocol._quic.send_stream_data(unidirectional_stream, b"", end_stream=True)
        protocol.h3.send_datagram(session_id, b"aioquic-datagram")
        protocol.transmit()

        try:
            datagram_echo = await asyncio.wait_for(protocol.datagram_echo, timeout)
        except asyncio.TimeoutError as error:
            raise RuntimeError(
                "timed out waiting for HTTP Datagram echo; "
                f"received HTTP/3 events: {protocol.events}"
            ) from error
        if datagram_echo != b"aioquic-datagram":
            raise RuntimeError(f"datagram echo mismatch: {datagram_echo!r}")
        # The fixture deliberately waits for this acknowledgement before
        # emitting CLOSE_WEBTRANSPORT_SESSION. DATAGRAM and close capsules do
        # not share ordered delivery, so this keeps the test deterministic.
        protocol.h3.send_datagram(session_id, b"close-ack")
        protocol.transmit()
        try:
            close_code, close_reason = await asyncio.wait_for(protocol.close_info, timeout)
        except asyncio.TimeoutError as error:
            raise RuntimeError("timed out waiting for Close Capsule") from error
        if (close_code, close_reason) != (7, "echo complete"):
            raise RuntimeError(f"unexpected Close Capsule: code={close_code}, reason={close_reason!r}")
        print("HTTP/3 GET + WebTransport aioquic -> cnetmod: CONNECT, child streams, Datagram, Close Capsule: ok")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("port", type=int)
    parser.add_argument("--path", default="/webtransport")
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()
    try:
        asyncio.run(run(args.host, args.port, args.path, args.timeout))
    except Exception as error:
        print(f"FAILED: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
