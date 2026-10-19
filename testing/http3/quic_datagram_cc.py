"""Real DATAGRAM admission / ACK-credit recovery / reliable STREAM PTO test.

Stock aioquic, packet sizes<=1200. No CC setter, fabricated ACK or timer pause.
The proxy independently authenticates both directions and gates only after
forwarding the actual command STREAM. --expect overflow is an old-code control.
"""
from __future__ import annotations

import argparse
import asyncio
import hashlib
import importlib.metadata
import json
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import time

try:
    from aioquic.asyncio import QuicConnectionProtocol, connect
    from aioquic.buffer import Buffer
    from aioquic.quic.configuration import QuicConfiguration
    from aioquic.quic.crypto import CryptoContext
    from aioquic.quic.events import StreamDataReceived, ConnectionTerminated, StreamReset
    from aioquic.quic.packet import pull_quic_header
    from aioquic.tls import Epoch
except ImportError:
    raise SystemExit(77)


def frames(payload):
    """Authenticate first, then parse actual frame identity; no size guesses."""
    data = Buffer(data=payload)
    result = {"streams": [], "datagrams": [], "kinds": []}
    while data.tell() < len(payload):
        kind = data.pull_uint_var()
        result["kinds"].append(kind)
        if kind in (0, 1, 0x1E):
            continue
        if kind in (2, 3):
            data.pull_uint_var()
            data.pull_uint_var()
            ranges = data.pull_uint_var()
            data.pull_uint_var()
            for _ in range(ranges):
                data.pull_uint_var()
                data.pull_uint_var()
            if kind == 3:
                for _ in range(3):
                    data.pull_uint_var()
        elif 8 <= kind <= 15:
            stream = data.pull_uint_var()
            offset = data.pull_uint_var() if kind & 4 else 0
            length = data.pull_uint_var() if kind & 2 else len(payload) - data.tell()
            body = data.pull_bytes(length)
            result["streams"].append({"stream": stream, "offset": offset,
                "length": length, "fin": bool(kind & 1),
                "sha256": hashlib.sha256(body).hexdigest()})
        elif kind in (0x30, 0x31):
            length = data.pull_uint_var() if kind & 1 else len(payload) - data.tell()
            body = data.pull_bytes(length)
            if body.startswith(b"DGCC"):
                sequence = int.from_bytes(body[4:8], "big")
                assert 0 <= sequence < 64 and body == b"DGCC" + sequence.to_bytes(4, "big") + b"x" * 1092
                result["datagrams"].append({"kind": "burst", "id": sequence, "length": length})
            else:
                assert body == b"DGCMfresh", repr(body)
                result["datagrams"].append({"kind": "marker", "length": length})
        elif kind == 4:
            for _ in range(3):
                data.pull_uint_var()
        elif kind in (5, 0x11, 0x15):
            data.pull_uint_var()
            data.pull_uint_var()
        elif kind == 6:
            data.pull_uint_var()
            data.pull_bytes(data.pull_uint_var())
        elif kind == 7:
            data.pull_bytes(data.pull_uint_var())
        elif kind in (0x10, 0x12, 0x13, 0x14, 0x16, 0x17, 0x19):
            data.pull_uint_var()
        elif kind == 0x18:
            data.pull_uint_var()
            data.pull_uint_var()
            data.pull_bytes(data.pull_uint8())
            data.pull_bytes(16)
        elif kind in (0x1A, 0x1B):
            data.pull_bytes(8)
        elif kind in (0x1C, 0x1D):
            data.pull_uint_var()
            if kind == 0x1C:
                data.pull_uint_var()
            data.pull_bytes(data.pull_uint_var())
        else:
            raise AssertionError(f"unhandled frame {kind:#x}")
    return result


class RawPeer(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.responses = {}

    def quic_event_received(self, event):
        if isinstance(event, StreamDataReceived):
            item = self.responses.get(event.stream_id)
            if item is not None:
                item["body"].extend(event.data)
                if event.end_stream and not item["done"].done():
                    item["fin"] = True
                    item["done"].set_result(bytes(item["body"]))
        elif isinstance(event, (ConnectionTerminated, StreamReset)):
            for stream, item in self.responses.items():
                if (isinstance(event, ConnectionTerminated) or stream == event.stream_id) and not item["done"].done():
                    item["done"].set_exception(AssertionError(repr(event)))

    def request(self, command, arm=None):
        stream = self._quic.get_next_available_stream_id()
        item = {"body": bytearray(), "fin": False,
                "done": asyncio.get_running_loop().create_future()}
        self.responses[stream] = item
        if arm:
            arm(stream)
        self._quic.send_stream_data(stream, command.encode() + b"\n", end_stream=True)
        self.transmit()
        return stream, item


class Backend(asyncio.DatagramProtocol):
    def __init__(self, proxy):
        self.proxy = proxy

    def datagram_received(self, data, address):
        self.proxy.from_server(data)

    def error_received(self, error):
        self.proxy.errors.append(repr(error))


class GateProxy(asyncio.DatagramProtocol):
    def __init__(self):
        self.front = self.back = self.client = None
        self.contexts = {}
        self.highest = {"client": 0, "server": 0}
        self.events = []
        self.errors = []
        self.mode = "open"
        self.phase = "warmup"
        self.target = None
        self.held = []
        self.stream_packets = {}
        self.repeats = []

    async def open(self, port):
        loop = asyncio.get_running_loop()
        self.back, _ = await loop.create_datagram_endpoint(lambda: Backend(self), remote_addr=("127.0.0.1", port))
        self.front, _ = await loop.create_datagram_endpoint(lambda: self, local_addr=("127.0.0.1", 0))
        return self.front.get_extra_info("sockname")[1]

    def error_received(self, error):
        self.errors.append(repr(error))

    def attach(self, peer):
        pair = peer._quic._cryptos[Epoch.ONE_RTT]
        for direction, source in (("client", pair.send), ("server", pair.recv)):
            assert source.is_valid()
            context = CryptoContext(key_phase=source.key_phase)
            context.setup(cipher_suite=source.cipher_suite, secret=source.secret, version=source.version)
            self.contexts[direction] = context
        self.cid_length = peer._quic.configuration.connection_id_length

    def record(self, direction, data):
        item = {"at": time.monotonic(), "direction": direction, "bytes": len(data),
                "action": "forward", "phase": self.phase}
        if direction in self.contexts and data and not data[0] & 0x80:
            try:
                header = Buffer(data=data)
                pull_quic_header(header, host_cid_length=self.cid_length)
                _, plaintext, number, _ = self.contexts[direction].decrypt_packet(data, header.tell(), self.highest[direction])
                self.highest[direction] = max(self.highest[direction], number + 1)
                item.update(pn=number, frames=frames(plaintext))
            except Exception as error:
                self.errors.append(repr(error))
                item["decode_error"] = repr(error)
        return item

    def datagram_received(self, data, address):
        if self.client is None:
            self.client = address
        if self.client != address:
            self.errors.append("unexpected second client tuple")
            return
        item = self.record("client", data)
        if self.mode == "hold":
            self.held.append(data)
            item["action"] = "hold"
        else:
            self.back.sendto(data)
            if self.mode == "arming" and any(frame["stream"] == self.target and frame["length"]
                    for frame in item.get("frames", {}).get("streams", [])):
                self.mode = "hold"
                item["action"] = "forward_command_then_hold"
        self.events.append(item)

    def from_server(self, data):
        item = self.record("server", data)
        if self.phase == "pto" and self.mode == "hold":
            for frame in item.get("frames", {}).get("streams", []):
                if frame["stream"] != self.target or not frame["length"]:
                    continue
                key = (frame["offset"], frame["length"], frame["fin"], frame["sha256"])
                original = self.stream_packets.get(key)
                if original is not None:
                    self.repeats.append({"original": original, "repeat": item.copy(), "frame": frame})
                else:
                    self.stream_packets[key] = item.copy()
        self.events.append(item)
        self.front.sendto(data, self.client)

    def arm(self, phase, stream):
        assert self.mode == "open" and not self.held
        self.phase, self.target, self.mode = phase, stream, "arming"

    def release(self):
        assert self.mode == "hold", self.mode
        count = len(self.held)
        self.mode = "open"
        for data in self.held:
            self.back.sendto(data)
        self.held.clear()
        self.events.append({"action": "release", "phase": self.phase, "packets": count, "at": time.monotonic()})
        return count

    def burst_packets(self):
        return [item for item in self.events if item.get("direction") == "server" and
                any(frame["kind"] == "burst" for frame in item.get("frames", {}).get("datagrams", []))]

    def close(self):
        if self.front:
            self.front.close()
        if self.back:
            self.back.close()


async def wait_until(predicate, timeout, reason):
    end = time.monotonic() + timeout
    while True:
        value = predicate()
        if value:
            return value
        if time.monotonic() >= end:
            raise AssertionError(reason)
        await asyncio.sleep(0.005)


async def exercise(args, process, port, result):
    proxy = GateProxy()
    proxy_port = await proxy.open(port)

    def healthy():
        assert process.poll() is None, "server exited early"
        failure = args.records / "failure.txt"
        assert not failure.exists(), failure.read_text(encoding="utf-8") if failure.exists() else ""
        assert not proxy.errors, proxy.errors
        return True

    async def body(item, expected):
        value = await asyncio.wait_for(asyncio.shield(item["done"]), 5)
        assert value == expected and item["fin"], (len(value), len(expected), item["fin"])

    configuration = QuicConfiguration(is_client=True, alpn_protocols=["quic-datagram-cc-test"])
    configuration.verify_mode = ssl.CERT_NONE
    configuration.max_data = 1048576
    configuration.max_stream_data = 1048576
    configuration.max_datagram_size = 1200
    configuration.max_datagram_frame_size = 1200
    peer = None
    try:
        async with connect("127.0.0.1", proxy_port, configuration=configuration, create_protocol=RawPeer) as peer:
            proxy.attach(peer)

            def clear_ready_report():
                healthy()
                try:
                    (args.records / "ready.txt").unlink(missing_ok=True)
                except PermissionError as error:
                    if getattr(error, "winerror", None) in (32, 33):
                        return False
                    raise
                return True

            async def warmup():
                await wait_until(clear_ready_report, 4, "previous warmup ready report is still open")
                _, item = peer.request("warmup")
                await body(item, b"c")
                await wait_until(lambda: healthy() and (args.records / "ready.txt").exists(), 4, "real recovery did not drain")

            await warmup()
            if args.scenario in ("all", "admission"):
                # Conservative bound: Reno starts14720 and can grow by no more
                # than total ACKed server bytes. Include ALL prior UDP bytes,
                # even long-header/untracked packets, so this overestimates.
                prior_bytes = sum(row["bytes"] for row in proxy.events if row.get("direction") == "server")
                bound = 14720 + prior_bytes
                _, burst_response = peer.request("burst", lambda stream: proxy.arm("burst", stream))
                await wait_until(lambda: healthy() and (args.records / "burst.json").exists(), 4, "burst acceptance report missing")
                report = json.loads((args.records / "burst.json").read_text())
                assert report["accepted"] == 64 and report["elapsed_us"] < report["first_recovery_deadline_us"], report
                assert 64 * 1100 > 2 * bound, (bound, report)
                await asyncio.sleep(0.1)
                healthy()
                assert proxy.mode == "hold" and proxy.held, "actual client ACKs were not gated"
                packets = proxy.burst_packets()
                assert packets and all(set(item["frames"]["kinds"]) <= {0, 2, 3, 0x30, 0x31} for item in packets), "burst was not pure DATAGRAM/ACK"
                identifiers = [frame["id"] for item in packets for frame in item["frames"]["datagrams"] if frame["kind"] == "burst"]
                assert len(identifiers) == len(set(identifiers)), "unreliable payload retransmitted"
                sent_bytes = sum(item["bytes"] for item in packets)
                overflow = sent_bytes > bound
                result["admission"] = {"prior_server_udp_bytes": prior_bytes, "cwnd_conservative_upper_bound": bound,
                    "burst_report": report, "wire_datagram_packets": len(packets), "wire_udp_bytes": sent_bytes,
                    "wire_ids": identifiers, "overflow": overflow, "held_packets": len(proxy.held)}
                if args.expect == "overflow":
                    assert overflow, "old negative control did not exceed conservative cwnd bound"
                else:
                    assert not overflow, "pure DATAGRAM packets exceeded conservative cwnd bound before ACK credit"
                    assert len(identifiers) < 64, "congestion/drop path was not exercised"
                proxy.release()
                await body(burst_response, b"c")
                await warmup()
                _, marker_response = peer.request("marker")
                await body(marker_response, b"c")
                await wait_until(lambda: healthy() and any(frame["kind"] == "marker" for item in proxy.events
                    for frame in item.get("frames", {}).get("datagrams", [])), 2, "fresh DATAGRAM did not progress after real ACK recovery")
                await asyncio.sleep(0.075)
                final_ids = [frame["id"] for item in proxy.burst_packets() for frame in item["frames"]["datagrams"] if frame["kind"] == "burst"]
                assert final_ids == identifiers, "old dropped DATAGRAM payload was retransmitted"
                result["admission"]["ack_recovery_marker"] = True
            if args.scenario in ("all", "pto"):
                await warmup()
                started = time.monotonic()
                stream, response = peer.request("pto", lambda sid: proxy.arm("pto", sid))
                await wait_until(lambda: healthy() and proxy.repeats, 3, "no real reliable-data PTO repeat while ACKs held")
                assert proxy.mode == "hold" and proxy.held
                repeat = proxy.repeats[0]
                assert repeat["repeat"]["pn"] > repeat["original"]["pn"]
                assert len(response["body"]) < 262144, "response did not exhaust available cwnd"
                result["pto"] = {"stream": stream, "repeat": repeat, "held_packets": len(proxy.held),
                    "received_before_ack_release": len(response["body"]), "repeat_elapsed_ms": (time.monotonic() - started) * 1000}
                proxy.release()
                await body(response, b"P" * 262144)
                result["pto"]["complete_body_bytes"] = len(response["body"])
                result["pto"]["complete_body_sha256"] = hashlib.sha256(response["body"]).hexdigest()
                result["pto"]["fin"] = response["fin"]
            healthy()
            result["status"] = "expected_overflow_proven" if args.expect == "overflow" else "passed"
    finally:
        if peer is not None:
            for response in peer.responses.values():
                future = response["done"]
                if not future.done():
                    future.cancel()
                elif not future.cancelled():
                    future.exception()
        result["proxy_errors"] = proxy.errors
        (args.records / "proxy-ledger.json").write_text(json.dumps(proxy.events, indent=2), encoding="utf-8")
        proxy.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server", type=Path)
    parser.add_argument("--helper-dir", type=Path, default=Path(__file__).parent)
    parser.add_argument("--records", type=Path)
    parser.add_argument("--expect", choices=("pass", "overflow"), default="pass")
    parser.add_argument("--scenario", choices=("all", "admission", "pto"), default="all")
    args = parser.parse_args()
    if args.expect == "overflow" and args.scenario == "pto":
        parser.error("overflow control requires admission scenario")
    if args.records is None:
        args.records = Path(tempfile.mkdtemp(prefix="quic-datagram-cc-"))
    if args.records.exists() and any(args.records.iterdir()):
        parser.error("records must be fresh; refusing evidence overwrite")
    if not args.server.is_file():
        parser.error("server binary is missing")
    args.records.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(args.helper_dir))
    try:
        from http_client_e2e import make_certificate, reserve_port
    except ImportError:
        return 77
    result = {"status": "failed", "expect": args.expect, "scenario": args.scenario,
        "server_sha256": hashlib.sha256(args.server.read_bytes()).hexdigest(),
        "driver_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "aioquic_version": importlib.metadata.version("aioquic"), "peer_aead": "unmodified installed aioquic"}
    process = None
    try:
        certificate, key = make_certificate(args.records)
        port = reserve_port()
        command = [str(args.server.resolve()), "--port", str(port), "--cert", str(certificate.resolve()), "--key", str(key.resolve()),
            "--listening-report", str((args.records / "listening.txt").resolve()), "--ready-report", str((args.records / "ready.txt").resolve()),
            "--burst-report", str((args.records / "burst.json").resolve()), "--result-report", str((args.records / "failure.txt").resolve())]
        result["command"] = command
        with (args.records / "server.log").open("w", encoding="utf-8") as output:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
            async def run():
                await wait_until(lambda: (args.records / "listening.txt").exists(), 3, "server did not listen")
                await exercise(args, process, port, result)
            asyncio.run(run())
    except Exception as error:
        result["status"] = "failed"
        result["error"] = repr(error)
    finally:
        if process:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
            result["server_exit_code"] = process.returncode
        (args.records / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result, sort_keys=True), flush=True)
    accepted = "expected_overflow_proven" if args.expect == "overflow" else "passed"
    return 0 if result["status"] == accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
