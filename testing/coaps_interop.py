import argparse
import concurrent.futures
import subprocess
import sys
import time
from pathlib import Path

from OpenSSL import SSL

from dtls_interop import MemoryBioDtlsServer, drive_dtls


TYPE_CON = 0
TYPE_ACK = 2
CODE_GET = 1
CODE_POST = 2
CODE_CONTENT = 69
OPT_URI_PATH = 11
OPT_CONTENT_FORMAT = 12


def encode_uint(value: int) -> bytes:
    if value == 0:
        return b""
    out = bytearray()
    while value:
        out.insert(0, value & 0xFF)
        value >>= 8
    return bytes(out)


def option_nibble(value: int) -> tuple[int, bytes]:
    if value < 13:
        return value, b""
    if value < 269:
        return 13, bytes([value - 13])
    return 14, (value - 269).to_bytes(2, "big")


def make_option(delta: int, value: bytes) -> bytes:
    delta_n, delta_ext = option_nibble(delta)
    len_n, len_ext = option_nibble(len(value))
    return bytes([(delta_n << 4) | len_n]) + delta_ext + len_ext + value


def encode_coap_request(path: str,
                        token: bytes = b"pycoaps1",
                        mid: int = 0x1234,
                        code: int = CODE_GET,
                        payload: bytes = b"") -> bytes:
    out = bytearray()
    out.append((1 << 6) | (TYPE_CON << 4) | len(token))
    out.append(code)
    out += mid.to_bytes(2, "big")
    out += token
    last = 0
    for segment in [part for part in path.split("/") if part]:
        number = OPT_URI_PATH
        out += make_option(number - last, segment.encode())
        last = number
    if payload:
        number = OPT_CONTENT_FORMAT
        out += make_option(number - last, encode_uint(0))
        out.append(0xFF)
        out += payload
    return bytes(out)


def encode_coap_response(req: dict, payload: bytes) -> bytes:
    token = req["token"]
    out = bytearray()
    out.append((1 << 6) | (TYPE_ACK << 4) | len(token))
    out.append(CODE_CONTENT)
    out += req["mid"].to_bytes(2, "big")
    out += token
    out += make_option(OPT_CONTENT_FORMAT, encode_uint(0))
    if payload:
        out.append(0xFF)
        out += payload
    return bytes(out)


def read_extended(nibble: int, data: bytes, offset: int) -> tuple[int, int]:
    if nibble < 13:
        return nibble, offset
    if nibble == 13:
        return data[offset] + 13, offset + 1
    if nibble == 14:
        return int.from_bytes(data[offset:offset + 2], "big") + 269, offset + 2
    raise ValueError("reserved option nibble")


def parse_coap(data: bytes) -> dict:
    if len(data) < 4:
        raise ValueError("short CoAP message")
    tkl = data[0] & 0x0F
    msg_type = (data[0] >> 4) & 0x03
    code = data[1]
    mid = int.from_bytes(data[2:4], "big")
    offset = 4
    token = data[offset:offset + tkl]
    offset += tkl
    options: list[tuple[int, bytes]] = []
    number = 0
    payload = b""
    while offset < len(data):
        if data[offset] == 0xFF:
            payload = data[offset + 1:]
            break
        first = data[offset]
        offset += 1
        delta, offset = read_extended(first >> 4, data, offset)
        length, offset = read_extended(first & 0x0F, data, offset)
        number += delta
        options.append((number, data[offset:offset + length]))
        offset += length
    path = "/" + "/".join(value.decode() for number, value in options if number == OPT_URI_PATH)
    return {"type": msg_type, "code": code, "mid": mid, "token": token, "path": path, "payload": payload}


def python_dtls_coap_request(port: int, path: str, code: int = CODE_GET,
                             payload: bytes = b"", token: bytes = b"pycoaps1") -> bytes:
    from OpenSSL import SSL
    import select
    import socket
    import time

    ctx = SSL.Context(SSL.DTLS_CLIENT_METHOD)
    ctx.set_verify(SSL.VERIFY_NONE, lambda *_args: True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(False)
    sock.connect(("127.0.0.1", port))
    conn = SSL.Connection(ctx, sock)
    conn.set_connect_state()
    drive_dtls(conn, sock, conn.do_handshake)
    drive_dtls(conn, sock, lambda: conn.send(encode_coap_request(
        path, token=token, code=code, payload=payload)))
    raw = drive_dtls(conn, sock, lambda: conn.recv(65536))
    conn.close()
    parsed = parse_coap(raw)
    if parsed["code"] != CODE_CONTENT:
        raise AssertionError(f"unexpected CoAP code: {parsed}")
    return parsed["payload"]


def python_dtls_coap_get(port: int, path: str) -> bytes:
    return python_dtls_coap_request(port, path)


def run_python_client(args: argparse.Namespace) -> None:
    proc = subprocess.Popen(
        [str(args.exe), str(args.cert), str(args.key), str(args.port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        cwd=args.cwd,
    )
    try:
        time.sleep(0.35)
        payload = None
        upload_payload = None
        parallel_payloads: list[bytes] = []
        last_error = None
        for _ in range(50):
            try:
                payload = python_dtls_coap_get(args.port, "/secure")
                upload_payload = python_dtls_coap_request(
                    args.port,
                    "/secure/upload",
                    code=CODE_POST,
                    payload=b"0123456789abcdef",
                    token=b"pycoapsp")
                if args.parallel > 1:
                    with concurrent.futures.ThreadPoolExecutor(max_workers=args.parallel) as pool:
                        futures = [
                            pool.submit(python_dtls_coap_get, args.port, "/secure")
                            for _ in range(args.parallel)
                        ]
                        parallel_payloads = [future.result(timeout=10) for future in futures]
                break
            except Exception as exc:
                last_error = exc
                time.sleep(0.1)
        if payload is None:
            raise TimeoutError(f"CoAPS client could not connect: {last_error}")
        proc.kill()
        output = proc.communicate(timeout=5)[0].splitlines()
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)
    if payload != b"coaps-secure-22.5":
        raise AssertionError(f"unexpected payload: {payload!r}")
    if upload_payload != b"coaps-upload-16":
        raise AssertionError(f"unexpected upload payload: {upload_payload!r}")
    if any(item != b"coaps-secure-22.5" for item in parallel_payloads):
        raise AssertionError(f"unexpected parallel payloads: {parallel_payloads!r}")
    print("PY_COAPS_CLIENT_OK", payload.decode(), upload_payload.decode(),
          f"parallel={len(parallel_payloads)}", *output)


def run_python_server(args: argparse.Namespace) -> None:
    server = MemoryBioDtlsServer(args.port, args.cert, args.key)
    print("PY_COAPS_SERVER_READY", args.port, flush=True)
    proc = subprocess.Popen(
        [str(args.exe), str(args.port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        cwd=args.cwd,
    )
    try:
        server.accept_first_peer()
        server.drive(server.conn.do_handshake)
        request = parse_coap(server.drive(lambda: server.conn.recv(65536)))
        response = encode_coap_response(request, b"coaps-python-22.5")
        server.drive(lambda: server.conn.send(response))
        server.flush()
        output = proc.communicate(timeout=10)[0].splitlines()
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(timeout=5)
    if request["path"] != "/secure":
        raise AssertionError(f"unexpected request: {request!r}")
    if not any(line == "COAPS_CLIENT_BODY coaps-python-22.5" for line in output):
        raise AssertionError(f"C++ client did not report expected body: {output!r}")
    print("PY_COAPS_SERVER_OK", request["path"], *output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["python-client", "python-server"], required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--cert", type=Path, default=Path("examples/test_ssl/cert.pem"))
    parser.add_argument("--key", type=Path, default=Path("examples/test_ssl/key.pem"))
    parser.add_argument("--cwd", type=Path, default=Path.cwd())
    parser.add_argument("--parallel", type=int, default=1)
    args = parser.parse_args()
    if args.mode == "python-client":
        run_python_client(args)
    else:
        run_python_server(args)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"COAPS_INTEROP_ERROR {type(exc).__name__}: {exc}", file=sys.stderr)
        raise
