"""Low-load oldFAIL/newPASS regression for the external fault-injected fixture.

No network is run by proposal preparation. Run this only in an assigned serial
test window, against isolated old/new libraries with inject_fault.py applied.
"""
from __future__ import annotations
import argparse
import asyncio
import contextlib
import errno
import json
import os
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import time


async def run_case(args, fault, mode):
    # Reuse only existing certificate/process helpers and the raw QUIC client.
    from aioquic.asyncio import connect
    from aioquic.quic.configuration import QuicConfiguration
    from http_client_e2e import make_certificate, reserve_port, stop_process
    from quic_retire_stream import RawPeer

    with contextlib.nullcontext(args.out / f"{mode}-{fault}") as root:
        root.mkdir()
        certificate, key = make_certificate(root)
        port = reserve_port()
        listening, report = root / "listening", root / "result"
        command = [str(args.binary), "--port", str(port), "--mode", mode,
                   "--cert", str(certificate), "--key", str(key),
                   "--listening-report", str(listening), "--result-report", str(report)]
        (root / "command.json").write_text(json.dumps(command, indent=2) + "\n")
        environment = dict(os.environ, CNETMOD_TEST_PACKET_FAILURE=fault)
        with (root / "server.log").open("w") as log:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=environment)
            try:
                limit = time.monotonic() + 5
                while not listening.exists():
                    assert process.poll() is None, (process.returncode, (root / "server.log").read_text())
                    assert time.monotonic() < limit, "listener did not start"
                    await asyncio.sleep(0.005)
                config = QuicConfiguration(is_client=True, alpn_protocols=["quic-retire-test"])
                config.verify_mode = ssl.CERT_NONE
                config.idle_timeout = 6
                async with connect("127.0.0.1", port, configuration=config,
                                   create_protocol=RawPeer) as peer:
                    peer.response.add_done_callback(
                        lambda future: None if future.cancelled() else future.exception())
                    peer._quic.send_stream_data(0, b"retire request\n", end_stream=True)
                    started = time.monotonic()
                    peer.transmit()
                    while not report.exists() or not report.stat().st_size:
                        assert time.monotonic() - started < 0.75, "writer waited for idle or hung"
                        await asyncio.sleep(0.005)
                    fields = report.read_text().strip().split()
                    assert len(fields) == 9, fields
                    status, category, value, elapsed, closed, next_error, candidates, committed, faults = fields
                    assert status == "error" and int(value) != 0, fields
                    assert int(elapsed) < 500 and closed == "true" and int(next_error) != 0, fields
                    assert int(candidates) == 2 and int(committed) == 1 and int(faults) == 1, fields
                    if fault == "seal":
                        assert category == "openssl", fields
                    elif fault == "hp":
                        assert category == "generic" and int(value) == errno.EINVAL, fields
                    else:
                        assert category == "generic" and int(value) == errno.EIO, fields
                    # A successfully sealed prefix existed in this batch, but
                    # nothing from that batch may reach the client after error.
                    await asyncio.sleep(0.05)
                    assert not peer.body, f"discarded batch prefix was transmitted: {len(peer.body)} bytes"
                    if peer.response.done():
                        peer.response.exception()  # Retrieve close exceptions.
                    print(json.dumps({"fault": fault, "mode": mode, "result": fields}), flush=True)
            finally:
                stop_process(process)


async def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--helpers", type=Path, required=True,
                        help="repository testing/http3 directory")
    parser.add_argument("--mode", choices=("observer", "private", "both"), default="both")
    parser.add_argument("--fault", choices=("seal", "hp", "seal-empty-error", "all"), default="all")
    parser.add_argument("--out", type=Path, help="new persistent evidence directory")
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    if args.out is None:
        args.out = Path(tempfile.mkdtemp(prefix="quic-packet-failure-"))
    else:
        args.out = args.out.resolve()
        args.out.mkdir()
    sys.path.insert(0, str(args.helpers.resolve()))
    modes = ("observer", "private") if args.mode == "both" else (args.mode,)
    for mode in modes:
        for fault in (("seal", "hp", "seal-empty-error") if args.fault == "all" else (args.fault,)):
            await run_case(args, fault, mode)


if __name__ == "__main__":
    asyncio.run(main())
