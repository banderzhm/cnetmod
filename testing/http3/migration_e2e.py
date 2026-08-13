"""Run the HTTP/3 connection-migration probe through a source-port switch."""

from __future__ import annotations

import argparse
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SKIP = 77


def reserve_port(sock_type: int) -> int:
    with socket.socket(socket.AF_INET, sock_type) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def make_certificate(directory: Path) -> tuple[Path, Path]:
    openssl = shutil.which("openssl")
    if openssl is None:
        raise RuntimeError("openssl is required to create the migration certificate")
    config = directory / "openssl.cnf"
    cert = directory / "cert.pem"
    key = directory / "key.pem"
    config.write_text(
        "[req]\n"
        "distinguished_name = dn\n"
        "x509_extensions = v3_req\n"
        "prompt = no\n"
        "[dn]\nCN = localhost\n"
        "[v3_req]\nsubjectAltName = DNS:localhost,IP:127.0.0.1\n",
        encoding="utf-8",
    )
    result = subprocess.run(
        [openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
         "-config", str(config), "-keyout", str(key), "-out", str(cert)],
        capture_output=True, text=True, timeout=20,
    )
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or "openssl failed")
    return cert, key


def wait_for_process(process: subprocess.Popen[str]) -> None:
    time.sleep(1)
    if process.poll() is not None:
        stdout, stderr = process.communicate()
        raise RuntimeError(
            f"fixture exited early ({process.returncode})\nstdout:\n{stdout}\nstderr:\n{stderr}"
        )


def stop(process: subprocess.Popen[str]) -> tuple[str, str]:
    if process.poll() is None:
        process.terminate()
    try:
        return process.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        return process.communicate()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    parser.add_argument("client", type=Path)
    parser.add_argument("proxy", type=Path)
    args = parser.parse_args()
    if not args.server.is_file() or not args.client.is_file() or not args.proxy.is_file():
        raise FileNotFoundError("HTTP/3 migration fixture is missing")

    try:
        with tempfile.TemporaryDirectory(prefix="cnetmod-h3-migration-") as temp:
            directory = Path(temp)
            cert, key = make_certificate(directory)
            server_port = reserve_port(socket.SOCK_DGRAM)
            proxy_port = reserve_port(socket.SOCK_DGRAM)
            marker = directory / "switch.marker"
            observed = directory / "path-moved.marker"
            server = subprocess.Popen(
                [str(args.server), "--port", str(server_port), "--cert", str(cert),
                 "--key", str(key)],
                cwd=directory, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            proxy = None
            try:
                wait_for_process(server)
                proxy = subprocess.Popen(
                    [sys.executable, str(args.proxy), "--listen", str(proxy_port),
                     "--target-port", str(server_port), "--marker", str(marker),
                     "--observed", str(observed)],
                    cwd=directory, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                )
                wait_for_process(proxy)
                result = subprocess.run(
                    [str(args.client), "127.0.0.1", str(proxy_port), str(marker)],
                    cwd=directory, text=True, capture_output=True, timeout=45,
                )
                if result.returncode:
                    proxy_out, proxy_err = stop(proxy)
                    server_out, server_err = stop(server)
                    raise RuntimeError(
                        f"migration client failed ({result.returncode})\n"
                        f"client stdout:\n{result.stdout}\nclient stderr:\n{result.stderr}\n"
                        f"proxy stderr:\n{proxy_err}\nserver stderr:\n{server_err}"
                    )
                if not observed.exists():
                    raise RuntimeError("proxy never observed a packet on the migrated path")
                print("HTTP/3 connection migration: path validation and second request passed")
                return 0
            finally:
                if proxy is not None:
                    stop(proxy)
                stop(server)
    except RuntimeError as error:
        if "openssl is required" in str(error):
            print(str(error), file=sys.stderr)
            return SKIP
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
