"""Real single-path PMTU ACK / ordinary-loss / large pending-frame regression.

No CC setters or fake ACK callbacks are used. --require-diagnostics consumes
only a separately instrumented validation binary; normal positive regression
runs remain usable after temporary production logging is removed.
"""
from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import ssl
import subprocess
import sys
import tempfile
import time


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server", type=Path)
    parser.add_argument("--helper-dir", type=Path, default=Path(__file__).parent)
    parser.add_argument("--records", type=Path)
    parser.add_argument("--expect", choices=("pass", "stall"), default="pass")
    parser.add_argument("--require-diagnostics", action="store_true")
    parser.add_argument("--large-aead-peer", action="store_true",
                        help="Explicit cryptography AEAD test-peer adapter; not stock aioquic interop")
    parser.add_argument("--trace-server", action="store_true",
                        help="Linux validation only: strace real UDP socket options/submissions")
    parser.add_argument("--epochs", type=int, default=16)
    parser.add_argument("--final-timeout", type=float, default=2.0)
    args = parser.parse_args()
    if args.epochs < 12 or args.epochs > 24:
        parser.error("epochs must be in [12,24] to exercise the reduction precondition")
    if args.expect == "stall" and not args.require_diagnostics:
        parser.error("an expected stall requires actual cwnd/flight/pending-frame diagnostics")
    if not args.server.is_file():
        parser.error("server binary is missing")
    if args.records is None:
        args.records = Path(tempfile.mkdtemp(prefix="quic-pmtu-cc-"))
    if args.records.exists() and any(args.records.iterdir()):
        parser.error("records directory must be empty; never overwrite earlier evidence")
    return args


class Trace:
    def __init__(self, path):
        self.path = path

    def read(self):
        try:
            lines = self.path.read_text(encoding="utf-8", errors="replace").splitlines()
        except FileNotFoundError:
            return []
        result = []
        for index, line in enumerate(lines):
            marker = re.search(r"PMTU_CC_(ACK|VALIDATE|BLOCK|SENT)\s+(.*)", line)
            if marker:
                fields = {key: (True if value == "true" else False if value == "false"
                                else int(value) if value.isdigit() else value)
                          for key, value in re.findall(r"(\w+)=(\w+)", marker[2])}
                fields.update(kind=marker[1], line=index)
                result.append(fields)
        return result

    def ack(self):
        return next((row for row in reversed(self.read()) if row["kind"] == "ACK" and row.get("space") == 2), None)

    def quiet(self):
        rows = self.read()
        ack = next((row for row in reversed(rows) if row["kind"] == "ACK" and row.get("space") == 2), None)
        if ack is None or ack.get("flight") != 0 or ack.get("pto"):
            return None
        # Do not accept a stale empty-flight snapshot if a later packet was
        # recovery tracked. Pure ACK-only SENT records do not create flight.
        if any(row["kind"] == "SENT" and row["line"] > ack["line"] and
               row.get("tracked", True) for row in rows):
            return None
        return ack


async def wait_until(predicate, timeout, reason):
    limit = time.monotonic() + timeout
    while True:
        value = predicate()
        if value:
            return value
        if time.monotonic() >= limit:
            raise AssertionError(reason)
        await asyncio.sleep(0.005)


async def exercise(args, process, port, trace, result, imports):
    QuicConnectionProtocol, connect, QuicConfiguration, StreamDataReceived, \
        ConnectionTerminated, StreamReset, CryptoContext, Epoch, Buffer, \
        pull_quic_header, response_stream_frames = imports

    class RawPeer(QuicConnectionProtocol):
        def __init__(self, *values, **kwargs):
            super().__init__(*values, **kwargs)
            self.responses = {}

        def quic_event_received(self, event):
            if isinstance(event, StreamDataReceived):
                response = self.responses.get(event.stream_id)
                assert response is not None, repr(event)
                response["body"].extend(event.data)
                if event.end_stream and not response["done"].done():
                    response["fin"] = True
                    response["done"].set_result(bytes(response["body"]))
            elif isinstance(event, (ConnectionTerminated, StreamReset)):
                for stream, response in self.responses.items():
                    if (isinstance(event, ConnectionTerminated) or stream == event.stream_id) \
                            and not response["done"].done():
                        response["done"].set_exception(AssertionError(repr(event)))

        def request(self, command, arm=None):
            stream = self._quic.get_next_available_stream_id()
            response = {"body": bytearray(), "fin": False,
                        "done": asyncio.get_running_loop().create_future()}
            self.responses[stream] = response
            if arm:
                arm(stream)
            self._quic.send_stream_data(stream, command.encode() + b"\n", end_stream=True)
            response["sender"] = self._quic._streams[stream].sender
            self.transmit()
            return stream, response

    class Backend(asyncio.DatagramProtocol):
        def __init__(self, owner):
            self.owner = owner

        def datagram_received(self, data, address):
            self.owner.from_server(data)

        def error_received(self, error):
            self.owner.errors.append(repr(error))

    class LossProxy(asyncio.DatagramProtocol):
        def __init__(self):
            self.front = self.back = None
            self.client = None
            self.crypto = None
            self.target = None
            self.epoch = None
            self.dropped = False
            self.highest = 0
            self.events = []
            self.errors = []

        async def open(self):
            loop = asyncio.get_running_loop()
            self.back, _ = await loop.create_datagram_endpoint(
                lambda: Backend(self), remote_addr=("127.0.0.1", port))
            self.front, _ = await loop.create_datagram_endpoint(
                lambda: self, local_addr=("127.0.0.1", 0))
            return self.front.get_extra_info("sockname")[1]

        def datagram_received(self, data, address):
            if self.client is None:
                self.client = address
            assert self.client == address, "fixture allows one stable tuple"
            self.events.append({"at": time.monotonic(), "direction": "client",
                                "bytes": len(data), "action": "forward"})
            self.back.sendto(data)

        def error_received(self, error):
            self.errors.append(repr(error))

        def attach(self, peer):
            source = peer._quic._cryptos[Epoch.ONE_RTT].recv
            assert source.is_valid()
            self.crypto = CryptoContext(key_phase=source.key_phase)
            self.crypto.setup(cipher_suite=source.cipher_suite,
                              secret=source.secret, version=source.version)
            self.cid_length = peer._quic.configuration.connection_id_length

        def arm(self, epoch, stream):
            assert not self.target or self.dropped, "earlier armed drop never happened"
            self.target = stream
            self.epoch = epoch
            self.dropped = False
            self.events.append({"at": time.monotonic(), "action": "arm",
                                "epoch": epoch, "stream": stream})

        def disarm(self):
            self.events.append({"at": time.monotonic(), "action": "disarm"})
            self.target = None
            self.epoch = None

        def from_server(self, data):
            event = {"at": time.monotonic(), "direction": "server",
                     "bytes": len(data), "action": "forward", "epoch": self.epoch}
            if self.crypto is not None and data and not data[0] & 0x80:
                try:
                    header = Buffer(data=data)
                    pull_quic_header(header, host_cid_length=self.cid_length)
                    _, payload, number, _ = self.crypto.decrypt_packet(
                        data, header.tell(), self.highest)
                    self.highest = max(self.highest, number + 1)
                    frames = response_stream_frames(payload, self.target
                                                    if self.target is not None else -1)
                    event.update(packet_number=number, target_stream=self.target, frames=frames)
                    # Drop actual reliable bytes for the named response. ACK,
                    # PING, PMTU padding and unrelated control traffic pass.
                    if self.target is not None and not self.dropped and \
                            any(frame["length"] for frame in frames):
                        self.dropped = True
                        event["action"] = "drop"
                        self.events.append(event)
                        return
                except Exception as error:
                    self.errors.append("decode: " + repr(error))
                    event["decode_error"] = repr(error)
            self.events.append(event)
            self.front.sendto(data, self.client)

        def close(self):
            if self.front:
                self.front.close()
            if self.back:
                self.back.close()

    def check_process():
        if process.poll() is not None:
            raise AssertionError(f"server exited early: {process.returncode}")
        failure = args.records / "failure.txt"
        if failure.exists():
            raise AssertionError(failure.read_text(encoding="utf-8", errors="replace"))
        return True

    def discovery_ready():
        check_process()
        ready = args.records / "ready.txt"
        return ready.exists() and ready.read_text(encoding="utf-8").strip() == "mds 4096"

    async def quiescent():
        # Identical stimulus/timing with or without diagnostics. Diagnostics
        # observe this fixed quiet point; they never trigger extra requests.
        await asyncio.sleep(0.075)
        check_process()
        if args.require_diagnostics:
            quiet = trace.quiet()
            assert quiet is not None, "actual flight/PTO did not become quiescent"
            return quiet
        return None

    proxy = LossProxy()
    proxy_port = await proxy.open()
    config = QuicConfiguration(is_client=True, alpn_protocols=["quic-pmtu-cc-test"])
    config.verify_mode = ssl.CERT_NONE
    config.idle_timeout = 30.0
    config.max_data = 1048576
    config.max_stream_data = 1048576
    config.max_datagram_size = 1200  # Outgoing target; not an advertised receive cap.
    result["peer_receive_limit"] = "omitted transport parameter; RFC default65527"
    result["peer_outgoing_target"] = 1200
    result["server_ceiling"] = 4096
    try:
        async with connect("127.0.0.1", proxy_port, configuration=config,
                           create_protocol=RawPeer) as peer:
            proxy.attach(peer)
            await wait_until(discovery_ready, 8, "no actual probe-ACK discovery to4096")
            if args.require_diagnostics:
                validates = [row for row in trace.read() if row["kind"] == "VALIDATE"]
                assert validates and validates[-1].get("mds") == 4096, validates
                result["discovery_trace"] = validates
            await quiescent()
            epochs = []
            for epoch in range(args.epochs):
                check_process()
                line_before = len(trace.read())
                stream, response = peer.request(f"epoch/{epoch}",
                    lambda sid: proxy.arm(epoch, sid))
                body = await asyncio.wait_for(asyncio.shield(response["done"]), 3)
                assert body == b"e" * 256 and response["fin"], (epoch, len(body))
                assert proxy.dropped, f"epoch{epoch} had no actual STREAM drop"
                await wait_until(lambda: response["sender"].is_finished, 1,
                                 "request FIN not acknowledged")
                losses = []
                for confirmation in range(4):
                    # A fresh ordinary higher-PN response and its real ACK
                    # force the detector to revisit the earlier dropped PN.
                    _, confirm = peer.request(f"confirm/{epoch}/{confirmation}")
                    data = await asyncio.wait_for(asyncio.shield(confirm["done"]), 1)
                    assert data == b"c" and confirm["fin"]
                    await asyncio.sleep(0.025)
                quiet = await quiescent()
                if args.require_diagnostics:
                    losses = [
                        row for row in trace.read()[line_before:]
                        if row["kind"] == "ACK" and row.get("ordinary_lost", 0) > 0]
                    assert losses, f"epoch{epoch}: PTO copy happened but no actual ordinary loss"
                    if args.expect == "pass":
                        assert all(row["cwnd"] >= 8192 for row in losses), losses
                dropped = [event for event in proxy.events if event.get("action") == "drop"
                           and event.get("epoch") == epoch]
                assert len(dropped) == 1, dropped
                later = [event for event in proxy.events if event.get("direction") == "server"
                         and event.get("action") == "forward" and
                         event.get("packet_number", -1) > dropped[0]["packet_number"] and
                         event.get("epoch") == epoch]
                assert later, "no actual higher-PN server packet was delivered"
                epochs.append({"epoch": epoch, "stream": stream,
                               "drop": dropped[0], "losses": losses, "quiet": quiet})
            result["epochs"] = epochs
            proxy.disarm()
            snapshot = await quiescent()
            if args.require_diagnostics:
                assert snapshot["flight"] == 0 and not snapshot["pto"], snapshot
                result["before_final"] = snapshot
                if args.expect == "stall":
                    assert snapshot["cwnd"] < 4096, snapshot
                    assert any(loss["cwnd"] == 2944 for item in epochs
                               for loss in item["losses"]), "old floor was never reached"
            final_line = len(trace.read())
            _, response = peer.request("final")
            started = time.monotonic()
            timed_out = False
            try:
                body = await asyncio.wait_for(asyncio.shield(response["done"]),
                                              args.final_timeout)
                result["final_body_bytes"] = len(body)
                result["final_body_sha256"] = hashlib.sha256(body).hexdigest()
                result["final_fin"] = response["fin"]
                assert body == b"F" * 16384 and response["fin"]
            except asyncio.TimeoutError:
                timed_out = True
            result["final_elapsed_ms"] = (time.monotonic() - started) * 1000
            result["final_timed_out"] = timed_out
            if args.expect == "stall":
                assert timed_out, "old comparison unexpectedly completed; no negative proof"
                blocks = [row for row in trace.read()[final_line:]
                          if row["kind"] == "BLOCK" and row.get("mds") == 4096
                          and row.get("noflight") and not row.get("pto")
                          and row.get("first_frame", 0) > row.get("cwnd", 0)]
                assert blocks, "timeout lacks actual empty-flight/noPTO/large-frame cwnd refusal"
                result["blocked_pending_frame"] = blocks[-1]
                response["done"].cancel()
                result["status"] = "expected_stall_proven"
            else:
                assert not timed_out, "16KiB response stalled after real loss epochs"
                await quiescent()
                result["status"] = "passed"
            if args.require_diagnostics:
                sends = [row for row in trace.read() if row["kind"] == "SENT"]
                assert all(row["bytes"] <= row["mds"] for row in sends
                           if not row.get("probe")), "ordinary packet exceeded discoveredMDS"
                assert any(row.get("probe") for row in sends), "no real probe transmission"
                result["packet_diagnostic_count"] = len(sends)
            assert not proxy.errors, proxy.errors
    finally:
        result["proxy_errors"] = proxy.errors
        (args.records / "proxy-ledger.json").write_text(
            json.dumps(proxy.events, indent=2), encoding="utf-8")
        proxy.close()


def main():
    args = arguments()
    args.records.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(args.helper_dir))
    try:
        from aioquic.asyncio import QuicConnectionProtocol, connect
        from aioquic.buffer import Buffer
        from aioquic.quic.configuration import QuicConfiguration
        from aioquic.quic.crypto import CryptoContext
        from aioquic.quic.events import StreamDataReceived, ConnectionTerminated, StreamReset
        from aioquic.quic.packet import pull_quic_header
        from aioquic.tls import Epoch
        from http_client_e2e import make_certificate, reserve_port
        from http3_outbound_pto import response_stream_frames
    except ImportError as error:
        print(f"fixture dependency unavailable: {error}", file=sys.stderr)
        return 77
    imports = (QuicConnectionProtocol, connect, QuicConfiguration, StreamDataReceived,
               ConnectionTerminated, StreamReset, CryptoContext, Epoch, Buffer,
               pull_quic_header, response_stream_frames)
    result = {"status": "failed", "expect": args.expect, "records": str(args.records.resolve()),
              "diagnostics_required": args.require_diagnostics,
              "confirmation_requests_per_epoch": 4,
              "confirmation_interval_seconds": 0.025,
              "quiet_interval_seconds": 0.075,
              "server_sha256": hashlib.sha256(args.server.read_bytes()).hexdigest()}
    process = None
    try:
        if args.large_aead_peer:
            from large_aead_peer import install
            result["peer_aead"] = install()
        else:
            result["peer_aead"] = {"kind": "unmodified-installed-aioquic",
                                    "stock_aioquic_interoperability": None}
        cert, key = make_certificate(args.records)
        port = reserve_port()
        command = [str(args.server.resolve()), "--port", str(port),
                   "--cert", str(cert.resolve()), "--key", str(key.resolve()),
                   "--listening-report", str((args.records / "listening.txt").resolve()),
                   "--ready-report", str((args.records / "ready.txt").resolve()),
                   "--result-report", str((args.records / "failure.txt").resolve())]
        result["command"] = command
        if args.trace_server:
            command = ["/usr/sbin/strace", "-ff", "-o", str(args.records / "udp-syscalls"),
                       "-e", "trace=sendmmsg,sendmsg,setsockopt,getsockopt", *command]
            result["trace_command"] = command
        log = args.records / "server.log"
        with log.open("w", encoding="utf-8") as output:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                       start_new_session=args.trace_server)
            async def run():
                await wait_until(lambda: (args.records / "listening.txt").exists(), 3,
                                 "fixture did not start listening")
                await exercise(args, process, port, Trace(log), result, imports)
            asyncio.run(run())
    except Exception as error:
        result["status"] = "failed"
        result["error"] = repr(error)
    finally:
        if process:
            if process.poll() is None:
                if args.trace_server:
                    os.killpg(process.pid, signal.SIGTERM)
                else:
                    process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                if args.trace_server:
                    os.killpg(process.pid, signal.SIGKILL)
                else:
                    process.kill()
                process.wait(timeout=5)
            result["server_exit_code"] = process.returncode
        (args.records / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result, sort_keys=True), flush=True)
    accepted = "expected_stall_proven" if args.expect == "stall" else "passed"
    return 0 if result["status"] == accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
