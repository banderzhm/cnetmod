"""Run the public http::client against a local cnetmod HTTP/3 server."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import queue
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time


SKIP = 77


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return int(probe.getsockname()[1])


def make_certificate(directory: Path) -> tuple[Path, Path]:
    openssl = shutil.which("openssl")
    if openssl is None:
        raise RuntimeError("openssl is required to create the local HTTP/3 certificate")
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
    completed = subprocess.run(
        [openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes",
         "-days", "1", "-config", str(config), "-keyout", str(key),
         "-out", str(cert)], capture_output=True, text=True, timeout=20,
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr.strip() or "openssl failed")
    return cert, key


def wait_for_server(process: subprocess.Popen[str]) -> None:
    readiness: queue.Queue[str] = queue.Queue(maxsize=1)

    def read_readiness() -> None:
        assert process.stderr is not None
        readiness.put(process.stderr.readline())

    threading.Thread(target=read_readiness, daemon=True).start()
    try:
        line = readiness.get(timeout=10)
    except queue.Empty as error:
        raise RuntimeError("HTTP/3 server did not report readiness") from error
    if "HTTP/3 E2E server listening" not in line:
        stdout, stderr = stop_process(process)
        raise RuntimeError(
            f"HTTP/3 server exited before readiness ({process.returncode})\n"
            f"stdout:\n{stdout}\nstderr:\n{line}{stderr}"
        )


def stop_process(process: subprocess.Popen[str]) -> tuple[str, str]:
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
    parser.add_argument("--dynamic-qpack", action="store_true")
    parser.add_argument("--multipath", action="store_true")
    parser.add_argument("--multipath-pmtu", action="store_true")
    parser.add_argument("--multipath-pmtu-blackhole", action="store_true")
    parser.add_argument("--multipath-timeout", action="store_true")
    parser.add_argument("--multipath-close", action="store_true")
    parser.add_argument("--connect-close", action="store_true")
    parser.add_argument("--pmtu-blackhole-proxy", type=Path)
    args = parser.parse_args()
    if not args.server.is_file() or not args.client.is_file():
        raise FileNotFoundError("HTTP/3 end-to-end binaries are missing")
    if args.multipath_pmtu_blackhole and (args.pmtu_blackhole_proxy is None or
                                          not args.pmtu_blackhole_proxy.is_file()):
        raise FileNotFoundError("HTTP/3 PMTU blackhole proxy is missing")

    try:
        with tempfile.TemporaryDirectory(prefix="cnetmod-http3-client-") as temp:
            directory = Path(temp)
            cert, key = make_certificate(directory)
            port = reserve_port()
            environment = os.environ.copy()
            server_command = [str(args.server), "--port", str(port), "--cert", str(cert),
                              "--key", str(key)]
            if args.dynamic_qpack:
                server_command.append("--dynamic-qpack")
            if (args.multipath or args.multipath_pmtu or args.multipath_pmtu_blackhole
                    or args.multipath_timeout or args.multipath_close):
                server_command.append("--multipath")
            if args.multipath_pmtu or args.multipath_pmtu_blackhole:
                server_command.append("--path-mtu-discovery")
            if args.multipath_pmtu_blackhole:
                server_command.extend(["--path-mtu-initial-delay-ms", "500"])
            server = subprocess.Popen(
                server_command,
                cwd=directory, env=environment, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            proxy = None
            try:
                wait_for_server(server)
                client_port = port
                dropped_marker = directory / "pmtu-probe-dropped.marker"
                arm_marker = directory / "pmtu-blackhole-armed.marker"
                if args.multipath_pmtu_blackhole:
                    proxy_port = reserve_port()
                    proxy = subprocess.Popen(
                        [sys.executable, str(args.pmtu_blackhole_proxy), "--listen",
                         str(proxy_port), "--target-port", str(port), "--maximum", "1200",
                         "--dropped-marker", str(dropped_marker), "--arm-marker",
                         str(arm_marker)],
                        cwd=directory, env=environment, text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    )
                    time.sleep(0.05)
                    if proxy.poll() is not None:
                        stdout, stderr = proxy.communicate()
                        raise RuntimeError(f"PMTU proxy exited early\nstdout:\n{stdout}\nstderr:\n{stderr}")
                try:
                    client_command = [str(args.client), str(client_port)]
                    if args.multipath:
                        client_command.append("--multipath")
                    elif args.multipath_pmtu:
                        client_command.append("--multipath-pmtu")
                    elif args.multipath_pmtu_blackhole:
                        client_command.extend(["--multipath-pmtu-blackhole",
                                               str(arm_marker), str(proxy_port)])
                    elif args.multipath_timeout:
                        client_command.append("--multipath-timeout")
                    elif args.multipath_close:
                        client_command.append("--multipath-close")
                    elif args.connect_close:
                        client_command.append("--connect-close")
                    if os.environ.get("CNETMOD_GDB"):
                        debugger = shutil.which("gdb")
                        if debugger is None:
                            raise RuntimeError("CNETMOD_GDB requires gdb")
                        client_command = [debugger, "--batch", "-ex", "run",
                                          "-ex", "thread apply all bt", "--args",
                                          *client_command]
                    completed = subprocess.run(
                        client_command, cwd=directory,
                        env=environment,
                        capture_output=os.environ.get("CNETMOD_QUIC_LIVE") is None,
                        # The executable runs a complete sequence of fresh,
                        # reused, streamed, resumed, and early-data sessions.
                        # Apple CI can take more than twice as long as Linux
                        # under variable hosted-runner load. Individual QUIC
                        # deadlines still detect stalled protocol work; this
                        # outer budget only bounds the whole process.
                        text=True, timeout=60,
                    )
                except subprocess.TimeoutExpired as error:
                    if server.poll() is None:
                        server.terminate()
                    server_stdout, server_stderr = server.communicate(timeout=5)
                    print(f"client timed out: {error}", file=sys.stderr)
                    if error.stdout:
                        print(f"client stdout before timeout:\n{error.stdout}",
                              file=sys.stderr)
                    if error.stderr:
                        print(f"client stderr before timeout:\n{error.stderr}",
                              file=sys.stderr)
                    print(f"server stdout:\n{server_stdout}\nserver stderr:\n{server_stderr}",
                          file=sys.stderr)
                    return 1
                if completed.stdout:
                    print(completed.stdout, end="")
                if completed.stderr:
                    print(completed.stderr, end="", file=sys.stderr)
                if completed.returncode:
                    if server.poll() is None:
                        server.terminate()
                    stdout, stderr = server.communicate(timeout=5)
                    print(f"client exited with status {completed.returncode}",
                          file=sys.stderr)
                    print(f"server stdout:\n{stdout}\nserver stderr:\n{stderr}",
                          file=sys.stderr)
                    return completed.returncode
                if args.multipath_pmtu_blackhole and not dropped_marker.exists():
                    print("PMTU blackhole proxy did not drop an oversized probe", file=sys.stderr)
                    return 1
                return 0
            finally:
                if proxy is not None:
                    stop_process(proxy)
                if server.poll() is None:
                    server.terminate()
                    try:
                        server.communicate(timeout=5)
                    except subprocess.TimeoutExpired:
                        server.kill()
                        server.communicate()
    except RuntimeError as error:
        if "openssl is required" in str(error):
            print(str(error), file=sys.stderr)
            return SKIP
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
