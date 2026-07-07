#!/usr/bin/env python3
import argparse
import socket
import struct
import subprocess
import sys
import threading
import time

from coap_interop import (
    CONTENT,
    GET,
    NON,
    OPT_CONTENT_FORMAT,
    OPT_URI_PATH,
    dec_uint,
    decode_message,
    enc_uint,
    encode_message,
)


GROUP = "224.0.1.187"


def encode_get(mid: int, token: bytes, path: str) -> bytes:
    return encode_message(
        NON,
        GET,
        mid,
        token,
        [(OPT_URI_PATH, part.encode()) for part in path.strip("/").split("/") if part],
    )


def option_path(msg) -> str:
    return "/" + "/".join(
        value.decode()
        for number, value in msg["options"]
        if number == OPT_URI_PATH
    )


def recv_ready(proc: subprocess.Popen, marker: str, timeout: float = 8.0) -> str:
    deadline = time.time() + timeout
    lines = []
    while time.time() < deadline:
        line = proc.stdout.readline()
        if line:
            lines.append(line.rstrip())
            if marker in line:
                return "\n".join(lines)
        elif proc.poll() is not None:
            break
    raise RuntimeError(f"process did not become ready: {lines!r}")


def python_client(exe: str, port: int):
    proc = subprocess.Popen(
        [exe, str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.settimeout(5)
    try:
        recv_ready(proc, "COAP_MULTICAST_SERVER_READY")
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, struct.pack("B", 1))
        sock.sendto(encode_get(0x5101, b"pymcast1", "/sensors/temp"), (GROUP, port))
        data, peer = sock.recvfrom(2048)
        msg = decode_message(data)
        if msg["code"] != CONTENT or msg["token"] != b"pymcast1":
            raise RuntimeError(f"unexpected multicast response: {msg!r}")
        if msg["payload"] != b"multicast-22.5":
            raise RuntimeError(f"unexpected multicast payload: {msg['payload']!r}")
        print("PY_MULTICAST_CLIENT_OK", peer, msg["payload"].decode())
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=1)
        except Exception:
            proc.kill()
        sock.close()


def python_server(exe: str, port: int):
    ready = threading.Event()
    errors = []

    def server():
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("", port))
        group = socket.inet_aton(GROUP)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, group + socket.inet_aton("0.0.0.0"))
        sock.settimeout(8)
        ready.set()
        try:
            data, peer = sock.recvfrom(2048)
            req = decode_message(data)
            if option_path(req) != "/sensors/temp":
                raise RuntimeError(f"unexpected path: {option_path(req)!r}")
            resp = encode_message(
                NON,
                CONTENT,
                0x6101,
                req["token"],
                [(OPT_CONTENT_FORMAT, enc_uint(0))],
                b"python-multicast-22.5",
            )
            sock.sendto(resp, peer)
        except Exception as exc:
            errors.append(exc)
        finally:
            sock.close()

    thread = threading.Thread(target=server, daemon=True)
    thread.start()
    if not ready.wait(3):
        raise RuntimeError("python multicast server did not start")

    proc = subprocess.Popen(
        [exe, str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    out, _ = proc.communicate(timeout=10)
    if errors:
        raise errors[0]
    if proc.returncode != 0:
        raise RuntimeError(f"C++ multicast client failed: {out!r}")
    if "COAP_MULTICAST_CLIENT_BODY" not in out or "python-multicast-22.5" not in out:
        raise RuntimeError(f"C++ multicast output mismatch: {out!r}")
    print("PY_MULTICAST_SERVER_OK", out.strip())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["python-client", "python-server"], required=True)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, required=True)
    args = parser.parse_args()
    if args.mode == "python-client":
        python_client(args.exe, args.port)
    else:
        python_server(args.exe, args.port)


if __name__ == "__main__":
    main()
