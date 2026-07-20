#!/usr/bin/env python3
import argparse
import socket
import struct
import subprocess
import sys
import threading
import time


CON = 0
NON = 1
ACK = 2
GET = 1
POST = 2
CONTENT = 69
CONTINUE = 95
BAD_OPTION = 130
METHOD_NOT_ALLOWED = 133
NOT_ACCEPTABLE = 134
REQUEST_ENTITY_INCOMPLETE = 136
PRECONDITION_FAILED = 140
OPT_IF_MATCH = 1
OPT_ETAG = 4
OPT_IF_NONE_MATCH = 5
OPT_OBSERVE = 6
OPT_URI_PATH = 11
OPT_CONTENT_FORMAT = 12
OPT_ACCEPT = 17
OPT_BLOCK2 = 23
OPT_BLOCK1 = 27
OPT_SIZE2 = 28
OPT_PROXY_URI = 35


def enc_uint(value: int) -> bytes:
    if value == 0:
        return b""
    out = bytearray()
    started = False
    for shift in (24, 16, 8, 0):
        b = (value >> shift) & 0xFF
        if b or started:
            out.append(b)
            started = True
    return bytes(out)


def dec_uint(value: bytes) -> int:
    result = 0
    for b in value:
        result = (result << 8) | b
    return result


def enc_ext(value: int):
    if value < 13:
        return value, b""
    if value < 269:
        return 13, bytes([value - 13])
    ext = value - 269
    return 14, struct.pack("!H", ext)


def encode_message(msg_type: int, code: int, mid: int, token: bytes, options, payload: bytes = b"") -> bytes:
    options = sorted(options, key=lambda item: item[0])
    out = bytearray([(1 << 6) | ((msg_type & 0x03) << 4) | len(token), code])
    out += struct.pack("!H", mid)
    out += token
    prev = 0
    for number, value in options:
        delta_nib, delta_ext = enc_ext(number - prev)
        len_nib, len_ext = enc_ext(len(value))
        out.append((delta_nib << 4) | len_nib)
        out += delta_ext
        out += len_ext
        out += value
        prev = number
    if payload:
        out.append(0xFF)
        out += payload
    return bytes(out)


def dec_ext(nibble: int, data: bytes, offset: int):
    if nibble < 13:
        return nibble, offset
    if nibble == 13:
        return 13 + data[offset], offset + 1
    if nibble == 14:
        return 269 + struct.unpack("!H", data[offset:offset + 2])[0], offset + 2
    raise ValueError("reserved option nibble")


def decode_message(data: bytes):
    if len(data) < 4:
        raise ValueError("short datagram")
    first, code = data[0], data[1]
    token_len = first & 0x0F
    msg = {
        "type": (first >> 4) & 0x03,
        "code": code,
        "mid": struct.unpack("!H", data[2:4])[0],
        "token": data[4:4 + token_len],
        "options": [],
        "payload": b"",
    }
    offset = 4 + token_len
    prev = 0
    while offset < len(data):
        if data[offset] == 0xFF:
            msg["payload"] = data[offset + 1:]
            break
        header = data[offset]
        offset += 1
        delta, offset = dec_ext(header >> 4, data, offset)
        length, offset = dec_ext(header & 0x0F, data, offset)
        number = prev + delta
        value = data[offset:offset + length]
        offset += length
        msg["options"].append((number, value))
        prev = number
    return msg


def uri_path_options(path: str):
    return [(OPT_URI_PATH, part.encode()) for part in path.strip("/").split("/") if part]


def block_value(num: int, more: bool, szx: int) -> bytes:
    return enc_uint((num << 4) | ((1 if more else 0) << 3) | (szx & 0x07))


def parse_block(value: bytes):
    raw = dec_uint(value)
    return raw >> 4, bool((raw >> 3) & 1), raw & 0x07


def option(msg, number: int):
    for n, value in msg["options"]:
        if n == number:
            return value
    return None


def request(sock, port: int, mid: int, token: bytes, code: int, path: str, options=None, payload=b""):
    opts = uri_path_options(path)
    if options:
        opts.extend(options)
    sock.sendto(encode_message(CON, code, mid, token, opts, payload), ("127.0.0.1", port))
    while True:
        data, _ = sock.recvfrom(1500)
        msg = decode_message(data)
        if msg["token"] == token:
            return msg


def python_client(cpp_server: str, port: int):
    proc = subprocess.Popen(
        [cpp_server, str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    try:
        time.sleep(0.8)
        if proc.poll() is not None:
            out = proc.stdout.read() if proc.stdout else ""
            raise RuntimeError(f"C++ CoAP server exited early: {out!r}")

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(5)

        discovery = request(sock, port, 0x1201, b"pydisc01", GET, "/.well-known/core")
        links = discovery["payload"].decode()
        if "</sensors/temp>" not in links or "</large>" not in links:
            raise RuntimeError(f"resource discovery mismatch: {links!r}")

        method_resp = request(sock, port, 0x1202, b"pymethod", POST, "/sensors/temp")
        if method_resp["code"] != METHOD_NOT_ALLOWED:
            raise RuntimeError(f"method response mismatch: {method_resp!r}")

        bad_opt_resp = request(sock, port, 0x1203, b"pybadopt", GET, "/sensors/temp", [(333, b"x")])
        if bad_opt_resp["code"] != BAD_OPTION:
            raise RuntimeError(f"bad option response mismatch: {bad_opt_resp!r}")

        accept_resp = request(sock, port, 0x1204, b"pyaccept", GET, "/sensors/temp", [(OPT_ACCEPT, enc_uint(50))])
        if accept_resp["code"] != NOT_ACCEPTABLE:
            raise RuntimeError(f"accept negotiation mismatch: {accept_resp!r}")

        if_match_bad = request(sock, port, 0x1205, b"pyifbad", GET, "/sensors/temp", [(OPT_IF_MATCH, b"wrong")])
        if if_match_bad["code"] != PRECONDITION_FAILED:
            raise RuntimeError(f"If-Match mismatch was not rejected: {if_match_bad!r}")

        if_match_ok = request(sock, port, 0x1206, b"pyifok", GET, "/sensors/temp", [(OPT_IF_MATCH, b"temp-v1")])
        if if_match_ok["code"] != CONTENT or option(if_match_ok, OPT_ETAG) != b"temp-v1":
            raise RuntimeError(f"If-Match success mismatch: {if_match_ok!r}")

        if_none_match = request(sock, port, 0x1207, b"pyifnone", GET, "/sensors/temp", [(OPT_IF_NONE_MATCH, b"")])
        if if_none_match["code"] != PRECONDITION_FAILED:
            raise RuntimeError(f"If-None-Match mismatch was not rejected: {if_none_match!r}")

        large_payload = bytearray()
        block_num = 0
        while True:
            block_resp = request(
                sock,
                port,
                0x1300 + block_num,
                b"pyblk%03d" % block_num,
                GET,
                "/large",
                [(OPT_BLOCK2, block_value(block_num, False, 4))],
            )
            large_payload += block_resp["payload"]
            b2 = option(block_resp, OPT_BLOCK2)
            if b2 is None:
                raise RuntimeError("missing Block2 option")
            num, more, _ = parse_block(b2)
            if num != block_num:
                raise RuntimeError(f"unexpected Block2 number {num}, expected {block_num}")
            if not more:
                break
            block_num += 1
        if len(large_payload) < 1500:
            raise RuntimeError(f"large payload too short: {len(large_payload)}")

        upload = b"python-block1-upload-" * 80
        block_size = 256
        upload_resp = None
        for num, off in enumerate(range(0, len(upload), block_size)):
            chunk = upload[off:off + block_size]
            more = off + block_size < len(upload)
            upload_resp = request(
                sock,
                port,
                0x1400 + num,
                b"pyupl001",
                2,
                "/upload",
                [(OPT_BLOCK1, block_value(num, more, 4))],
                chunk,
            )
            if num == 0:
                duplicate = request(
                    sock,
                    port,
                    0x1480,
                    b"pyupl001",
                    2,
                    "/upload",
                    [(OPT_BLOCK1, block_value(num, more, 4))],
                    chunk,
                )
                if duplicate["code"] != CONTINUE:
                    raise RuntimeError(f"Block1 duplicate check failed: {duplicate!r}")
        if upload_resp is None or f"upload-{len(upload)}".encode() not in upload_resp["payload"]:
            raise RuntimeError(f"Block1 upload failed: {upload_resp!r}")

        out_of_order = request(
            sock,
            port,
            0x1450,
            b"pybadblk",
            2,
            "/upload",
            [(OPT_BLOCK1, block_value(1, False, 4))],
            b"bad-order",
        )
        if out_of_order["code"] != REQUEST_ENTITY_INCOMPLETE:
            raise RuntimeError(f"Block1 order check failed: {out_of_order!r}")

        proxy_port = port + 2
        proxy_ready = threading.Event()

        def proxy_upstream():
            psock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            psock.bind(("127.0.0.1", proxy_port))
            psock.settimeout(8)
            proxy_ready.set()
            data, peer = psock.recvfrom(1500)
            req_msg = decode_message(data)
            path = "/".join(value.decode() for number, value in req_msg["options"] if number == OPT_URI_PATH)
            body = f"proxied-{path}".encode()
            psock.sendto(encode_message(
                ACK if req_msg["type"] == CON else NON,
                CONTENT,
                req_msg["mid"],
                req_msg["token"],
                [(OPT_CONTENT_FORMAT, enc_uint(0))],
                body,
            ), peer)
            psock.close()

        thread = threading.Thread(target=proxy_upstream, daemon=True)
        thread.start()
        if not proxy_ready.wait(3):
            raise RuntimeError("proxy upstream did not start")
        proxy_uri = f"coap://localhost:{proxy_port}/proxied".encode()
        proxy_resp = request(
            sock,
            port,
            0x1500,
            b"pyproxy1",
            GET,
            "/ignored",
            [(OPT_PROXY_URI, proxy_uri)],
        )
        if proxy_resp["payload"] != b"proxied-proxied":
            raise RuntimeError(f"proxy response mismatch: {proxy_resp!r}")

        token = b"pyobs001"
        req = encode_message(
            CON,
            GET,
            0x1234,
            token,
            [(OPT_OBSERVE, b"")] + uri_path_options("/sensors/temp"),
        )
        sock.sendto(req, ("127.0.0.1", port))
        start = encode_message(
            CON,
            GET,
            0x1235,
            b"pystart1",
            uri_path_options("/start-notify"),
        )
        sock.sendto(start, ("127.0.0.1", port))

        bodies = []
        observe_values = []
        deadline = time.time() + 8
        while time.time() < deadline and len(bodies) < 3:
            data, _ = sock.recvfrom(1500)
            msg = decode_message(data)
            if msg["token"] != token or msg["code"] != CONTENT:
                continue
            bodies.append(msg["payload"].decode())
            for number, value in msg["options"]:
                if number == OPT_OBSERVE:
                    observe_values.append(dec_uint(value))
            if msg["type"] == CON:
                ack = encode_message(ACK, 0, msg["mid"], b"", [])
                sock.sendto(ack, ("127.0.0.1", port))

        if "22.5" not in bodies:
            raise RuntimeError(f"missing initial response, bodies={bodies!r}")
        if not any(body.startswith("notify-") for body in bodies):
            raise RuntimeError(f"missing observe notification, bodies={bodies!r}")
        if not observe_values:
            raise RuntimeError("missing Observe option")
        print("PY_CLIENT_OK", bodies, observe_values, len(large_payload), upload_resp["payload"].decode(), proxy_resp["payload"].decode())
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=3)
        except Exception:
            proc.kill()


def python_server(cpp_client: str, port: int):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", port))
    sock.settimeout(10)

    proc = subprocess.Popen(
        [cpp_client, str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        large_body = (b"python-block2-response-" * 100)
        served_temp = False
        served_large_done = False
        served_upload_done = False
        uploads = {}
        deadline = time.time() + 10
        while time.time() < deadline and not (served_temp and served_large_done and served_upload_done):
            data, peer = sock.recvfrom(1500)
            req = decode_message(data)
            path = "/".join(value.decode() for number, value in req["options"] if number == OPT_URI_PATH)
            if req["code"] == GET and path == "sensors/temp":
                resp = encode_message(
                    ACK if req["type"] == CON else NON,
                    CONTENT,
                    req["mid"],
                    req["token"],
                    [(OPT_CONTENT_FORMAT, enc_uint(0))],
                    b"python-22.5",
                )
                served_temp = True
                sock.sendto(resp, peer)
                continue
            if req["code"] == GET and path == "large":
                b2 = option(req, OPT_BLOCK2)
                num, _, szx = parse_block(b2 or b"")
                size = 1 << (szx + 4)
                off = num * size
                chunk = large_body[off:off + size]
                more = off + size < len(large_body)
                resp = encode_message(
                    ACK if req["type"] == CON else NON,
                    CONTENT,
                    req["mid"],
                    req["token"],
                    [(OPT_CONTENT_FORMAT, enc_uint(0)), (OPT_BLOCK2, block_value(num, more, szx)), (OPT_SIZE2, enc_uint(len(large_body)))],
                    chunk,
                )
                sock.sendto(resp, peer)
                served_large_done = not more
                continue
            if req["code"] == POST and path == "upload":
                b1 = option(req, OPT_BLOCK1)
                num, more, szx = parse_block(b1 or b"")
                key = req["token"]
                uploads.setdefault(key, bytearray())
                block_size = 1 << (szx + 4)
                expected = num * block_size
                if len(uploads[key]) < expected:
                    uploads[key].extend(b"\x00" * (expected - len(uploads[key])))
                uploads[key][expected:expected + len(req["payload"])] = req["payload"]
                if more:
                    resp = encode_message(
                        ACK if req["type"] == CON else NON,
                        CONTINUE,
                        req["mid"],
                        req["token"],
                        [(OPT_BLOCK1, block_value(num, more, szx))],
                    )
                    sock.sendto(resp, peer)
                    continue
                body = f"python-upload-{len(uploads[key])}".encode()
                resp = encode_message(
                    ACK if req["type"] == CON else NON,
                    CONTENT,
                    req["mid"],
                    req["token"],
                    [(OPT_CONTENT_FORMAT, enc_uint(0)), (OPT_BLOCK1, block_value(num, more, szx))],
                    body,
                )
                served_upload_done = True
                sock.sendto(resp, peer)
                continue
            raise RuntimeError(f"unexpected path {path!r}")
        out, _ = proc.communicate(timeout=10)
        if "COAP_CLIENT_BODY python-22.5" not in out:
            raise RuntimeError(f"C++ client output mismatch: {out!r}")
        if "COAP_CLIENT_LARGE " not in out:
            raise RuntimeError(f"C++ blockwise output missing: {out!r}")
        if "COAP_CLIENT_UPLOAD python-upload-" not in out:
            raise RuntimeError(f"C++ blockwise upload output missing: {out!r}")
        print("PY_SERVER_OK", out.strip())
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=1)
        except Exception:
            proc.kill()
        sock.close()


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
