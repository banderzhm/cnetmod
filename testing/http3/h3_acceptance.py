#!/usr/bin/env python3
"""Executable HTTP/3 interoperability gate.

This program intentionally treats an unavailable peer implementation as a
*skip* (exit 77), never as a passed interoperability result.  A completed
case must make an HTTP/3 request and validate its response.
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
    if "Status: 200" not in reply.stdout:
        raise AssertionError(f"unexpected client result: {reply.stdout!r}")


def case(name: str, action: Callable[[], None]) -> Result:
    began = time.monotonic()
    try:
        action()
        return Result(name, "passed", "request and response validated", int((time.monotonic() - began) * 1000))
    except (FileNotFoundError, SkipCase) as exc:
        return Result(name, "skipped", str(exc), int((time.monotonic() - began) * 1000))
    except Exception as exc:  # keep each peer case independent
        return Result(name, "failed", str(exc), int((time.monotonic() - began) * 1000))


def main() -> int:
    parser = argparse.ArgumentParser(description="HTTP/3 interoperability release gate")
    parser.add_argument("--server", type=Path, required=True, help="cnetmod h3_interop_server executable")
    parser.add_argument("--client", type=Path, required=True, help="cnetmod h3_interop_client executable")
    parser.add_argument("--aioquic-peer", type=Path, default=Path(__file__).with_name("h3_aioquic_peer.py"))
    parser.add_argument("--port", type=int, default=4433)
    parser.add_argument("--results", type=Path, default=Path("h3-interop-results.json"))
    parser.add_argument("--nghttp3-client-command", help="external HTTP/3 client command template; use {url}")
    parser.add_argument("--nghttp3-server-command", help="external HTTP/3 server command template; use {port}, {cert}, {key}")
    args = parser.parse_args()

    cases: list[Result] = []

    def cnetmod_server_to_curl() -> None:
        if not args.server.is_file():
            raise FileNotFoundError(args.server)
        with tempfile.TemporaryDirectory(prefix="cnetmod-h3-") as temp:
            directory = Path(temp)
            make_certificate(directory)
            process = subprocess.Popen([str(args.server), "--port", str(args.port)], cwd=directory, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                wait_for_process(process)
                # The cnetmod fixture listener intentionally binds an IPv4
                # wildcard endpoint.  `localhost` resolution is platform
                # dependent and may select ::1 first, which is not a QUIC
                # protocol failure but causes a misleading handshake timeout.
                # Keep this loopback acceptance path on the explicit IPv4
                # address covered by the generated certificate SAN.
                curl_http3(f"https://127.0.0.1:{args.port}/health")
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

    def cnetmod_server_to_nghttp3() -> None:
        if not args.nghttp3_client_command:
            raise SkipCase("--nghttp3-client-command was not supplied")
        if not args.server.is_file():
            raise FileNotFoundError(args.server)
        with tempfile.TemporaryDirectory(prefix="cnetmod-h3-") as temp:
            directory = Path(temp)
            make_certificate(directory)
            process = subprocess.Popen([str(args.server), "--port", str(args.port)], cwd=directory, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                wait_for_process(process)
                command = shlex.split(args.nghttp3_client_command.format(url=f"https://127.0.0.1:{args.port}/health"))
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
            command = shlex.split(args.nghttp3_server_command.format(port=args.port, cert=cert, key=key, root=directory))
            process = subprocess.Popen(command, cwd=directory, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                wait_for_process(process)
                try:
                    cnetmod_client(args.client, args.port)
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
    cases.append(case("cnetmod-server_to_curl-http3", cnetmod_server_to_curl))
    cases.append(case("cnetmod-server_to_nghttp3-client", cnetmod_server_to_nghttp3))
    cases.append(case("nghttp3-server_to_cnetmod-client", nghttp3_server_to_cnetmod))
    args.results.write_text(json.dumps([asdict(item) for item in cases], indent=2), encoding="utf-8")
    for item in cases:
        print(f"[{item.status.upper()}] {item.name}: {item.detail}")
    if any(item.status == "failed" for item in cases):
        return 1
    return 0 if any(item.status == "passed" for item in cases) else SKIP


if __name__ == "__main__":
    raise SystemExit(main())
