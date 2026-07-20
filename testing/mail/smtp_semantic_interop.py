#!/usr/bin/env python3
"""Bidirectional semantic interoperability test using Python SMTP libraries."""

from __future__ import annotations

import asyncio
import contextlib
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

try:
    import aiosmtplib
    from aiosmtpd.controller import Controller
except ImportError as error:
    print(
        f"SKIP: install testing/mail/requirements.txt to run SMTP interoperability: {error}",
        file=sys.stderr,
    )
    raise SystemExit(77)


HOST = "127.0.0.1"
TIMEOUT = 8.0


def reserve_port() -> int:
    with contextlib.closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
        sock.bind((HOST, 0))
        return int(sock.getsockname()[1])


def wait_port(port: int) -> None:
    deadline = time.monotonic() + TIMEOUT
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((HOST, port), timeout=0.1):
                return
        except OSError:
            time.sleep(0.02)
    raise TimeoutError(f"SMTP server did not listen on {port}")


def wait_file(path: Path) -> str:
    deadline = time.monotonic() + TIMEOUT
    while time.monotonic() < deadline:
        if path.exists():
            return path.read_bytes().decode("utf-8")
        time.sleep(0.02)
    raise TimeoutError("cnetmod SMTP server did not deliver the message")


async def assert_cnetmod_server(server: Path) -> None:
    """aiosmtplib validates cnetmod's SMTP command and DATA semantics."""
    port = reserve_port()
    with tempfile.TemporaryDirectory() as directory:
        result = Path(directory) / "received.txt"
        process = subprocess.Popen([str(server), str(port), str(result)])
        try:
            wait_port(port)
            client = aiosmtplib.SMTP(
                hostname=HOST,
                port=port,
                timeout=TIMEOUT,
                local_hostname="python.semantic.test",
            )
            await client.connect()
            rejected = await client.execute_command(b"DATA")
            assert rejected.code == 503, f"DATA before envelope returned {rejected.code}"
            response = await client.ehlo()
            assert response.code == 250, f"EHLO returned {response.code}"
            await client.login("semantic-user", "semantic-password")
            refused, _response = await client.sendmail(
                "sender@example.test",
                ["recipient@example.test"],
                "From: sender@example.test\r\n"
                "To: recipient@example.test\r\n"
                "Subject: Python semantic interop\r\n"
                "\r\nfirst line\r\n.leading dot\r\n..double dot\r\n",
            )
            assert not refused, f"recipient rejected: {refused}"
            await client.quit()

            delivered = wait_file(result)
            assert "FROM=sender@example.test\n" in delivered
            assert "TO=recipient@example.test\n" in delivered
            assert "HEADER=Subject:Python semantic interop\n" in delivered
            assert "BODY\nfirst line\r\n.leading dot\r\n..double dot\r\n" in delivered
        finally:
            process.terminate()
            with contextlib.suppress(subprocess.TimeoutExpired):
                process.wait(timeout=2.0)
            if process.poll() is None:
                process.kill()
                process.wait()


class CapturingHandler:
    """aiosmtpd handler: accepts the cnetmod client as a real SMTP peer."""

    def __init__(self) -> None:
        self.mail_from = ""
        self.recipients: list[str] = []
        self.content = b""

    async def handle_DATA(self, _server, _session, envelope):
        self.mail_from = envelope.mail_from
        self.recipients = list(envelope.rcpt_tos)
        self.content = envelope.content
        return "250 2.0.0 accepted by aiosmtpd"


async def assert_cnetmod_client(client_binary: Path) -> None:
    """aiosmtpd verifies cnetmod client serialization and dot unstuffing."""
    handler = CapturingHandler()
    port = reserve_port()
    controller = Controller(handler, hostname=HOST, port=port)
    controller.start()
    try:
        completed = await asyncio.to_thread(
            subprocess.run,
            [str(client_binary), str(port)],
            capture_output=True,
            text=True,
            timeout=TIMEOUT,
        )
        assert completed.returncode == 0, completed.stderr or completed.stdout
        assert handler.mail_from == "sender@example.test"
        assert handler.recipients == ["primary@example.test", "copy@example.test"]
        text = handler.content.decode("utf-8")
        assert "Subject: cnetmod SMTP semantic interop\r\n" in text
        assert "first line\r\n.leading dot\r\n..double dot\r\n" in text
    finally:
        controller.stop()


async def run(server: Path, client: Path) -> None:
    await assert_cnetmod_server(server)
    await assert_cnetmod_client(client)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: smtp_semantic_interop.py <smtp_interop_server> <smtp_interop_client>", file=sys.stderr)
        return 2
    server, client = (Path(argument) for argument in sys.argv[1:])
    asyncio.run(run(server, client))
    print("PASS aiosmtplib ↔ cnetmod SMTP semantic interoperability")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
