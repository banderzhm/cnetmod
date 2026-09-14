#!/usr/bin/env python3
"""Executable HTTP/3 interoperability gate.

Every required release-gate case must use the aioquic peer installed by CI.
An unavailable required peer is a failing gate, not a passing or skipped
result. Optional curl, nghttp3 and Rust wtransport checks retain their skip
behaviour because the workflow does not provision those external tools.
"""

from __future__ import annotations

import argparse
import json
import shutil
import shlex
import socket
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Callable

SKIP = 77


class SkipCase(Exception):
    """The environment cannot run this peer; it is not a passed case."""


@dataclass
class Result:
    name: str
    status: str
    detail: str
    elapsed_ms: int = 0


def run(command: list[str], timeout: int, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, cwd=cwd)


def make_certificate(directory: Path) -> tuple[Path, Path]:
    openssl = shutil.which("openssl")
    if not openssl:
        raise RuntimeError("openssl is required to create an ephemeral test certificate")
    cert, key = directory / "cert.pem", directory / "key.pem"
    config = directory / "openssl.cnf"
    config.write_text(
        "[req]\n"
        "distinguished_name = distinguished_name\n"
        "prompt = no\n"
        "x509_extensions = v3_req\n"
        "[distinguished_name]\n"
        "CN = localhost\n"
        "[v3_req]\n"
        "subjectAltName = DNS:localhost,IP:127.0.0.1\n",
        encoding="utf-8",
    )
    completed = run([openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-config", str(config), "-keyout", str(key), "-out", str(cert)], 20)
    if completed.returncode:
        raise RuntimeError(completed.stderr.strip())
    return cert, key


def reserve_udp_port() -> int:
    """Choose an unused loopback UDP port for an isolated peer process."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def wait_for_process(process: subprocess.Popen[str], seconds: float = 1.0) -> None:
    time.sleep(seconds)
    if process.poll() is not None:
        stdout, stderr = process.communicate()
        raise RuntimeError(f"peer exited early (stdout={stdout!r}, stderr={stderr!r})")


def curl_http3(url: str) -> None:
    curl = shutil.which("curl")
    if not curl:
        raise FileNotFoundError("curl")
    version = run([curl, "--version"], 5)
    if "http3" not in version.stdout.lower():
        raise SkipCase("curl lacks HTTP/3 support")
    reply = run([curl, "--http3-only", "--insecure", "--silent", "--show-error", "--fail", url], 20)
    if reply.returncode:
        raise AssertionError(reply.stderr.strip() or reply.stdout.strip())
    if "ok" not in reply.stdout.lower():
        raise AssertionError(f"unexpected response body: {reply.stdout!r}")


def cnetmod_client(binary: Path, port: int) -> None:
    reply = run([str(binary), "127.0.0.1", str(port), "/health"], 20)
    if reply.returncode:
        raise AssertionError(reply.stderr.strip() or reply.stdout.strip())
    output = reply.stdout + reply.stderr
    if "Status: 200" not in output:
        raise AssertionError(f"unexpected client result: {output!r}")


def case(name: str, action: Callable[[], None],
         success_detail: str = "request and response validated") -> Result:
    began = time.monotonic()
    try:
        action()
        return Result(name, "passed", success_detail, int((time.monotonic() - began) * 1000))
    except (FileNotFoundError, SkipCase) as exc:
        return Result(name, "skipped", str(exc), int((time.monotonic() - began) * 1000))
    except Exception as exc:  # keep each peer case independent
        return Result(name, "failed", str(exc), int((time.monotonic() - began) * 1000))


def main() -> int:
    parser = argparse.ArgumentParser(description="HTTP/3 interoperability release gate")
    parser.add_argument("--server", type=Path, required=True, help="cnetmod h3_interop_server executable")
    parser.add_argument("--client", type=Path, required=True, help="cnetmod h3_interop_client executable")
    parser.add_argument("--aioquic-peer", type=Path, default=Path(__file__).with_name("h3_aioquic_peer.py"))
    parser.add_argument("--aioquic-webtransport-probe", type=Path,
                        default=Path(__file__).with_name("h3_webtransport_aioquic_probe.py"))
    parser.add_argument("--wtransport-probe-command",
                        help="Rust wtransport probe command template; use {url}")
    parser.add_argument("--wtransport-probe", type=Path,
                        help="built Rust wtransport probe executable")
    parser.add_argument("--wtransport-repeats", type=int, default=3,
                        help="number of independent wtransport probes (default: 3)")
    parser.add_argument("--server-workers", type=int, default=1,
                        help="number of cnetmod HTTP/3 server workers (default: 1)")
    parser.add_argument("--port", type=int, default=4433)
    parser.add_argument("--results", type=Path, default=Path("h3-interop-results.json"))
    parser.add_argument("--nghttp3-client-command", help="external HTTP/3 client command template; use {url}")
    parser.add_argument("--nghttp3-server-command", help="external HTTP/3 server command template; use {port}, {cert}, {key}")
    args = parser.parse_args()

    cases: list[Result] = []
    required_cases: set[str] = {
        "aioquic-server_to_cnetmod-client",
        "aioquic-webtransport_to_cnetmod-server",
    }

    def cnetmod_server_to_curl() -> None:
        if not args.server.is_file():
            raise FileNotFoundError(args.server)
        with tempfile.TemporaryDirectory(prefix="cnetmod-h3-") as temp:
            directory = Path(temp)
            make_certificate(directory)
            port = reserve_udp_port()
            process = subprocess.Popen([str(args.server), "--port", str(port),
                "--workers", str(args.server_workers)], cwd=directory, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                wait_for_process(process)
                # The cnetmod fixture listener intentionally binds an IPv4
                # wildcard endpoint.  `localhost` resolution is platform
                # dependent and may select ::1 first, which is not a QUIC
                # protocol failure but causes a misleading handshake timeout.
                # Keep this loopback acceptance path on the explicit IPv4
                # address covered by the generated certificate SAN.
                curl_http3(f"https://127.0.0.1:{port}/health")
            finally:
                process.terminate()
                try:
                    process.wait(5)
                except subprocess.TimeoutExpired:
                    process.kill()

    def aioquic_server_to_cnetmod() -> None:
        try:
            import aioquic  # noqa: F401
        except ImportError as exc:
            raise SkipCase(f"aioquic unavailable: {exc}") from exc
        python = sys.executable
        if not args.client.is_file():
            raise FileNotFoundError(args.client)
        if not args.aioquic_peer.is_file():
            raise FileNotFoundError(args.aioquic_peer)
        with tempfile.TemporaryDirectory(prefix="cnetmod-h3-") as temp:
            directory = Path(temp)
            cert, key = make_certificate(directory)
            # Keep peer directions on distinct UDP ports. A just-terminated
            # cnetmod fixture can still own the previous port briefly while
            # its event loop unwinds, which otherwise creates a false client
            # failure unrelated to protocol interoperability.
            aioquic_port = reserve_udp_port()
            process = subprocess.Popen([python, str(args.aioquic_peer), "--port", str(aioquic_port), "--cert", str(cert), "--key", str(key)], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                wait_for_process(process)
                try:
                    cnetmod_client(args.client, aioquic_port)
                except AssertionError as exc:
                    process.terminate()
                    _, stderr = process.communicate(timeout=5)
                    raise AssertionError(f"{exc}\naioquic stderr:\n{stderr}") from exc
            finally:
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(5)
                except subprocess.TimeoutExpired:
                    process.kill()

    def aioquic_webtransport_to_cnetmod() -> None:
        try:
            import aioquic  # noqa: F401
        except ImportError as exc:
            raise SkipCase(f"aioquic unavailable: {exc}") from exc
        if not args.server.is_file():
            raise FileNotFoundError(args.server)
        if not args.aioquic_webtransport_probe.is_file():
            raise FileNotFoundError(args.aioquic_webtransport_probe)
        with tempfile.TemporaryDirectory(prefix="cnetmod-h3-webtransport-") as temp:
            directory = Path(temp)
            cert, key = make_certificate(directory)
            port = reserve_udp_port()
            process = subprocess.Popen(
                [str(args.server), "--port", str(port), "--cert", str(cert), "--key", str(key),
                 "--workers", str(args.server_workers), "--webtransport"],
                cwd=directory, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            try:
                wait_for_process(process)
                try:
                    completed = run([sys.executable, str(args.aioquic_webtransport_probe),
                                     "127.0.0.1", str(port)], 20)
                except subprocess.TimeoutExpired as exc:
                    process.terminate()
                    _, stderr = process.communicate(timeout=5)
                    raise AssertionError(
                        f"aioquic WebTransport probe timed out after {exc.timeout}s\n"
                        f"cnetmod server stderr:\n{stderr}"
                    ) from exc
                if completed.returncode == SKIP:
                    raise SkipCase(completed.stdout.strip() or completed.stderr.strip())
                if completed.returncode:
                    process.terminate()
                    _, stderr = process.communicate(timeout=5)
                    raise AssertionError(
                        f"{completed.stderr.strip() or completed.stdout.strip()}\n"
                        f"cnetmod server stderr:\n{stderr}"
                    )
            finally:
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(5)
                except subprocess.TimeoutExpired:
                    process.kill()

    def wtransport_webtransport_to_cnetmod() -> None:
        if not args.wtransport_probe and not args.wtransport_probe_command:
            raise SkipCase("--wtransport-probe or --wtransport-probe-command was not supplied")
        if args.wtransport_repeats < 1:
            raise ValueError("--wtransport-repeats must be at least 1")
        if args.wtransport_probe and not args.wtransport_probe.is_file():
            raise FileNotFoundError(args.wtransport_probe)
        if not args.server.is_file():
            raise FileNotFoundError(args.server)
        with tempfile.TemporaryDirectory(prefix="cnetmod-h3-webtransport-") as temp:
            directory = Path(temp)
            cert, key = make_certificate(directory)
            for attempt in range(1, args.wtransport_repeats + 1):
                # Use a fresh fixture process for every probe.  A failed or
                # cancelled WebTransport session must not leave connection-
                # local state that can affect the next independent run.
                port = reserve_udp_port()
                process = subprocess.Popen(
                    [str(args.server), "--port", str(port), "--cert", str(cert), "--key", str(key),
                     "--workers", str(args.server_workers), "--webtransport"],
                    cwd=directory, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                )
                try:
                    wait_for_process(process)
                    url = f"https://127.0.0.1:{port}/webtransport"
                    command = ([str(args.wtransport_probe), url] if args.wtransport_probe else
                               shlex.split(args.wtransport_probe_command.format(url=url)))
                    completed = run(command, 30)
                    if completed.returncode == SKIP:
                        raise SkipCase(completed.stdout.strip() or completed.stderr.strip())
                    if completed.returncode:
                        process.terminate()
                        _, stderr = process.communicate(timeout=5)
                        raise AssertionError(
                            f"wtransport probe {attempt}/{args.wtransport_repeats} failed: "
                            f"{completed.stderr.strip() or completed.stdout.strip()}\n"
                            f"cnetmod server stderr:\n{stderr}"
                        )
                    if "WebTransport wtransport -> cnetmod:" not in completed.stdout:
                        raise AssertionError(
                            f"wtransport probe {attempt}/{args.wtransport_repeats} returned "
                            f"unexpected output: {completed.stdout!r}"
                        )
                finally:
                    if process.poll() is None:
                        process.terminate()
                    try:
                        process.wait(5)
                    except subprocess.TimeoutExpired:
                        process.kill()

    def cnetmod_server_to_nghttp3() -> None:
        if not args.nghttp3_client_command:
            raise SkipCase("--nghttp3-client-command was not supplied")
        if not args.server.is_file():
            raise FileNotFoundError(args.server)
        with tempfile.TemporaryDirectory(prefix="cnetmod-h3-") as temp:
            directory = Path(temp)
            make_certificate(directory)
            port = reserve_udp_port()
            process = subprocess.Popen([str(args.server), "--port", str(port),
                "--workers", str(args.server_workers)], cwd=directory, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                wait_for_process(process)
                command = shlex.split(args.nghttp3_client_command.format(
                    url=f"https://127.0.0.1:{port}/health"))
                completed = run(command, 20)
                if completed.returncode:
                    raise AssertionError(completed.stderr.strip() or completed.stdout.strip())
                if "ok" not in (completed.stdout + completed.stderr).lower():
                    raise AssertionError(f"unexpected nghttp3 client response: {completed.stdout!r}")
            finally:
                process.terminate()
                try:
                    process.wait(5)
                except subprocess.TimeoutExpired:
                    process.kill()

    def nghttp3_server_to_cnetmod() -> None:
        if not args.nghttp3_server_command:
            raise SkipCase("--nghttp3-server-command was not supplied")
        if not args.client.is_file():
            raise FileNotFoundError(args.client)
        with tempfile.TemporaryDirectory(prefix="cnetmod-h3-") as temp:
            directory = Path(temp)
            cert, key = make_certificate(directory)
            (directory / "health").write_text("ok\n", encoding="utf-8")
            port = reserve_udp_port()
            command = shlex.split(args.nghttp3_server_command.format(
                port=port, cert=cert, key=key, root=directory))
            process = subprocess.Popen(command, cwd=directory, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                wait_for_process(process)
                try:
                    cnetmod_client(args.client, port)
                except AssertionError as exc:
                    process.terminate()
                    stdout, stderr = process.communicate(timeout=5)
                    raise AssertionError(
                        f"{exc}\nnghttp3 server stdout:\n{stdout}\nnghttp3 server stderr:\n{stderr}"
                    ) from exc
            finally:
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(5)
                except subprocess.TimeoutExpired:
                    process.kill()

    cases.append(case("aioquic-server_to_cnetmod-client", aioquic_server_to_cnetmod))
    cases.append(case("aioquic-webtransport_to_cnetmod-server", aioquic_webtransport_to_cnetmod))
    cases.append(case(
        "wtransport-webtransport_to_cnetmod-server", wtransport_webtransport_to_cnetmod,
        f"{args.wtransport_repeats} independent connections validated"))
    cases.append(case("cnetmod-server_to_curl-http3", cnetmod_server_to_curl))
    cases.append(case("cnetmod-server_to_nghttp3-client", cnetmod_server_to_nghttp3))
    cases.append(case("nghttp3-server_to_cnetmod-client", nghttp3_server_to_cnetmod))
    args.results.write_text(json.dumps([asdict(item) for item in cases], indent=2), encoding="utf-8")
    for item in cases:
        print(f"[{item.status.upper()}] {item.name}: {item.detail}")
    if any(item.status == "failed" for item in cases):
        return 1
    skipped_required = [item.name for item in cases
                        if item.name in required_cases and item.status == "skipped"]
    if skipped_required:
        print("[FAILED] required interoperability peer unavailable: " + ", ".join(skipped_required),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
