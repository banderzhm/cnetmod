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

    required = ("grpc", "grpc_tools", "grpc_health", "grpc_reflection")
    if any(not has_module(name) for name in required):
        print("[  SKIPPED ] grpcio interop requires: pip install -r testing/grpc/requirements.txt")
        return 77

    import grpc
    from grpc_tools import protoc
    from grpc_health.v1 import health_pb2
    from grpc_health.v1 import health_pb2_grpc
    from grpc_reflection.v1alpha import reflection_pb2
    from grpc_reflection.v1alpha import reflection_pb2_grpc

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
            compressed = stub.Say(
                echo_pb2.EchoRequest(name="gzip", sequence=9),
                timeout=5,
                compression=grpc.Compression.Gzip,
            )
            assert compressed.message == "hello gzip", compressed
            assert compressed.sequence == 9, compressed

            health = health_pb2_grpc.HealthStub(channel)
            health_reply = health.Check(
                health_pb2.HealthCheckRequest(
                    service="cnetmod.testing.grpc.EchoService"
                ),
                timeout=5,
            )
            assert health_reply.status == health_pb2.HealthCheckResponse.SERVING

            reflection = reflection_pb2_grpc.ServerReflectionStub(channel)
            reflection_reply = next(
                reflection.ServerReflectionInfo(
                    iter([reflection_pb2.ServerReflectionRequest(list_services="")]),
                    timeout=5,
                )
            )
            services = {item.name for item in reflection_reply.list_services_response.service}
            assert "cnetmod.testing.grpc.EchoService" in services, services
            assert "grpc.health.v1.Health" in services, services
            # A real grpcio client must receive a standards-compliant status
            # for an unregistered RPC rather than an HTTP-layer failure.
            unknown = channel.unary_unary(
                "/cnetmod.testing.grpc.EchoService/Unknown",
                request_serializer=lambda value: value,
                response_deserializer=lambda value: value,
            )
            try:
                unknown(b"", timeout=5)
                raise AssertionError("unregistered RPC unexpectedly succeeded")
            except grpc.RpcError as error:
                assert error.code() == grpc.StatusCode.UNIMPLEMENTED, error
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
