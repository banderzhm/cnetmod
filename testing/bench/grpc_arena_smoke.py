#!/usr/bin/env python3
"""Bounded, low-load startup and wire-contract regression, optionally with TLS."""
import argparse
import importlib.util
from pathlib import Path
import socket
import subprocess
import sys
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server", type=Path)
    parser.add_argument("--openssl")
    args = parser.parse_args()
    if importlib.util.find_spec("grpc") is None:
        print("SKIP: grpcio is required for the independent wire oracle")
        return 77
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cert, key = root / "cert.pem", root / "key.pem"
        if args.openssl:
            subprocess.run([args.openssl, "req", "-x509", "-newkey", "rsa:2048",
                            "-nodes", "-keyout", str(key), "-out", str(cert),
                            "-days", "1", "-subj", "/CN=localhost",
                            "-addext", "subjectAltName=DNS:localhost"],
                           check=True, capture_output=True, timeout=15)
        for tls in ([False, True] if args.openssl else [False]):
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            command = [str(args.server.resolve()), str(port), "1"]
            if tls:
                command += [str(cert), str(key)]
            with (root / "server.log").open("w+") as log:
                server = subprocess.Popen(command, stdout=log, stderr=log)
                try:
                    check = [sys.executable, str(Path(__file__).with_name("grpc_arena_check.py")),
                             f"127.0.0.1:{port}"]
                    if tls:
                        check += ["--cert", str(cert)]
                    subprocess.run(check, check=True, timeout=20)
                    if server.poll() is not None:
                        raise RuntimeError(f"server exited: {server.returncode}")
                except Exception:
                    log.seek(0)
                    print(log.read(), file=sys.stderr)
                    raise
                finally:
                    server.terminate()
                    try:
                        server.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        server.kill()
                        server.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
