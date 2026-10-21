"""Observe cold listener reclamation with no inbound packet during the wait."""

import asyncio
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

from http3_lifecycle import Probe, configuration
from http_client_e2e import make_certificate, reserve_port, stop_process
from aioquic.asyncio import connect
from aioquic.quic.connection import QuicConnection


async def prepare(port):
    # An uncompleted Initial creates a dispatcher orphan on Windows. Retain
    # the socket until the stateless Retry has arrived, avoiding an ICMP error.
    orphan = QuicConnection(configuration=configuration())
    orphan.connect(("127.0.0.1", port), now=time.monotonic())
    datagrams = orphan.datagrams_to_send(now=time.monotonic())
    assert datagrams
    async with connect("127.0.0.1", port, configuration=configuration(),
                       create_protocol=Probe) as peer:
        # The normal handshake retries during listener startup. Only submit
        # the deliberately one-shot orphan after that handshake succeeded.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            sender.setblocking(False)
            loop = asyncio.get_running_loop()
            target = datagrams[0][1]
            assert all(candidate == target for _, candidate in datagrams)
            sender.connect(target)
            for data, _ in datagrams:
                await loop.sock_sendall(sender, data)
            await asyncio.wait_for(loop.sock_recv(sender, 65536), 5)
        assert await peer.get(port, b"/cold-cleanup-start") == b"ok\n"
    # Context-manager teardown closes the UDP transport. From this point no
    # probe, keepalive or statistics request is sent to the server.


def main():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cert, key = make_certificate(root)
        report = root / "cold-snapshots.txt"
        port = reserve_port()
        server_log = root / "server.log"
        with server_log.open("w+") as log:
            server = subprocess.Popen([str(Path(sys.argv[1]).resolve()),
                "--port", str(port), "--cert", str(cert), "--key", str(key),
                "--workers", "2", "--lifecycle-test", "--cold-cleanup-report", str(report)],
                cwd=root, stdout=log, stderr=log)
            try:
                # Linux binds reuseport worker sockets successively. Starting
                # QUIC before the complete socket set exists can change the
                # selected worker during the handshake. This file-only barrier
                # sends no packet and leaves the 500ms idle policy unchanged.
                ready = f"listener started on UDP {port} with 2 workers"
                deadline = time.monotonic() + 10
                while ready not in server_log.read_text():
                    assert server.poll() is None, "server exited before ready"
                    assert time.monotonic() < deadline, "listener did not become ready"
                    time.sleep(0.01)
                asyncio.run(prepare(port))
                started = time.monotonic()
                while time.monotonic() - started < 42:
                    assert server.poll() is None, "server exited"
                    rows = report.read_text().splitlines() if report.exists() else []
                    if len(rows) == 2:
                        break
                    time.sleep(0.1)
                else:
                    raise AssertionError("internal snapshot timer did not finish")
                before, after = [list(map(int, row.split())) for row in rows]
                assert before[1] > 0, ("fixture did not retain reset routes", before)
                if sys.platform == "win32":
                    assert before[2] > 0 and before[4] > 0, ("missing orphan", before)
                assert after == [0] * 6, (before, after)
                print(f"PASS zero-ingress 35s cleanup: {before} -> {after}")
            except BaseException:
                log.seek(0)
                print(log.read(), file=sys.stderr)
                raise
            finally:
                stop_process(server)


if __name__ == "__main__":
    main()
