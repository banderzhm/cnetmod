#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import pathlib
import socket
import subprocess
import sys
import tempfile
from concurrent import futures


def has_module(name: str) -> bool:
    return importlib.util.find_spec(name) is not None


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: grpcio_server_to_cnetmod_client_interop.py <grpc_interop_client.exe> <echo.proto>", file=sys.stderr)
        return 2

    if not has_module("grpc") or not has_module("grpc_tools"):
        print("[  SKIPPED ] grpcio reverse interop requires: pip install grpcio grpcio-tools")
        return 0

    import grpc
    from grpc_tools import protoc

    client_exe = pathlib.Path(sys.argv[1])
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

        class EchoService(echo_pb2_grpc.EchoServiceServicer):
            def Say(self, request, context):
                return echo_pb2.EchoReply(message=f"hello {request.name}", sequence=request.sequence)

            def Chat(self, request_iterator, context):
                for request in request_iterator:
                    yield echo_pb2.EchoReply(message=f"hello {request.name}", sequence=request.sequence)

        server = grpc.server(futures.ThreadPoolExecutor(max_workers=4))
        echo_pb2_grpc.add_EchoServiceServicer_to_server(EchoService(), server)
        server.add_insecure_port(f"127.0.0.1:{port}")
        server.start()
        try:
            result = subprocess.run(
                [str(client_exe), str(port)],
                text=True,
                capture_output=True,
                timeout=15,
                check=False,
            )
            if result.returncode != 0:
                raise AssertionError(
                    f"cnetmod grpc client failed: rc={result.returncode}\n"
                    f"stdout={result.stdout}\nstderr={result.stderr}"
                )
            print("[  PYTHON  ] grpcio server -> cnetmod client interop passed")
        finally:
            server.stop(grace=0).wait(timeout=5)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
