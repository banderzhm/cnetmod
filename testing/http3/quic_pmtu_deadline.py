"""Real PMTU deadline regression: authenticated ACK spacing and sustained PINGs.

No fake ACK, CC setter, manual timer poll or paused timer.
The server replaces its scheduled timer after every real ingress/poll; it does
not retain the HTTP/3 heap's earlier snapshot which can accidentally rescue old.
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
    from aioquic.quic.packet import pull_quic_header
    from aioquic.tls import Epoch
    from deadline_frame_parser import frames
except ImportError:
    raise SystemExit(77)


class Backend(asyncio.DatagramProtocol):
    def __init__(self, proxy):
        self.proxy = proxy
    def datagram_received(self, data, address):
        self.proxy.receive("server", data)
    def error_received(self, error):
        self.proxy.errors.append(repr(error))


class WireProxy(asyncio.DatagramProtocol):
    def __init__(self):
        self.front = self.back = self.client = None
        self.contexts = {}
        self.highest = {"client": 0, "server": 0}
        self.events = []
        self.raw = []
        self.errors = []

    async def open(self, port):
        loop = asyncio.get_running_loop()
        self.back, _ = await loop.create_datagram_endpoint(lambda: Backend(self), remote_addr=("127.0.0.1", port))
        self.front, _ = await loop.create_datagram_endpoint(lambda: self, local_addr=("127.0.0.1", 0))
        return self.front.get_extra_info("sockname")[1]

    def error_received(self, error):
        self.errors.append(repr(error))

    def datagram_received(self, data, address):
        if self.client is None:
            self.client = address
        if self.client != address:
            self.errors.append("unexpected second peer")
            return
        self.receive("client", data)

    def decode(self, item, data):
        direction = item["direction"]
        if item.get("decoded") or direction not in self.contexts or not data or data[0] & 0x80:
            return
        try:
            header = Buffer(data=data)
            pull_quic_header(header, host_cid_length=self.cid_length)
            _, plaintext, number, _ = self.contexts[direction].decrypt_packet(data, header.tell(), self.highest[direction])
            self.highest[direction] = max(self.highest[direction], number + 1)
            item.update(decoded=True, pn=number, frames=frames(plaintext))
        except Exception as error:
            self.errors.append(repr(error))
            item.update(decoded=True, decode_error=repr(error))

    def receive(self, direction, data):
        item = {"direction": direction, "at": time.monotonic(), "bytes": len(data), "action": "forward"}
        self.decode(item, data)
        self.events.append(item)
        self.raw.append(data)
        if direction == "client":
            self.back.sendto(data)
        else:
            self.front.sendto(data, self.client)

    def attach(self, peer):
        pair = peer._quic._cryptos[Epoch.ONE_RTT]
        for direction, source in (("client", pair.send), ("server", pair.recv)):
            assert source.is_valid()
            context = CryptoContext(key_phase=source.key_phase)
            context.setup(cipher_suite=source.cipher_suite, secret=source.secret, version=source.version)
            self.contexts[direction] = context
        self.cid_length = peer._quic.configuration.connection_id_length
        # Initial0 can emit a probe before connect() returns. Authenticate the
        # original captured datagrams in arrival order once secrets exist.
        for item, data in zip(self.events, self.raw):
            self.decode(item, data)

    def probes(self, length):
        return [item for item in self.events if item["direction"] == "server" and item["bytes"] == length and
                1 in item.get("frames", {}).get("kinds", []) and 0 in item.get("frames", {}).get("kinds", [])]

    def ack_for(self, pn):
        return next((item for item in self.events if item["direction"] == "client" and
            any(low <= pn <= high for low, high in item.get("frames", {}).get("acks", []))), None)

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
        await asyncio.sleep(0.002)


async def exercise(args, process, port, result):
    proxy = WireProxy()
    proxy_port = await proxy.open(port)
    def healthy():
        assert process.poll() is None, "server exited before evidence completed"
        failure = args.records / "failure.txt"
        assert not failure.exists(), failure.read_text() if failure.exists() else ""
    config = QuicConfiguration(is_client=True, alpn_protocols=["quic-pmtu-deadline-test"], max_datagram_size=1452)
    config.verify_mode = ssl.CERT_NONE
    try:
        async with connect("127.0.0.1", proxy_port, configuration=config, create_protocol=QuicConnectionProtocol, wait_connected=True) as peer:
            proxy.attach(peer)
            result["connected_at"] = connected = time.monotonic()
            # Actual PING input continues well beyond the requested initial
            # delay; any large scheduling gap invalidates this setup.
            start = time.monotonic()
            scheduled = start
            while time.monotonic() < start + 0.65:
                healthy()
                peer._quic.send_ping(len(proxy.events))
                peer.transmit()
                scheduled += 0.02
                await asyncio.sleep(max(0, scheduled - time.monotonic()))
            stopped = time.monotonic()
            result["traffic"] = {"start": start, "stop": stopped}
            first = await wait_until(lambda: proxy.probes(1328), 2, "no real1328 padded PING observed after traffic stopped")
            first = first[0]
            ack = await wait_until(lambda: proxy.ack_for(first["pn"]), 2, "no authenticated real ACK for first probe")
            second = await wait_until(lambda: proxy.probes(1452), 2, "no real1452 padded PING after first probe ACK")
            second = second[0]
            healthy()
            assert not proxy.errors, proxy.errors
            assert first["at"] < ack["at"] < second["at"], "wire ordering cannot identify causal first-probe ACK"
            pings = [item for item in proxy.events if item["direction"] == "client" and start <= item["at"] <= stopped and
                     1 in item.get("frames", {}).get("kinds", [])]
            assert len(pings) >= 20, "invalid setup: insufficient actual PING traffic"
            gaps = [b["at"] - a["at"] for a,b in zip(pings, pings[1:])]
            assert pings[-1]["at"] - pings[0]["at"] >= 0.5 and max(gaps) <= 0.1, "invalid setup: PING stream scheduling gap"
            measurement = {"first_probe": first, "first_probe_ack_forwarded": ack, "second_probe": second,
                           "ack_to_second_ms": (second["at"]-ack["at"])*1000,
                           "first_after_connected_ms": (first["at"]-connected)*1000,
                           "first_after_traffic_start_ms": (first["at"]-start)*1000,
                           "first_after_traffic_stop_ms": (first["at"]-stopped)*1000,
                           "actual_pings": len(pings), "max_ping_gap_ms": max(gaps)*1000}
            result["measurement"] = measurement
            if args.scenario == "interval":
                assert measurement["ack_to_second_ms"] >= 180, "probe ACK's200ms interval was overwritten by short-header arming"
                assert measurement["ack_to_second_ms"] < 1500, "invalid setup: excessive scheduler or path delay"
            else:
                # On fixed source the first probe occurs during the continuous
                # PING flow. Old source's first probe appears ~200ms after it
                # stops. We collect that late actual probe before asserting.
                assert -0.03 <= first["at"]-start, "invalid setup: peer connection completed after initial quiet period"
                assert first["at"]-start <= 0.4, "continuous short headers postponed the initial200ms probe"
                assert first["at"] < stopped - 0.15, "first probe was not observed during sustained PING traffic"
            result["status"] = "passed"
    finally:
        result["proxy_errors"] = proxy.errors
        (args.records / "wire-ledger.json").write_text(json.dumps(proxy.events, indent=2))
        proxy.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server", type=Path)
    parser.add_argument("--helper-dir", type=Path, required=True)
    parser.add_argument("--records", type=Path)
    parser.add_argument("--scenario", choices=("interval", "initial"), required=True)
    parser.add_argument("--source-sha256", help="Build-controller verified source identity for external controls")
    args = parser.parse_args()
    if not args.server.is_file():
        parser.error("server is missing")
    if args.records is None:
        args.records = Path(tempfile.mkdtemp(prefix="quic-pmtu-deadline-"))
    if args.records.exists() and any(args.records.iterdir()):
        parser.error("records must be empty; do not overwrite earlier evidence")
    args.records.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(args.helper_dir))
    from http_client_e2e import make_certificate, reserve_port
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    if not args.source_sha256:
        args.source_sha256 = sha(args.helper_dir.resolve().parents[1]/"src/protocol/quic/quic_connection.cpp")
    result = {"status": "failed", "scenario": args.scenario, "source_sha256": args.source_sha256,
              "server_sha256": sha(args.server), "driver_sha256": sha(Path(__file__)), "fixture_source_sha256": sha(Path(__file__).with_name("quic_pmtu_deadline_server.cpp")),
              "parser_sha256": sha(Path(__file__).with_name("deadline_frame_parser.py")),
              "aioquic_version": importlib.metadata.version("aioquic"), "peer": "stock installed aioquic"}
    process = None
    try:
        certificate,key = make_certificate(args.records)
        port = reserve_port()
        command = [str(args.server.resolve()), "--port", str(port), "--cert", str(certificate.resolve()), "--key", str(key.resolve()),
                   "--listening-report", str((args.records/"listening.txt").resolve()),
                   "--result-report", str((args.records/"failure.txt").resolve()),
                   "--initial-delay-ms", "0" if args.scenario == "interval" else "200", "--interval-ms", "200"]
        result["command"] = command
        with (args.records / "server.log").open("w") as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            async def run():
                await wait_until(lambda: (args.records/"listening.txt").is_file(), 3, "server did not report listening")
                await exercise(args, process, port, result)
            asyncio.run(run())
    except Exception as error:
        result.update(status="failed", error=repr(error))
    finally:
        if process:
            process.terminate() if process.poll() is None else None
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
            result["server_exit"] = process.returncode
        (args.records/"result.json").write_text(json.dumps(result, indent=2))
        print(json.dumps(result, sort_keys=True), flush=True)
    return 0 if result["status"] == "passed" else 1

if __name__ == "__main__":
    raise SystemExit(main())