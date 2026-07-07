import argparse
import select
import socket
import subprocess
import sys
import time
from pathlib import Path

from OpenSSL import SSL


def drive_dtls(conn: SSL.Connection, sock: socket.socket, fn, timeout: float = 10.0):
    deadline = time.time() + timeout
    while True:
        try:
            return fn()
        except (SSL.WantReadError, SSL.WantWriteError) as exc:
            if time.time() > deadline:
                raise TimeoutError("DTLS operation timed out") from exc
            if isinstance(exc, SSL.WantReadError):
                select.select([sock], [], [], 0.2)
            else:
                select.select([], [sock], [], 0.2)


def python_dtls_client(port: int, payload: bytes) -> bytes:
    ctx = SSL.Context(SSL.DTLS_CLIENT_METHOD)
    ctx.set_verify(SSL.VERIFY_NONE, lambda *_args: True)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(False)
    sock.connect(("127.0.0.1", port))

    conn = SSL.Connection(ctx, sock)
    conn.set_connect_state()
    drive_dtls(conn, sock, conn.do_handshake)
    drive_dtls(conn, sock, lambda: conn.send(payload))
    echoed = drive_dtls(conn, sock, lambda: conn.recv(65536))
    conn.close()
    return echoed


def retry_python_dtls_client(port: int, payload: bytes, timeout: float = 10.0) -> bytes:
    deadline = time.time() + timeout
    last_error: Exception | None = None
    while time.time() < deadline:
        try:
            return python_dtls_client(port, payload)
        except Exception as exc:
            last_error = exc
            time.sleep(0.1)
    raise TimeoutError(f"DTLS client could not connect: {last_error}") from last_error


class MemoryBioDtlsServer:
    def __init__(self, port: int, cert: Path, key: Path):
        self.port = port
        self.ctx = SSL.Context(SSL.DTLS_SERVER_METHOD)
        self.ctx.use_certificate_file(str(cert))
        self.ctx.use_privatekey_file(str(key))
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", port))
        self.sock.settimeout(10.0)
        self.conn = SSL.Connection(self.ctx, None)
        self.conn.set_accept_state()

    def accept_first_peer(self) -> None:
        data, peer = self.sock.recvfrom(65536)
        self.sock.connect(peer)
        self.sock.setblocking(False)
        self.conn.bio_write(data)

    def flush(self) -> None:
        while True:
            try:
                data = self.conn.bio_read(65536)
            except SSL.WantReadError:
                return
            if data:
                self.sock.send(data)

    def feed(self, timeout: float) -> None:
        deadline = time.time() + timeout
        while True:
            remaining = deadline - time.time()
            if remaining <= 0:
                raise TimeoutError("DTLS server input timed out")
            readable, _, _ = select.select([self.sock], [], [], min(0.2, remaining))
            if readable:
                self.conn.bio_write(self.sock.recv(65536))
                return

    def drive(self, fn, timeout: float = 10.0):
        deadline = time.time() + timeout
        while True:
            try:
                result = fn()
                self.flush()
                return result
            except SSL.WantReadError:
                self.flush()
                self.feed(max(0.01, deadline - time.time()))
            except SSL.WantWriteError:
                self.flush()
            if time.time() > deadline:
                raise TimeoutError("DTLS server operation timed out")

    def run_once(self) -> bytes:
        self.accept_first_peer()
        self.drive(self.conn.do_handshake)
        payload = self.drive(lambda: self.conn.recv(65536))
        self.drive(lambda: self.conn.send(payload))
        self.flush()
        return payload


def run_python_client(args: argparse.Namespace) -> None:
    proc = subprocess.Popen(
        [str(args.exe), str(args.cert), str(args.key), str(args.port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        cwd=args.cwd,
    )
    try:
        echoed = retry_python_dtls_client(args.port, args.payload.encode())
        remaining = proc.communicate(timeout=10)[0].splitlines()
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)

    if echoed != args.payload.encode():
        raise AssertionError(f"unexpected echo: {echoed!r}")
    if not any(line.startswith("DTLS_SERVER_READY") for line in remaining):
        raise AssertionError(f"C++ server did not report ready: {remaining!r}")
    print("PY_DTLS_CLIENT_OK", echoed.decode(), *remaining)


def run_python_server(args: argparse.Namespace) -> None:
    server = MemoryBioDtlsServer(args.port, args.cert, args.key)
    print("PY_DTLS_SERVER_READY", args.port, flush=True)

    proc = subprocess.Popen(
        [str(args.exe), "127.0.0.1", str(args.port), args.payload],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        cwd=args.cwd,
    )
    try:
        received = server.run_once()
        output = proc.communicate(timeout=10)[0].splitlines()
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)

    if received != args.payload.encode():
        raise AssertionError(f"unexpected received payload: {received!r}")
    if not any(line == f"DTLS_CLIENT_ECHO {args.payload}" for line in output):
        raise AssertionError(f"C++ client did not report expected echo: {output!r}")
    print("PY_DTLS_SERVER_OK", received.decode(), *output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["python-client", "python-server"], required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--payload", default="dtls-udp-ssl")
    parser.add_argument("--cert", type=Path, default=Path("examples/test_ssl/cert.pem"))
    parser.add_argument("--key", type=Path, default=Path("examples/test_ssl/key.pem"))
    parser.add_argument("--cwd", type=Path, default=Path.cwd())
    args = parser.parse_args()

    if args.mode == "python-client":
        run_python_client(args)
    else:
        run_python_server(args)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"DTLS_INTEROP_ERROR {type(exc).__name__}: {exc}", file=sys.stderr)
        raise
