#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import pathlib
import socket
import subprocess
import sys
import tempfile
import time


def has_module(name: str) -> bool:
    return importlib.util.find_spec(name) is not None


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def wait_ready(proc: subprocess.Popen[str], timeout: float = 10.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = proc.stdout.readline() if proc.stdout is not None else ""
        if line.startswith("READY "):
            return
        if proc.poll() is not None:
            stderr = proc.stderr.read() if proc.stderr is not None else ""
            raise RuntimeError(f"interop server exited early: {proc.returncode}\n{stderr}")
    raise TimeoutError("interop server did not become ready")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: grpcio_cross_process_interop.py <grpc_interop_server.exe> <echo.proto>", file=sys.stderr)
        return 2

    if not has_module("grpc") or not has_module("grpc_tools"):
        print("[  SKIPPED ] grpcio interop requires: pip install grpcio grpcio-tools")
        return 0

    import grpc
    from grpc_tools import protoc

    server_exe = pathlib.Path(sys.argv[1])
    proto_file = pathlib.Path(sys.argv[2])
    port = free_port()

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = pathlib.Path(tmpdir)
        rc = protoc.main(
            [
                "grpc_tools.protoc",
                f"-I{proto_file.parent}",
                f"--python_out={tmp}",
                f"--grpc_python_out={tmp}",
                str(proto_file),
            ]
        )
        if rc != 0:
            raise RuntimeError(f"protoc failed: {rc}")

        sys.path.insert(0, str(tmp))
        import echo_pb2
        import echo_pb2_grpc

        proc = subprocess.Popen(
            [str(server_exe), str(port)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            wait_ready(proc)
            channel = grpc.insecure_channel(f"127.0.0.1:{port}")
            grpc.channel_ready_future(channel).result(timeout=10)
            stub = echo_pb2_grpc.EchoServiceStub(channel)
            resp = stub.Say(echo_pb2.EchoRequest(name="python-grpcio", sequence=7, urgent=True), timeout=5)
            assert resp.message == "hello python-grpcio", resp
            assert resp.sequence == 7, resp
            print("[  PYTHON  ] grpcio cross-process interop passed")
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
