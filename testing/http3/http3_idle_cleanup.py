"""Verify that a silent peer is removed by the listener's idle timer."""

import asyncio
import os
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import time

try:
    from aioquic.asyncio import QuicConnectionProtocol, connect
    from aioquic.h3.connection import H3Connection, H3_ALPN
    from aioquic.h3.events import DataReceived, HeadersReceived
    from aioquic.quic.configuration import QuicConfiguration
except ImportError:
    print("aioquic is required for the idle-cleanup regression")
    raise SystemExit(77)

from http_client_e2e import make_certificate, reserve_port, wait_for_server


class Probe(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.h3 = H3Connection(self._quic)
        self.responses = {}

    def quic_event_received(self, event):
        for item in self.h3.handle_event(event):
            response = self.responses.get(getattr(item, "stream_id", None))
            if response is None:
                continue
            if isinstance(item, HeadersReceived):
                response["status"] = dict(item.headers).get(b":status")
            elif isinstance(item, DataReceived):
                response["body"].extend(item.data)
            if getattr(item, "stream_ended", False) and not response["done"].done():
                response["done"].set_result(None)

    async def get_count(self, port):
        stream = self._quic.get_next_available_stream_id()
        response = {"done": asyncio.get_running_loop().create_future(),
                    "status": None, "body": bytearray()}
        self.responses[stream] = response
        self.h3.send_headers(stream, [(b":method", b"GET"), (b":scheme", b"https"),
            (b":authority", f"127.0.0.1:{port}".encode()),
            (b":path", b"/active-connections")], end_stream=True)
        self.transmit()
        await asyncio.wait_for(response["done"], 5)
        assert response["status"] == b"200", response
        self.responses.pop(stream)
        return int(response["body"])


async def probe(port, abandon=False):
    config = QuicConfiguration(is_client=True, alpn_protocols=H3_ALPN,
                               verify_mode=ssl.CERT_NONE, idle_timeout=60)
    async with connect("127.0.0.1", port, configuration=config,
                       create_protocol=Probe) as client:
        if abandon:
            await client.get_count(port)
            # Deliberately bypass QUIC close and context-manager cleanup.
            # A closed UDP socket sends no CONNECTION_CLOSE frame.
            os._exit(0)
        for _ in range(3):
            child = await asyncio.create_subprocess_exec(sys.executable,
                str(Path(__file__).resolve()), "--abandon", str(port))
            assert await child.wait() == 0
        initial = await client.get_count(port)
        assert initial >= 4, f"silent peers not retained initially: {initial}"
        started = time.monotonic()
        samples = []
        while time.monotonic() - started < 40:
            count = await client.get_count(port)
            samples.append((round(time.monotonic() - started, 2), count))
            if count == 1:
                print(f"idle peers reclaimed: {initial} -> {count}; samples={samples}")
                return
            await asyncio.sleep(1)
        raise AssertionError(f"listener retained timed-out peers: {samples}")


def main():
    if sys.argv[1] == "--abandon":
        asyncio.run(probe(int(sys.argv[2]), abandon=True))
        return
    server_binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as directory:
        cert, key = make_certificate(Path(directory))
        port = reserve_port()
        server = subprocess.Popen([str(server_binary), "--port", str(port),
            "--cert", str(cert), "--key", str(key)], cwd=directory,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            wait_for_server(server)
            asyncio.run(probe(port))
        finally:
            server.terminate()
            try:
                server.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.communicate()


if __name__ == "__main__":
    main()
