"""Real pending PMTU deferral and ACK-release wakeup regression.

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
        self.delay = 0.05
        self.armed = False
        self.gate_used = False
        self.held = []
        self.allow_ping = False
        self.ping_packet = None
        self.handles = []

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

    def covers(self, item, pn):
        return any(low <= pn <= high for low,high in item.get("frames",{}).get("acks",[]))

    def forward(self, direction, data, item):
        item["scheduled_forward_at"] = time.monotonic() + self.delay
        item["action"] = "scheduled_forward"
        def submit():
            item["forwarded_at"] = time.monotonic()
            if direction == "client":
                self.back.sendto(data)
            else:
                self.front.sendto(data,self.client)
        self.handles.append(asyncio.get_running_loop().call_later(self.delay,submit))

    def receive(self, direction, data):
        item = {"direction":direction,"at":time.monotonic(),"bytes":len(data)}
        self.decode(item,data)
        self.events.append(item)
        self.raw.append(data)
        if direction == "server":
            self.forward(direction,data,item)
            return
        first = self.probes(1328)
        if not self.gate_used and first and self.covers(item,first[0]["pn"]):
            # This is an original authenticated ACK for an actual probe. Its
            # delayed actual submission advances real MDS and triggers the
            # server's public DATAGRAM API burst. Later ACKs remain original.
            self.armed = True
            self.gate_used = True
            item["arms_gate"] = True
            self.forward(direction,data,item)
        elif self.armed:
            kinds = item.get("frames",{}).get("kinds",[])
            if self.allow_ping and 1 in kinds:
                if item.get("frames",{}).get("acks") or any(k not in (0,1) for k in kinds):
                    self.errors.append("invalid setup: requested PING carried ACK credit or other work")
                    item["action"] = "hold_invalid_ping"
                    self.held.append((data,item))
                    return
                self.ping_packet = item
                self.allow_ping = False
                self.forward(direction,data,item)
            else:
                item["action"] = "hold"
                self.held.append((data,item))
        else:
            self.forward(direction,data,item)

    def release(self):
        held = list(self.held)
        assert held, "no original ACK packets held"
        assert all(item.get("frames",{}).get("acks") and
                   all(k in (0,2,3) for k in item["frames"]["kinds"]) for _,item in held), "held input is not pure original ACK"
        self.armed = False
        self.held.clear()
        for data,item in held:
            item["released_at"] = time.monotonic()
            self.forward("client",data,item)
        return [item for _,item in held]

    def datagram_packets(self):
        return [item for item in self.events if item["direction"]=="server" and item.get("frames",{}).get("datagrams")]

    def held_acks_cover_datagrams(self):
        packets = self.datagram_packets()
        return bool(packets) and all(any(self.covers(item,row["pn"]) for _,item in self.held) for row in packets)

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
        for handle in self.handles:
            handle.cancel()
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


def read_json(path):
    try:
        return json.loads(path.read_text())
    except (FileNotFoundError,PermissionError,json.JSONDecodeError):
        return None


async def exercise(args, process, port, result):
    proxy = WireProxy()
    proxy_port = await proxy.open(port)
    ping_task = None
    def healthy():
        assert process.poll() is None, "server exited before evidence completed"
        failure = args.records/"failure.txt"
        assert not failure.exists(), failure.read_text() if failure.exists() else ""
        assert not proxy.errors, proxy.errors
    def report(name):
        healthy()
        return read_json(args.records/name)
    async def until(predicate, deadline, label):
        while time.monotonic() < deadline:
            healthy()
            value=predicate()
            if value:
                return value
            await asyncio.sleep(0.002)
        return None
    config = QuicConfiguration(is_client=True, alpn_protocols=["quic-pmtu-pending-test"],
        max_datagram_size=1452, max_datagram_frame_size=1200)
    config.verify_mode = ssl.CERT_NONE
    try:
        async with connect("127.0.0.1",proxy_port,configuration=config,create_protocol=QuicConnectionProtocol,wait_connected=True) as peer:
            proxy.attach(peer)
            burst = await wait_until(lambda: report("burst.json"),5,"no successful actual public DATAGRAM burst")
            attempt = await wait_until(lambda: report("attempt.json"),2,"no actual scheduled MTU poll after burst")
            assert burst["accepted"] == 64 and burst["finished_us"] < burst["observed_deadline_us"], "invalid setup: burst timing"
            observed_gap=burst["observed_deadline_us"]-burst["mds_observed_us"]
            assert 0 <= burst["configured_interval_us"]-observed_gap <= 2000, "invalid setup: generic deadline does not track the configured interval; typed diagnostic confirmation required"
            assert attempt["mds"] == 1328 and not proxy.probes(1452), "invalid setup: larger probe already escaped before test"
            assert proxy.armed, "invalid setup: actual first probe ACK did not arm gate"
            now = time.monotonic()
            # This Linux fixture's libc++ steady_clock and Python monotonic
            # both use CLOCK_MONOTONIC. Reject incompatible clocks or report
            # lag rather than silently extending an actual server deadline.
            report_lag = now-attempt["at_us"]/1e6
            assert -0.002 <= report_lag <= 0.05, "invalid setup: report clock/latency mismatch"
            deadline = attempt["next_deadline_us"]/1e6
            assert deadline > now, "invalid setup: next public recovery deadline already due"
            datagrams = proxy.datagram_packets()
            assert 2 <= len(datagrams) < 64, "invalid setup: public burst did not fill cwnd and discard excess"
            assert all(0x30 in x["frames"]["kinds"] or 0x31 in x["frames"]["kinds"] for x in datagrams)
            # Every accepted body is verified by the authenticated parser.
            result["measurement"]={"burst":burst,"attempt":attempt,"report_lag_ms":report_lag*1000,
                "wire_datagram_packets":len(datagrams),"wire_datagram_udp_bytes":sum(x["bytes"] for x in datagrams),
                "original_public_deadline":deadline,"delay_seconds":proxy.delay}
            if args.scenario == "ack_only":
                assert deadline-now > 2*proxy.delay+0.02, "invalid setup: insufficient real PTO margin for PING"
                # Send before delayed DATAGRAM delivery can make this PING
                # carry ACK credit; the proxy verifies and rejects piggyback.
                proxy.allow_ping = True
                ping_task=asyncio.create_task(peer.ping())
                ping=await wait_until(lambda: proxy.ping_packet,0.1,"no actual pure PING packet")
                await wait_until(lambda: ping.get("forwarded_at"),0.2,"PING not actually submitted")
                ack=await until(lambda: next((item for item in proxy.events if item["direction"]=="server" and
                    any(low<=ping["pn"]<=high for low,high in item.get("frames",{}).get("acks",[]))),None),deadline-0.01,"PING ACK")
                result["measurement"].update(ping=ping,ack=ack,last_public_state=report("state.json"))
                assert ack is not None, "pending MTU blocked ordinary ACK-only packet before original recovery deadline"
                assert ack["bytes"] <= 1328 and all(k in (0,2,3) for k in ack["frames"]["kinds"]), "ACK reply was not ordinary validated-size ACK-only"
                assert ack["at"] < deadline and not proxy.probes(1452), "ACK credit or recovery escape confounded deferral oracle"
                await asyncio.wait_for(asyncio.shield(ping_task),1)
                result["measurement"]["primary_oracle"] = "ordinary ACK-only packet before original recovery deadline"
                # This scenario ends at its ACK-only oracle. Pure-ACK release
                # and subsequent probe progress belong to the wake scenario.
            else:
                await wait_until(proxy.held_acks_cover_datagrams,1,"real held ACKs do not cover DATAGRAM flight")
                healthy()
                assert not proxy.probes(1452), "invalid setup: candidate sent before ACK release"
                assert deadline-time.monotonic() > proxy.delay+0.02, "invalid setup: insufficient real deadline margin for ACK release"
                released=proxy.release()
                await wait_until(lambda: all(x.get("forwarded_at") for x in released),0.3,"original ACK release was not actually submitted")
                actual_forward=max(x["forwarded_at"] for x in released)
                candidate=await until(lambda: proxy.probes(1452),deadline-0.01,"deferred probe wake")
                result["measurement"].update(released_original_acks=len(released),last_original_ack_forwarded_at=actual_forward,
                    larger_probe=(candidate[0] if candidate else None),last_public_state=report("state.json"))
                assert candidate, "pure ACK release left only deferred PMTU probe without a writer wake before original deadline"
                assert actual_forward <= candidate[0]["at"] < deadline, "candidate did not follow genuine ACK release before fallback"
                assert all(x["frames"]["acks"] and all(k in (0,2,3) for k in x["frames"]["kinds"]) for x in released), "ACK release carried ack-eliciting work"
            if args.scenario == "wake":
                # Observe actual candidate delivery and ACK as well as the public
                # resulting MDS. The public getter alone is not the wire oracle.
                larger=await wait_until(lambda: proxy.probes(1452),2,"no actual deferred1452 probe")
                ack=await wait_until(lambda: proxy.ack_for(larger[0]["pn"]),2,"no actual ACK covering deferred probe")
                await wait_until(lambda: ack.get("forwarded_at"),0.3,"probe ACK not submitted")
                final=await wait_until(lambda: (x if (x:=report("state.json")) and x["mds"]==1452 else None),1,"no public MDS promotion after real deferred-probe ACK")
                result["measurement"].update(larger_probe=larger[0],larger_probe_ack=ack,final_state=final)
        assert not proxy.errors, proxy.errors
        result["status"]="passed"
    finally:
        if ping_task:
            if not ping_task.done():
                ping_task.cancel()
            try:
                await ping_task
            except (asyncio.CancelledError,Exception):
                pass
        result["proxy_errors"]=proxy.errors
        result["forward_delay_proof"]=[{"direction":x["direction"],"received_at":x["at"],
            "scheduled_forward_at":x["scheduled_forward_at"],"forwarded_at":x["forwarded_at"],
            "released_at":x.get("released_at")} for x in proxy.events if "forwarded_at" in x]
        (args.records/"wire-ledger.json").write_text(json.dumps(proxy.events,indent=2))
        proxy.close()
    assert not proxy.errors, proxy.errors
    for row in result["forward_delay_proof"]:
        submitted_from=row["released_at"] if row["released_at"] is not None else row["received_at"]
        assert row["forwarded_at"]-submitted_from >= proxy.delay-0.002, "actual forwarding did not preserve configured real path delay"


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server",type=Path)
    parser.add_argument("--helper-dir",type=Path,required=True)
    parser.add_argument("--records",type=Path)
    parser.add_argument("--scenario",choices=("ack_only","wake"),required=True)
    parser.add_argument("--source-sha256",help="Build controller must verify against frozen source/archive")
    args=parser.parse_args()
    if not args.server.is_file():
        parser.error("server missing")
    if args.records is None:
        args.records=Path(tempfile.mkdtemp(prefix="quic-pmtu-pending-"))
    if args.records.exists() and any(args.records.iterdir()):
        parser.error("fresh records required; evidence overwrite refused")
    args.records.mkdir(parents=True,exist_ok=True)
    sys.path.insert(0,str(args.helper_dir))
    from http_client_e2e import make_certificate,reserve_port
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    if not args.source_sha256:
        args.source_sha256=sha(args.helper_dir.resolve().parents[1]/"src/protocol/quic/quic_connection.cpp")
    result={"status":"failed","scenario":args.scenario,"source_sha256":args.source_sha256,
        "server_sha256":sha(args.server),"driver_sha256":sha(Path(__file__)),
        "server_cpp_sha256":sha(Path(__file__).with_name("quic_pmtu_pending_server.cpp")),
        "parser_sha256":sha(Path(__file__).with_name("deadline_frame_parser.py")),
        "aioquic_version":importlib.metadata.version("aioquic"),"peer":"unmodified installed aioquic"}
    process=None
    try:
        cert,key=make_certificate(args.records)
        port=reserve_port()
        command=[str(args.server.resolve()),"--port",str(port),"--cert",str(cert.resolve()),"--key",str(key.resolve()),
            "--listening-report",str((args.records/"listening.txt").resolve()),"--result-report",str((args.records/"failure.txt").resolve()),
            "--burst-report",str((args.records/"burst.json").resolve()),"--attempt-report",str((args.records/"attempt.json").resolve()),
            "--state-report",str((args.records/"state.json").resolve()),"--initial-delay-ms","200","--interval-ms","10"]
        result["command"]=command
        with (args.records/"server.log").open("w") as log:
            process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
            async def run():
                await wait_until(lambda:(args.records/"listening.txt").is_file(),3,"server not listening")
                await asyncio.wait_for(exercise(args,process,port,result),10)
            asyncio.run(run())
    except Exception as error:
        result.update(status="failed",error=repr(error))
    finally:
        if process:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
            result["server_exit"]=process.returncode
        (args.records/"result.json").write_text(json.dumps(result,indent=2))
        print(json.dumps(result,sort_keys=True),flush=True)
    return 0 if result["status"]=="passed" else 1

if __name__=="__main__":
    raise SystemExit(main())
