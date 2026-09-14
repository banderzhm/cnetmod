"""Ephemeral TLS termination for real-broker interoperability tests."""

from __future__ import annotations

import datetime
import ipaddress
import select
import socket
import ssl
import tempfile
import threading
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID


class TlsProxy:
    def __init__(self, upstream_host: str, upstream_port: int) -> None:
        self.upstream = (upstream_host, upstream_port)
        self.directory = tempfile.TemporaryDirectory(prefix="cnetmod-tls-")
        self.listener: socket.socket | None = None
        self.stop_event = threading.Event()
        self.thread: threading.Thread | None = None
        self.ca_file, self.cert_file, self.key_file = self._create_identity(
            Path(self.directory.name)
        )

    @staticmethod
    def _create_identity(directory: Path) -> tuple[Path, Path, Path]:
        now = datetime.datetime.now(datetime.UTC)
        ca_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        ca_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "cnetmod test CA")])
        ca = (
            x509.CertificateBuilder()
            .subject_name(ca_name)
            .issuer_name(ca_name)
            .public_key(ca_key.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(now - datetime.timedelta(minutes=1))
            .not_valid_after(now + datetime.timedelta(days=1))
            .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
            .sign(ca_key, hashes.SHA256())
        )
        server_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        server_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")])
        server = (
            x509.CertificateBuilder()
            .subject_name(server_name)
            .issuer_name(ca.subject)
            .public_key(server_key.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(now - datetime.timedelta(minutes=1))
            .not_valid_after(now + datetime.timedelta(days=1))
            .add_extension(
                x509.SubjectAlternativeName(
                    [
                        x509.DNSName("localhost"),
                        x509.IPAddress(ipaddress.ip_address("127.0.0.1")),
                    ]
                ),
                critical=False,
            )
            .sign(ca_key, hashes.SHA256())
        )
        ca_file = directory / "ca.pem"
        cert_file = directory / "server.pem"
        key_file = directory / "server-key.pem"
        ca_file.write_bytes(ca.public_bytes(serialization.Encoding.PEM))
        cert_file.write_bytes(server.public_bytes(serialization.Encoding.PEM))
        key_file.write_bytes(
            server_key.private_bytes(
                serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption(),
            )
        )
        return ca_file, cert_file, key_file

    def start(self) -> tuple[str, int, str]:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(self.cert_file, self.key_file)
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(0.2)
        self.listener = listener
        self.thread = threading.Thread(
            target=self._accept, args=(context,), daemon=True, name="cnetmod-tls-proxy"
        )
        self.thread.start()
        return "localhost", int(listener.getsockname()[1]), str(self.ca_file)

    def _accept(self, context: ssl.SSLContext) -> None:
        assert self.listener is not None
        while not self.stop_event.is_set():
            try:
                downstream, _ = self.listener.accept()
            except TimeoutError:
                continue
            except OSError:
                break
            threading.Thread(
                target=self._relay_connection,
                args=(context, downstream),
                daemon=True,
            ).start()

    def _relay_connection(
        self, context: ssl.SSLContext, downstream_socket: socket.socket
    ) -> None:
        try:
            with downstream_socket:
                with context.wrap_socket(downstream_socket, server_side=True) as downstream:
                    with socket.create_connection(self.upstream, timeout=5) as upstream:
                        downstream.setblocking(False)
                        upstream.setblocking(False)
                        while not self.stop_event.is_set():
                            readable, _, _ = select.select(
                                [downstream, upstream], [], [], 0.2
                            )
                            for source in readable:
                                destination = upstream if source is downstream else downstream
                                data = source.recv(65536)
                                if not data:
                                    return
                                destination.sendall(data)
        except (ConnectionError, OSError, ssl.SSLError):
            return

    def stop(self) -> None:
        self.stop_event.set()
        if self.listener is not None:
            self.listener.close()
        if self.thread is not None:
            self.thread.join(timeout=2)
        self.directory.cleanup()
