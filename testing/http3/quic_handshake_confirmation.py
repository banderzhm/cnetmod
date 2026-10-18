"""Drop actual HANDSHAKE_DONE, then prove confirmed-client application PTO.

TLS completion is deliberately observed separately from handshake confirmation.
The first phase sends no application data or PINGs. The second phase discards
the first real request STREAM packet, and requires a new-PN retransmission and
both response FIN delivery and request FIN acknowledgement.
"""

from __future__ import annotations

import argparse
import asyncio
import json
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import time

try:
    from aioquic.asyncio import connect
    from aioquic.buffer import Buffer
    from aioquic.quic.configuration import QuicConfiguration
    from aioquic.quic.crypto import CryptoContext
    from aioquic.quic.packet import pull_quic_header
    from aioquic.tls import Epoch
except ImportError:
    raise SystemExit(77)

from http_client_e2e import make_certificate, reserve_port, stop_process
from quic_retire_stream import RawPeer, wait_until
from http3_outbound_pto import response_stream_frames


def parse_frames(payload, target, kinds):
    """Parse fixture QUIC frames without delivering or acknowledging them."""
    buffer = Buffer(data=payload)
    found = []
    while buffer.tell() < len(payload):
        kind = buffer.pull_uint_var()
        kinds.append(kind)
        if kind in (0, 1, 0x1E):  # PADDING, PING, HANDSHAKE_DONE
            continue
        if kind in (2, 3):
            buffer.pull_uint_var()  # largest acknowledged
            buffer.pull_uint_var()  # ACK delay
            ranges = buffer.pull_uint_var()
            buffer.pull_uint_var()
            for _ in range(ranges):
                buffer.pull_uint_var()
                buffer.pull_uint_var()
            if kind == 3:
                for _ in range(3):
                    buffer.pull_uint_var()
        elif 8 <= kind <= 15:
            stream = buffer.pull_uint_var()
            offset = buffer.pull_uint_var() if kind & 4 else 0
            length = buffer.pull_uint_var() if kind & 2 else len(payload) - buffer.tell()
            buffer.pull_bytes(length)
            if stream == target:
                found.append({"offset": offset, "length": length, "fin": bool(kind & 1)})
        elif kind in (4,):  # RESET_STREAM
            for _ in range(3):
                buffer.pull_uint_var()
        elif kind in (5, 0x11, 0x15):
            buffer.pull_uint_var()
            buffer.pull_uint_var()
        elif kind == 6:  # CRYPTO
            buffer.pull_uint_var()
            buffer.pull_bytes(buffer.pull_uint_var())
        elif kind == 7:  # NEW_TOKEN
            buffer.pull_bytes(buffer.pull_uint_var())
        elif kind in (0x10, 0x12, 0x13, 0x14, 0x16, 0x17, 0x19):
            buffer.pull_uint_var()
        elif kind == 0x18:  # NEW_CONNECTION_ID
            buffer.pull_uint_var()
            buffer.pull_uint_var()
            buffer.pull_bytes(buffer.pull_uint8())
            buffer.pull_bytes(16)
        elif kind in (0x1A, 0x1B):
            buffer.pull_bytes(8)
        elif kind in (0x1C, 0x1D):
            buffer.pull_uint_var()
            if kind == 0x1C:
                buffer.pull_uint_var()
            buffer.pull_bytes(buffer.pull_uint_var())
        elif kind in (0x30, 0x31):
            length = buffer.pull_uint_var() if kind & 1 else len(payload) - buffer.tell()
            buffer.pull_bytes(length)
        else:
            raise AssertionError(f"unhandled fixture QUIC frame {kind:#x}")
    return found


class Backend(asyncio.DatagramProtocol):
    def __init__(self, owner):
        self.owner = owner

    def datagram_received(self, data, address):
        self.owner.from_server(data)

    def error_received(self, error):
        self.owner.result["socket_errors"].append(repr(error))


class ConfirmationProxy(asyncio.DatagramProtocol):
    def __init__(self, port, result):
        self.port, self.result = port, result
        self.peer = self.front = self.back = self.client = None
        self.server_crypto = self.client_crypto = None
        self.request_started = False
        self.server_expected = self.client_expected = 0
        result.update(done_packets=[], request_packets=[], socket_errors=[],
                      decode_errors=[], dropped_done=False, dropped_request=False,
                      client_packets_blocked_before_confirmation=0, client_application_pto_count=0)

    async def open(self):
        loop = asyncio.get_running_loop()
        self.back, _ = await loop.create_datagram_endpoint(
            lambda: Backend(self), remote_addr=("127.0.0.1", self.port))
        self.front, _ = await loop.create_datagram_endpoint(
            lambda: self, local_addr=("127.0.0.1", 0))
        return self.front.get_extra_info("sockname")[1]

    def decrypt(self, data, server):
        if not self.peer or data[0] & 0x80:
            return None
        name = "server_crypto" if server else "client_crypto"
        crypto = getattr(self, name)
        if crypto is None:
            pair = self.peer._quic._cryptos[Epoch.ONE_RTT]
            source = pair.recv if server else pair.send
            if not source.is_valid():
                raise AssertionError("1-RTT keys absent before short-header packet")
            crypto = CryptoContext(key_phase=source.key_phase)
            crypto.setup(cipher_suite=source.cipher_suite, secret=source.secret,
                         version=source.version)
            setattr(self, name, crypto)
        header = Buffer(data=data)
        pull_quic_header(header, host_cid_length=8)
        expected = self.server_expected if server else self.client_expected
        _, payload, number, _ = crypto.decrypt_packet(data, header.tell(), expected)
        if server:
            self.server_expected = max(expected, number + 1)
        else:
            self.client_expected = max(expected, number + 1)
        # Parse every frame. HANDSHAKE_DONE is one byte and can only be in a
        # frame position; searching ciphertext/plaintext bytes is insufficient.
        kinds = []
        parse_frames(payload, 0, kinds)
        return number, payload, kinds

    def from_server(self, data):
        try:
            decoded = self.decrypt(data, True)
            if decoded and 0x1E in decoded[2]:
                number, _, kinds = decoded
                self.result["done_packets"].append({"pn": number, "bytes": len(data),
                    "at_ms": (time.monotonic() - self.started) * 1000,
                    "frames": kinds})
                if not self.result["dropped_done"]:
                    assert not self.peer._quic._handshake_confirmed
                    self.result["dropped_done"] = True
                    return
        except Exception as error:
            self.result["decode_errors"].append(repr(error))
        self.front.sendto(data, self.client)

    def datagram_received(self, data, address):
        if self.client is None:
            self.client = address
        assert self.client == address
        if (self.result["dropped_done"] and
                not self.peer._quic._handshake_confirmed):
            self.result["client_packets_blocked_before_confirmation"] += 1
            return
        try:
            decoded = self.decrypt(data, False)
            if decoded:
                number, payload, kinds = decoded
                if self.request_started and any(
                        frame["length"] for frame in response_stream_frames(payload, 0)):
                    self.result["client_application_pto_count"] = max(
                        self.result["client_application_pto_count"], self.peer._quic._loss._pto_count)
                    self.result["request_packets"].append({"pn": number, "bytes": len(data),
                        "at_ms": (time.monotonic() - self.request_at) * 1000})
                    if not self.result["dropped_request"]:
                        self.result["dropped_request"] = True
                        return
        except Exception as error:
            self.result["decode_errors"].append(repr(error))
        self.back.sendto(data)

    def error_received(self, error):
        self.result["socket_errors"].append(repr(error))

    def close(self):
        for transport in (self.front, self.back):
            if transport:
                transport.close()


async def exercise(port, result):
    proxy = ConfirmationProxy(port, result)
    proxy_port = await proxy.open()
    config = QuicConfiguration(is_client=True, alpn_protocols=["quic-retire-test"])
    config.verify_mode = ssl.CERT_NONE
    config.idle_timeout = 6

    def create(*args, **kwargs):
        peer = RawPeer(*args, **kwargs)
        proxy.peer = peer
        return peer

    proxy.started = time.monotonic()
    try:
        async with connect("127.0.0.1", proxy_port, configuration=config,
                           create_protocol=create) as peer:
            result["tls_completed"] = peer._quic._handshake_complete
            assert result["tls_completed"]
            result["confirmed_after_tls_complete"] = peer._quic._handshake_confirmed
            await wait_until(lambda: peer._quic._handshake_confirmed, 3,
                             "TLS completed but HANDSHAKE_DONE never confirmed handshake")
            result["handshake_confirmed"] = peer._quic._handshake_confirmed
            assert result["dropped_done"], "no actual HANDSHAKE_DONE packet was dropped"
            numbers = [item["pn"] for item in result["done_packets"]]
            assert len(numbers) >= 2 and len(set(numbers)) == len(numbers), numbers
            assert not result["decode_errors"], result
            # No application traffic is sent before confirmation. A lost DONE
            # must be recovered autonomously rather than by a business packet.
            await asyncio.sleep(0.05)
            proxy.request_started = True
            proxy.request_at = time.monotonic()
            peer._quic.send_stream_data(0, b"retire request\n", end_stream=True)
            sender = peer._quic._streams[0].sender
            peer.transmit()
            assert await asyncio.wait_for(peer.response, 1.5) == b"retire response\n"
            await wait_until(lambda: sender.is_finished, 1,
                             "request FIN not acknowledged after client application PTO")
            result["request_fin_acked"] = True
            numbers = [item["pn"] for item in result["request_packets"]]
            assert result["dropped_request"] and len(set(numbers)) >= 2, result
            assert result["client_application_pto_count"] > 0, result
            assert not result["decode_errors"] and not result["socket_errors"], result
            # An authenticated client must never send HANDSHAKE_DONE. Inject
            # the actual frame through aioquic's packet builder, then demand
            # the precise transport error and triggering frame type.
            original_ping = peer._quic._write_ping_frame
            injected = []
            def invalid_done(builder, uids=(), comment=""):
                builder.start_frame(0x1E, capacity=1)
                injected.append(builder.packet_number)
            peer._quic._write_ping_frame = invalid_done
            try:
                peer._quic.send_ping(7171)
                peer.transmit()
            finally:
                peer._quic._write_ping_frame = original_ping
            assert injected, "illegal role fixture did not encode HANDSHAKE_DONE"
            await wait_until(lambda: peer._quic._close_event is not None, 1,
                             "server did not reject client HANDSHAKE_DONE")
            event = peer._quic._close_event
            result["illegal_client_done_packet_numbers"] = injected
            result["illegal_client_done_error_code"] = event.error_code
            result["illegal_client_done_frame_type"] = event.frame_type
            assert event.error_code == 0xA and event.frame_type == 0x1E, repr(event)
    finally:
        proxy.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server", type=Path)
    parser.add_argument("--mode", choices=("observer", "private"), default="observer")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = {"case": "handshake_confirmation", "mode": args.mode, "status": "failed"}
    with tempfile.TemporaryDirectory(prefix="cnetmod-confirmation-") as directory:
        root = Path(directory)
        cert, key = make_certificate(root)
        port = reserve_port()
        command = [str(args.server.resolve()), "--port", str(port), "--mode", args.mode,
                   "--cert", str(cert), "--key", str(key),
                   "--listening-report", str(root / "listening"),
                   "--ready-report", str(root / "ready"),
                   "--retire-trigger", str(root / "trigger"),
                   "--result-report", str(root / "result")]
        server = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 5
            while not (root / "listening").exists() or not (root / "listening").read_text().strip():
                assert server.poll() is None and time.monotonic() < deadline, "fixture not ready"
                time.sleep(0.01)
            asyncio.run(exercise(port, result))
            result["status"] = "passed"
        except Exception as error:
            result["error"] = repr(error)
        finally:
            result["server_stdout"], result["server_stderr"] = stop_process(server)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, sort_keys=True), flush=True)
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
