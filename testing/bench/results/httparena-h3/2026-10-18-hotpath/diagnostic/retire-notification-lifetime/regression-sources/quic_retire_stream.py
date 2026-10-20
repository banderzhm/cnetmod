"""Prove delayed stream retirement publishes MAX_STREAMS without peer ingress.

Run against quic_retire_stream_server. No HTTP handler timing is involved:
the fixture explicitly retires stream zero only after a file barrier, after
the request/response have completed and server recovery has become quiescent.
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
    from aioquic.asyncio import QuicConnectionProtocol, connect
    from aioquic.quic.configuration import QuicConfiguration
    from aioquic.quic.events import ConnectionTerminated, StreamDataReceived
except ImportError:
    raise SystemExit(77)

from http_client_e2e import make_certificate, reserve_port, stop_process
from http3_outbound_pto import ResponseLossProxy


class RawPeer(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.body = bytearray()
        self.response = asyncio.get_running_loop().create_future()
        self.outbound = None
        self.outbound_body = bytearray()

    def quic_event_received(self, event):
        if isinstance(event, StreamDataReceived):
            assert event.stream_id in (0, 1), event
            body = self.body if event.stream_id == 0 else self.outbound_body
            future = self.response if event.stream_id == 0 else self.outbound
            body.extend(event.data)
            assert future is not None, event
            if event.end_stream and not future.done():
                future.set_result(bytes(body))
        elif isinstance(event, ConnectionTerminated):
            for future in (self.response, self.outbound):
                if future is not None and not future.done():
                    future.set_exception(AssertionError(repr(event)))


class Backend(asyncio.DatagramProtocol):
    def __init__(self, proxy):
        self.proxy = proxy

    def datagram_received(self, data, address):
        self.proxy.server_datagram(data)

    def error_received(self, error):
        self.proxy.errors.append(repr(error))


class IngressGate(asyncio.DatagramProtocol):
    def __init__(self, port):
        self.port = port
        self.front = self.back = None
        self.client = None
        self.gated = False
        self.forwarded = 0
        self.blocked = 0
        self.server_after_gate = 0
        self.errors = []

    async def open(self):
        loop = asyncio.get_running_loop()
        self.back, _ = await loop.create_datagram_endpoint(
            lambda: Backend(self), remote_addr=("127.0.0.1", self.port))
        self.front, _ = await loop.create_datagram_endpoint(
            lambda: self, local_addr=("127.0.0.1", 0))
        return self.front.get_extra_info("sockname")[1]

    def datagram_received(self, data, address):
        if self.client is None:
            self.client = address
        assert self.client == address, "fixture supports exactly one client"
        if self.gated:
            self.blocked += 1
        else:
            self.forwarded += 1
            self.back.sendto(data)

    def server_datagram(self, data):
        if self.gated:
            self.server_after_gate += 1
        self.front.sendto(data, self.client)

    def error_received(self, error):
        self.errors.append(repr(error))

    def close(self):
        if self.front:
            self.front.close()
        if self.back:
            self.back.close()


async def wait_until(predicate, timeout, message):
    deadline = time.monotonic() + timeout
    while not predicate():
        if time.monotonic() >= deadline:
            raise AssertionError(message)
        await asyncio.sleep(0.005)


async def wait_file(path, process, report, timeout):
    def available():
        if process.poll() is not None:
            raise AssertionError(f"fixture exited: {process.returncode}")
        if report.exists():
            text = report.read_text(encoding="utf-8")
            if text.startswith("failed:"):
                raise AssertionError(text)
        return path.exists() and path.stat().st_size > 0
    await wait_until(available, timeout, f"fixture barrier missing: {path.name}")
    return path.read_text(encoding="utf-8")


async def exercise(process, port, directory, result):
    report = directory / "result.txt"
    ready = directory / "ready.txt"
    await wait_file(directory / "listening.txt", process, report, 3)
    proxy = IngressGate(port)
    proxy_port = await proxy.open()
    config = QuicConfiguration(is_client=True, alpn_protocols=["quic-retire-test"])
    config.verify_mode = ssl.CERT_NONE
    config.idle_timeout = 6.0
    try:
        async with connect("127.0.0.1", proxy_port, configuration=config,
                           create_protocol=RawPeer) as peer:
            initial = peer._quic._remote_max_streams_bidi
            assert initial == 1, initial
            stream = peer._quic.get_next_available_stream_id()
            assert stream == 0, stream
            peer._quic.send_stream_data(stream, b"retire request\n", end_stream=True)
            # Retain the sender object even if aioquic removes the fully closed
            # stream from its live map after both directions finish.
            sender = peer._quic._streams[stream].sender
            peer.transmit()
            assert await asyncio.wait_for(peer.response, 2) == b"retire response\n"
            await wait_until(lambda: sender.is_finished, 1,
                             "request data/FIN was not acknowledged")
            assert await wait_file(ready, process, report, 2) == "quiescent\n"
            result["request_fin_acked"] = sender.is_finished
            result["initial_max_streams"] = initial
            assert peer._quic._remote_max_streams_bidi == initial
            proxy.gated = True
            forwarded_at_gate = proxy.forwarded
            # Drain already-forwarded ingress/egress before releasing the
            # explicit application barrier. This delay never triggers retire.
            await asyncio.sleep(0.05)
            assert proxy.server_after_gate == 0, "server was not quiescent at gate"
            assert peer._quic._remote_max_streams_bidi == initial
            started = time.monotonic()
            (directory / "trigger.txt").write_text("retire\n", encoding="utf-8")
            await wait_until(lambda: peer._quic._remote_max_streams_bidi > initial,
                             0.75, "MAX_STREAMS stayed at one after explicit retire without ingress")
            result["credit_at_ms"] = (time.monotonic() - started) * 1000
            result["final_max_streams"] = peer._quic._remote_max_streams_bidi
            assert result["final_max_streams"] == 2
            # Its normal MAX_STREAMS ACK is also blocked. Do not inject probes
            # or call transmit to make the server's writer run.
            await asyncio.sleep(0.05)
            line = await wait_file(report, process, report, 1)
            fields = line.split()
            assert len(fields) == 4 and fields[0] == "retired", line
            result["pre_retire_deadline_ms"] = int(fields[1])
            result["observer_notifications_after_retire"] = int(fields[2])
            result["fixture_ingress_count"] = int(fields[3])
            assert result["pre_retire_deadline_ms"] > 3000
            if result["mode"] == "observer":
                assert result["observer_notifications_after_retire"] > 0
            assert proxy.forwarded == forwarded_at_gate, "ingress escaped the gate"
            assert proxy.blocked > 0, "normal client ACK did not exercise the gate"
            assert proxy.server_after_gate > 0, "no server datagram delivered the credit"
            assert not proxy.errors, proxy.errors
            if result["case"] == "notification-lifetime":
                lifetime = await wait_file(directory / "notification.txt", process, report, 1)
                baseline, after_dispatch, expired = lifetime.split()
                result["notification_capture_owners_before"] = int(baseline)
                result["notification_capture_owners_after_dispatch"] = int(after_dispatch)
                result["notification_capture_expired_after_unobserve"] = expired == "true"
                assert int(baseline) == 1, lifetime
                assert int(after_dispatch) == int(baseline), lifetime
                assert expired == "true", lifetime
            result["client_forwarded_after_gate"] = proxy.forwarded - forwarded_at_gate
            result["client_blocked"] = proxy.blocked
            result["server_datagrams_after_gate"] = proxy.server_after_gate
            proxy.gated = False
    finally:
        result["proxy_errors"] = proxy.errors
        proxy.close()


async def exercise_private_pto(process, port, directory, result):
    report = directory / "result.txt"
    await wait_file(directory / "listening.txt", process, report, 3)
    proxy = ResponseLossProxy(port, result)
    proxy_port = await proxy.open()
    config = QuicConfiguration(is_client=True, alpn_protocols=["quic-retire-test"])
    config.verify_mode = ssl.CERT_NONE
    config.idle_timeout = 6.0
    try:
        async with connect("127.0.0.1", proxy_port, configuration=config,
                           create_protocol=RawPeer) as peer:
            peer._quic.send_stream_data(0, b"retire request\n", end_stream=True)
            sender = peer._quic._streams[0].sender
            peer.transmit()
            assert await asyncio.wait_for(peer.response, 2) == b"retire response\n"
            await wait_until(lambda: sender.is_finished, 1, "request FIN not ACKed")
            assert await wait_file(directory / "ready.txt", process, report, 2) == "quiescent\n"
            peer.outbound = asyncio.get_running_loop().create_future()
            started = time.monotonic()
            proxy.arm(peer, 1, started)
            await asyncio.sleep(0.05)
            assert result["server_forwarded_while_gated"] == 0, "server was not quiescent"
            (directory / "trigger.txt").write_text("send\n", encoding="utf-8")
            payload = await asyncio.wait_for(peer.outbound, 0.75)
            assert payload == b"private delayed response\n", payload
            result["response_at_ms"] = (time.monotonic() - started) * 1000
            assert result["response_datagrams_dropped"] == 1, result
            assert result["dropped_stream_frames"] == [
                {"offset": 0, "length": len(payload), "fin": True}], result
            assert result["retransmitted_stream_frames"], result
            line = await wait_file(report, process, report, 1)
            fields = line.split()
            assert len(fields) == 4 and fields[0] == "sent", line
            result["pre_send_deadline_ms"] = int(fields[1])
            result["observer_notifications"] = int(fields[2])
            assert result["pre_send_deadline_ms"] > 3000
            assert result["observer_notifications"] == 0
            assert result["client_forwarded"] == result["client_forwarded_at_gate"]
            assert not result["socket_errors"] and not result["decode_errors"], result
            proxy.release()
    finally:
        proxy.close()


def run_case(args, mode, directory):
    result = {"case": args.scenario, "mode": mode, "status": "failed"}
    process = None
    try:
        cert, key = make_certificate(directory)
        port = reserve_port()
        command = [str(args.server), "--port", str(port), "--mode", mode,
                   "--scenario", args.scenario,
                   "--cert", str(cert), "--key", str(key),
                   "--listening-report", str(directory / "listening.txt"),
                   "--ready-report", str(directory / "ready.txt"),
                   "--retire-trigger", str(directory / "trigger.txt"),
                   "--result-report", str(directory / "result.txt")]
        if args.scenario == "notification-lifetime":
            command += ["--notification-report", str(directory / "notification.txt")]
        process = subprocess.Popen(command, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        exercise_case = exercise_private_pto if args.scenario == "private-pto" else exercise
        asyncio.run(exercise_case(process, port, directory, result))
        result["status"] = "passed"
    except Exception as error:
        result["error"] = repr(error)
    finally:
        if process:
            stdout, stderr = stop_process(process)
            result["server_stdout"] = stdout
            result["server_stderr"] = stderr
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server", type=Path)
    parser.add_argument("--mode", choices=("all", "observer", "private"), default="all")
    parser.add_argument("--scenario", choices=("credit", "private-pto", "notification-lifetime"), default="credit")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not args.server.is_file():
        parser.error("server binary is missing")
    results = []
    modes = ("observer",) if args.scenario == "notification-lifetime" else ("private",) if args.scenario == "private-pto" else (
        ("observer", "private") if args.mode == "all" else (args.mode,))
    for mode in modes:
        with tempfile.TemporaryDirectory(prefix=f"cnetmod-retire-{mode}-") as directory:
            result = run_case(args, mode, Path(directory))
            results.append(result)
            print(json.dumps(result, sort_keys=True), flush=True)
    if args.output:
        args.output.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return 0 if all(result["status"] == "passed" for result in results) else 1


if __name__ == "__main__":
    sys.exit(main())
