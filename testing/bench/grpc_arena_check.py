#!/usr/bin/env python3
"""Low-load independent grpcio oracle for the Arena server (no protobuf plugin)."""
import argparse
from pathlib import Path

import grpc


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target")
    parser.add_argument("--cert", type=Path)
    args = parser.parse_args()
    if args.cert:
        channel = grpc.secure_channel(
            args.target, grpc.ssl_channel_credentials(args.cert.read_bytes()),
            options=[("grpc.ssl_target_name_override", "localhost")])
    else:
        channel = grpc.insecure_channel(args.target)
    with channel:
        grpc.channel_ready_future(channel).result(timeout=10)
        call = channel.unary_unary("/benchmark.BenchmarkService/GetSum")
        vectors = [
            ("08011002", "0803"),        # Arena request: 1 + 2
            ("08071023", "082a"),        # 7 + 35, not a constant response
            ("", ""),                    # proto3 omitted defaults
            ("080108021003", "0805"),    # duplicate scalar: last wins
            ("080110021863", "0803"),    # unknown field
            ("08ffffffffffffffffff011002", "0801"),  # -1 + 2
            ("08feffffffffffffffff011001", "08ffffffffffffffffff01"),  # -2 + 1
        ]
        for request, expected in vectors:
            reply, status = call.with_call(bytes.fromhex(request), timeout=5)
            assert reply == bytes.fromhex(expected), (request, reply.hex(), expected)
            assert status.code() == grpc.StatusCode.OK
        try:
            call(b"\x08\x80", timeout=5)
        except grpc.RpcError as error:
            assert error.code() == grpc.StatusCode.INVALID_ARGUMENT, error
        else:
            raise AssertionError("malformed protobuf accepted")
        unknown = channel.unary_unary("/benchmark.BenchmarkService/Unknown")
        try:
            unknown(b"", timeout=5)
        except grpc.RpcError as error:
            assert error.code() == grpc.StatusCode.UNIMPLEMENTED, error
        else:
            raise AssertionError("unknown method accepted")
    print("PASS: 7 protobuf vectors, gRPC trailers, malformed input, unknown method")


if __name__ == "__main__":
    main()
