#!/usr/bin/env python3
import pathlib
import subprocess
import sys


def enc_varint(value: int) -> bytes:
    out = bytearray()
    while value >= 0x80:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def field_key(number: int, wire_type: int) -> bytes:
    return enc_varint((number << 3) | wire_type)


def enc_string(number: int, value: str) -> bytes:
    raw = value.encode("utf-8")
    return field_key(number, 2) + enc_varint(len(raw)) + raw


def enc_uint64(number: int, value: int) -> bytes:
    return field_key(number, 0) + enc_varint(value)


def enc_bool(number: int, value: bool) -> bytes:
    return field_key(number, 0) + enc_varint(1 if value else 0)


def decode_fields(payload: bytes):
    pos = 0
    fields = []

    def read_varint() -> int:
        nonlocal pos
        value = 0
        shift = 0
        while pos < len(payload):
            b = payload[pos]
            pos += 1
            value |= (b & 0x7F) << shift
            if not (b & 0x80):
                return value
            shift += 7
        raise AssertionError("truncated varint")

    while pos < len(payload):
        key = read_varint()
        number = key >> 3
        wire_type = key & 7
        if wire_type == 0:
            fields.append((number, wire_type, read_varint()))
        elif wire_type == 2:
            n = read_varint()
            data = payload[pos : pos + n]
            pos += n
            fields.append((number, wire_type, data))
        else:
            raise AssertionError(f"unsupported wire type {wire_type}")
    return fields


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: grpc_proto_interop.py <test_grpc_proto.exe> <echo.proto>", file=sys.stderr)
        return 2

    test_exe = pathlib.Path(sys.argv[1])
    proto_file = pathlib.Path(sys.argv[2])
    result = subprocess.run([str(test_exe)], text=True, capture_output=True, check=False)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode != 0:
        return result.returncode

    proto_text = proto_file.read_text(encoding="utf-8")
    required_tokens = [
        "message EchoRequest",
        "message EchoReply",
        "service EchoService",
        "rpc Say(EchoRequest) returns (EchoReply)",
        "rpc Chat(stream EchoRequest) returns (stream EchoReply)",
    ]
    missing = [token for token in required_tokens if token not in proto_text]
    if missing:
        raise AssertionError(f"proto contract missing tokens: {missing}")

    payload = enc_string(1, "Ada") + enc_uint64(2, 150) + enc_bool(3, True)
    expected_hex = "0a034164611096011801"
    if payload.hex() != expected_hex:
        raise AssertionError(f"python oracle mismatch: {payload.hex()} != {expected_hex}")

    fields = decode_fields(payload)
    if fields != [(1, 2, b"Ada"), (2, 0, 150), (3, 0, 1)]:
        raise AssertionError(f"decoded fields mismatch: {fields!r}")

    package = "cnetmod.testing.grpc"
    if f"/{package}.EchoService/Say" != "/cnetmod.testing.grpc.EchoService/Say":
        raise AssertionError("python gRPC path oracle failed")

    print("[  PYTHON  ] grpc proto oracle passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
