"""Targeted outbound PTO and peer-CLOSE regressions against h3_interop_server.

The PTO proxy drops actual response STREAM data, not an arbitrary ACK packet.
After the request FIN has been acknowledged it blocks all client ingress until
the response completes. Recovery must therefore come from the server's timer.
Each case starts a fresh listener; --restart also exercises stop/start handoff.
"""

from __future__ import annotations

import argparse
import asyncio
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

try:
    from aioquic.asyncio import QuicConnectionProtocol, connect
    from aioquic.buffer import Buffer
    from aioquic.h3.connection import H3Connection
    from aioquic.h3.events import DataReceived, HeadersReceived
    from aioquic.quic.crypto import CryptoContext
    from aioquic.quic.events import ConnectionTerminated, StreamReset
    from aioquic.quic.packet import pull_quic_header
    from aioquic.tls import Epoch
except ImportError:
    raise SystemExit(77)

from http3_lifecycle import configuration
from http_client_e2e import make_certificate, reserve_port, stop_process


class HttpProbe(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.h3 = H3Connection(self._quic)
        self.responses = {}
        self.last_termination = None

    def quic_event_received(self, event):
        if isinstance(event, ConnectionTerminated):
            self.last_termination = event
        if isinstance(event, (ConnectionTerminated, StreamReset)):
            for stream, response in self.responses.items():
                if (isinstance(event, ConnectionTerminated) or
                        stream == event.stream_id) and not response["done"].done():
                    response["done"].set_exception(AssertionError(repr(event)))
        for item in self.h3.handle_event(event):
            response = self.responses.get(getattr(item, "stream_id", None))
            if response is None:
                continue
            if isinstance(item, HeadersReceived):
                response["status"] = dict(item.headers).get(b":status")
            elif isinstance(item, DataReceived):
                response["body"].extend(item.data)
            if getattr(item, "stream_ended", False) and not response["done"].done():
                response["done"].set_result(
                    (response["status"], bytes(response["body"])))

    def start_get(self, port, path):
        stream = self._quic.get_next_available_stream_id()
        future = asyncio.get_running_loop().create_future()
        self.responses[stream] = {"status": None, "body": bytearray(), "done": future}
        self.h3.send_headers(stream, [
            (b":method", b"GET"), (b":scheme", b"https"),
            (b":authority", f"localhost:{port}".encode()), (b":path", path),
        ], end_stream=True)
        self.transmit()
        return stream, future

    async def get(self, port, path, timeout=5):
        _, future = self.start_get(port, path)
        status, body = await asyncio.wait_for(future, timeout)
        assert status == b"200", (status, body)
        return body


def response_stream_frames(payload, target):
    """Parse fixture QUIC frames without delivering or acknowledging them."""
    buffer = Buffer(data=payload)
    found = []
    while buffer.tell() < len(payload):
        kind = buffer.pull_uint_var()
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
        self.owner.record_socket_error(error, "backend")


class ResponseLossProxy(asyncio.DatagramProtocol):
    def __init__(self, target_port, stats):
        self.target_port = target_port
        self.stats = stats
        self.front = None
        self.backend = None
        self.client = None
        self.crypto = None
        self.target_stream = None
        self.started = None
        self.gated = False
        self.last_client_short = None
        self.reset_token = None
        self.probe_window_started = None
        self.probe_replies = []
        self.reset_received_at = None
        stats.update(client_forwarded=0, client_blocked=0,
                     client_forwarded_while_gated=0, server_forwarded_while_gated=0,
                     response_datagrams_dropped=0, socket_errors=[], decode_errors=[],
                     retransmitted_stream_frames=[], control_only_after_drop=0,
                     socket_error_events=[])

    async def open(self):
        loop = asyncio.get_running_loop()
        self.backend, _ = await loop.create_datagram_endpoint(
            lambda: Backend(self), remote_addr=("127.0.0.1", self.target_port))
        self.front, _ = await loop.create_datagram_endpoint(
            lambda: self, local_addr=("127.0.0.1", 0))
        return self.front.get_extra_info("sockname")[1]

    def datagram_received(self, data, address):
        if data and not data[0] & 0x80:
            self.last_client_short = data
        if self.client is None:
            self.client = address
        assert self.client == address, "proxy accepts exactly one QUIC client"
        if self.gated:
            self.stats["client_blocked"] += 1
            return
        self.stats["client_forwarded"] += 1
        self.backend.sendto(data)

    def error_received(self, error):
        self.record_socket_error(error, "front")

    def record_socket_error(self, error, source):
        self.stats["socket_errors"].append(repr(error))
        self.stats["socket_error_events"].append({
            "source": source, "error": repr(error),
            "after_matching_reset": self.reset_received_at is not None,
            "at_ms": ((time.monotonic() - self.probe_window_started) * 1000
                      if self.probe_window_started is not None else None)})

    def arm(self, peer, stream, started):
        source = peer._quic._cryptos[Epoch.ONE_RTT].recv
        assert source.is_valid(), "client 1-RTT receive keys are unavailable"
        # Copy the traffic secret into a separate decryptor. The proxy never
        # calls receive_datagram on the client or mutates its packet/ACK state.
        self.crypto = CryptoContext(key_phase=source.key_phase)
        self.crypto.setup(cipher_suite=source.cipher_suite,
                          secret=source.secret, version=source.version)
        self.cid_length = peer._quic.configuration.connection_id_length
        self.target_stream = stream
        self.started = started
        self.gated = True
        self.stats["gate_at_ms"] = (time.monotonic() - started) * 1000
        self.stats["client_forwarded_at_gate"] = self.stats["client_forwarded"]

    def from_server(self, data):
        if self.probe_window_started is not None:
            reset = (self.reset_token is not None and len(data) >= 21 and
                     data[-16:] == self.reset_token)
            self.probe_replies.append({"at_ms": (time.monotonic() - self.probe_window_started) * 1000,
                                       "bytes": len(data), "reset_token_match": reset})
            if reset and self.reset_received_at is None:
                self.reset_received_at = time.monotonic()
        if self.gated and self.crypto is not None and not data[0] & 0x80:
            try:
                header = Buffer(data=data)
                pull_quic_header(header, host_cid_length=self.cid_length)
                _, payload, packet_number, _ = self.crypto.decrypt_packet(data, header.tell(), 0)
                frames = response_stream_frames(payload, self.target_stream)
                if (not self.stats["response_datagrams_dropped"] and
                        any(frame["length"] for frame in frames)):
                    self.stats["response_datagrams_dropped"] = 1
                    self.stats["dropped_packet_number"] = packet_number
                    self.stats["dropped_datagram_bytes"] = len(data)
                    self.stats["dropped_stream_frames"] = frames
                    self.stats["drop_at_ms"] = (time.monotonic() - self.started) * 1000
                    return
                if self.stats["response_datagrams_dropped"]:
                    if frames:
                        self.stats["retransmitted_stream_frames"].extend(frames)
                    else:
                        self.stats["control_only_after_drop"] += 1
            except Exception as error:
                self.stats["decode_errors"].append(repr(error))
                # Unknown packets must not be mistaken for a successful loss
                # injection. Final assertions reject every decoding failure.
        if self.gated:
            self.stats["server_forwarded_while_gated"] += 1
        self.front.sendto(data, self.client)

    def release(self):
        self.gated = False

    def close(self):
        if self.front:
            self.front.close()
        if self.backend:
            self.backend.close()


async def check_pto(port, result, timeout, full_frame=False):
    proxy = ResponseLossProxy(port, result)
    proxy_port = await proxy.open()
    try:
        async with connect("127.0.0.1", proxy_port, configuration=configuration(),
                           create_protocol=HttpProbe) as peer:
            assert await peer.get(port, b"/health") == b"ok\n"
            # Let ACKs of the warmup/control flights clear the listener's old
            # recovery work before this asynchronous response creates new work.
            await asyncio.sleep(0.05)
            path = b"/delayed-pto-full-frame" if full_frame else b"/delayed-pto"
            stream, future = peer.start_get(port, path)
            started = time.monotonic()
            await asyncio.sleep(0.1)
            sender = peer._quic._streams[stream].sender
            result["request_fin_acked"] = sender.is_finished
            assert sender.is_finished, "request data/FIN not ACKed before ingress gate"
            assert not future.done(), "delayed fixture responded before the loss gate"
            proxy.arm(peer, stream, started)
            try:
                remaining = timeout - (time.monotonic() - started)
                assert remaining > 0, "fixture setup consumed the response budget"
                status, body = await asyncio.wait_for(future, remaining)
                result["response_at_ms"] = (time.monotonic() - started) * 1000
                expected_body = b"x" * 1158 if full_frame else b"delayed pto response\n"
                assert status == b"200" and body == expected_body, (status, body)
                assert result["response_datagrams_dropped"] == 1, "no actual response data was dropped"
                if full_frame:
                    # Verify this is the exact unsplittable PTO atom boundary,
                    # rather than a convenient small fragment of a large body.
                    assert result["dropped_datagram_bytes"] == 1199, result
                    atom = {"offset": 0, "length": 1166, "fin": True}
                    assert result["dropped_stream_frames"] == [atom], result
                    assert atom in result["retransmitted_stream_frames"], result
                assert not result["decode_errors"], result["decode_errors"]
                assert not result["socket_errors"], result["socket_errors"]
                result["client_forwarded_at_response"] = result["client_forwarded"]
                assert result["client_forwarded"] == result["client_forwarded_at_gate"]
                result["response_body"] = body.decode()
            finally:
                result["client_forwarded_while_gated"] = (
                    result["client_forwarded"] - result["client_forwarded_at_gate"])
                proxy.release()
        async with connect("127.0.0.1", port, configuration=configuration(),
                           create_protocol=HttpProbe) as health:
            assert await health.get(port, b"/health") == b"ok\n"
        result["listener_healthy"] = True
    finally:
        proxy.close()


async def check_close(port, result, timeout):
    async with connect("127.0.0.1", port, configuration=configuration(),
                       create_protocol=HttpProbe) as first:
        assert await first.get(port, b"/health") == b"ok\n"
        # Keep the UDP transport alive while inspecting retirement: exiting
        # its context would wait for the client's close timer and mask revival.
        first._quic.close(error_code=0, reason_phrase="peer close regression")
        first.transmit()
        await asyncio.sleep(0.05)
        async with connect("127.0.0.1", port, configuration=configuration(),
                           create_protocol=HttpProbe) as second:
            body = await second.get(port, b"/active-connections", timeout)
            result["active_connections_after_peer_close"] = int(body)
            assert int(body) == 1, ("peer CLOSE left or revived a live connection", body)
            assert await second.get(port, b"/health") == b"ok\n"
            result["listener_healthy"] = True


async def check_draining(port, result, timeout):
    proxy = ResponseLossProxy(port, result)
    proxy_port = await proxy.open()
    try:
        async with connect("127.0.0.1", proxy_port, configuration=configuration(),
                           create_protocol=HttpProbe) as first:
            assert await first.get(port, b"/health") == b"ok\n"
            # Initial CIDs need not carry a reset token. Move to an advertised
            # NEW_CONNECTION_ID and verify traffic on that authenticated route.
            assert first._quic._peer_cid_available, "no token-bearing CID advertised"
            first._quic.change_connection_id()
            first.transmit()
            assert await first.get(port, b"/health") == b"ok\n"
            async with connect("127.0.0.1", port, configuration=configuration(),
                               create_protocol=HttpProbe) as health:
                assert await health.get(port, b"/active-connections") == b"2"
                await asyncio.sleep(0.05)
                packet = proxy.last_client_short
                token = first._quic._peer_cid.stateless_reset_token
                assert packet and len(packet) >= 21, "no valid old short-header packet"
                assert token and len(token) == 16, "server reset token unavailable"
                assert packet[1:1 + len(first._quic._peer_cid.cid)] == first._quic._peer_cid.cid
                proxy.reset_token = token
                closed_at = time.monotonic()
                first._quic.close(error_code=0, reason_phrase="draining silence regression")
                first.transmit()
                # Use an already-handshaken independent connection to confirm
                # server processing, without spending a fresh handshake in the
                # old connection's guaranteed minimum draining window.
                while await health.get(port, b"/active-connections") != b"1":
                    assert time.monotonic() - closed_at < 0.03, "CLOSE processing barrier exceeded budget"
                processed_at = time.monotonic()
                result["close_processed_at_ms"] = (processed_at - closed_at) * 1000
                proxy.gated = True
                proxy.probe_window_started = closed_at
                proxy.backend.sendto(packet)
                result["first_old_packet_at_ms"] = (time.monotonic() - closed_at) * 1000
                # pto_duration() includes 25ms max_ack_delay and >=1ms
                # granularity, so max(handshake, app) * 3 is at least 78ms.
                # This is a lower bound, never an assumed expiry timestamp.
                minimum_drain = 0.078
                await asyncio.sleep(max(0, closed_at + minimum_drain / 2 - time.monotonic()))
                observed_at = time.monotonic()
                result["minimum_drain_ms"] = minimum_drain * 1000
                result["silence_observed_until_ms"] = (observed_at - closed_at) * 1000
                result["probe_replies"] = proxy.probe_replies
                assert observed_at - closed_at < minimum_drain, "fixture scheduling exceeded guaranteed drain window"
                assert not proxy.probe_replies, "old CID emitted a packet during draining"
                deadline = closed_at + 5
                while proxy.reset_received_at is None and time.monotonic() < deadline:
                    proxy.backend.sendto(packet)
                    await asyncio.sleep(0.025)
                assert proxy.reset_received_at is not None, "no matching stateless reset after drain expiry"
                result["first_matching_reset_at_ms"] = (proxy.reset_received_at - closed_at) * 1000
                assert result["first_matching_reset_at_ms"] >= minimum_drain * 1000
                result["reset_token_matched"] = True
                proxy.release()
                assert await health.get(port, b"/health") == b"ok\n"
                result["listener_healthy"] = True
    finally:
        result["probe_replies"] = proxy.probe_replies
        proxy.close()
        if result.get("listener_healthy"):
            # The terminated client's UDP transport can close before a final
            # reset reaches it. Retain that expected post-reset ICMP evidence;
            # every backend or pre-reset socket error remains a failure.
            assert all(event["source"] == "front" and event["after_matching_reset"] and
                       event["error"].startswith(("ConnectionRefusedError", "ConnectionResetError"))
                       for event in result["socket_error_events"]), result


async def check_active_restart(port, result, timeout, report):
    async with connect("127.0.0.1", port, configuration=configuration(),
                       create_protocol=HttpProbe) as first:
        assert await first.get(port, b"/health") == b"ok\n"
        _, future = first.start_get(port, b"/restart-inflight")
        try:
            await asyncio.wait_for(asyncio.shield(future), 5)
        except AssertionError as error:
            result["old_request_terminated"] = True
            result["old_request_error"] = str(error)
        else:
            raise AssertionError("in-flight request survived listener shutdown")
        deadline = time.monotonic() + 5
        restarted = ""
        while time.monotonic() < deadline:
            restarted = report.read_text().strip() if report.exists() else ""
            if restarted:
                break
            await asyncio.sleep(0.01)
        assert restarted == "restarted", ("active listener restart did not finish", restarted)
        assert "invalid HTTP/3 GOAWAY stream ID" not in result["old_request_error"]
        if first.last_termination is not None:
            result["old_connection_close_code"] = first.last_termination.error_code
            assert first.last_termination.error_code == 0, repr(first.last_termination)
        result["active_restart_report"] = "restarted"
        result["active_restart_budget"] = list(map(int, Path(str(report) + ".stats").read_text().split()))
        assert result["active_restart_budget"] == [0, 0, 0], result["active_restart_budget"]
        async with connect("127.0.0.1", port, configuration=configuration(),
                           create_protocol=HttpProbe) as second:
            assert await second.get(port, b"/health", timeout) == b"ok\n"
            assert await second.get(port, b"/active-connections", timeout) == b"1"
        result["listener_healthy"] = True


def run_case(args, case, root):
    result = {"case": case, "workers": args.workers, "restart": args.restart}
    cert, key = make_certificate(root)
    port = reserve_port()
    command = [str(args.server.resolve()), "--port", str(port), "--cert", str(cert),
               "--key", str(key), "--workers", str(args.workers), "--outbound-pto-test"]
    restart_report = root / "restart.txt"
    active_restart_report = root / "active-restart.txt"
    if case == "active":
        command.extend(["--active-restart-report", str(active_restart_report)])
    if args.restart:
        command.extend(["--restart-report", str(restart_report)])
    server_log = root / "server.log"
    with server_log.open("w+") as log:
        server = subprocess.Popen(command, cwd=root, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 5
            while f"listener started on UDP {port} with" not in server_log.read_text():
                assert server.poll() is None, "listener exited before ready"
                assert time.monotonic() < deadline, "listener readiness log missing"
                time.sleep(0.01)
            if args.restart:
                deadline = time.monotonic() + 5
                report = ""
                while time.monotonic() < deadline:
                    assert server.poll() is None, "listener exited during restart"
                    report = restart_report.read_text().strip() if restart_report.exists() else ""
                    if report:
                        break
                    time.sleep(0.01)
                assert report, "listener restart report missing"
                assert report == "restarted", report
                result["restart_report"] = report
                result["restart_budget"] = list(map(int, Path(str(restart_report) + ".stats").read_text().split()))
                assert result["restart_budget"] == [0, 0, 0], result["restart_budget"]
            if case == "active":
                asyncio.run(check_active_restart(port, result, args.response_timeout, active_restart_report))
            else:
                if case in ("pto", "pto-full"):
                    asyncio.run(check_pto(port, result, args.response_timeout,
                                         full_frame=case == "pto-full"))
                elif case == "draining":
                    asyncio.run(check_draining(port, result, args.response_timeout))
                else:
                    asyncio.run(check_close(port, result, args.response_timeout))
            assert server.poll() is None, "listener exited"
            result["status"] = "passed"
        except Exception as error:
            result["status"] = "failed"
            result["error"] = f"{type(error).__name__}: {error}"
            log.seek(0)
            result["server_log"] = log.read()
        finally:
            stop_process(server)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server", type=Path)
    parser.add_argument("--workers", type=int, choices=(1, 2), default=1)
    parser.add_argument("--case", choices=("all", "pto", "pto-full", "close", "draining", "active"), default="all")
    parser.add_argument("--restart", action="store_true")
    parser.add_argument("--response-timeout", type=float, default=2.0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not args.server.is_file():
        parser.error("server binary is missing")
    if not 1.2 < args.response_timeout < 3.0:
        parser.error("response timeout must be after the 1200ms response and before the 3000ms idle deadline")
    cases = ("pto", "pto-full", "close", "draining", "active") if args.case == "all" else (args.case,)
    results = []
    for case in cases:
        with tempfile.TemporaryDirectory(prefix=f"cnetmod-outbound-{case}-") as directory:
            result = run_case(args, case, Path(directory))
            results.append(result)
            print(json.dumps(result, sort_keys=True), flush=True)
    if args.output:
        args.output.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return 0 if all(result["status"] == "passed" for result in results) else 1


if __name__ == "__main__":
    sys.exit(main())
