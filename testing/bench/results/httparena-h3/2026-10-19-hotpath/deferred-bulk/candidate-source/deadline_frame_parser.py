from aioquic.buffer import Buffer
import hashlib

def frames(payload):
    """Authenticate first, then parse actual frame identity; no size guesses."""
    data = Buffer(data=payload)
    result = {"streams": [], "datagrams": [], "kinds": [], "acks": []}
    while data.tell() < len(payload):
        kind = data.pull_uint_var()
        result["kinds"].append(kind)
        if kind in (0, 1, 0x1E):
            continue
        if kind in (2, 3):
            largest = data.pull_uint_var()
            data.pull_uint_var()
            count = data.pull_uint_var()
            first = data.pull_uint_var()
            low = largest - first
            assert low >= 0
            result["acks"].append([low, largest])
            for _ in range(count):
                gap, length = data.pull_uint_var(), data.pull_uint_var()
                largest = low - gap - 2
                low = largest - length
                assert low >= 0
                result["acks"].append([low, largest])
            if kind == 3:
                for _ in range(3):
                    data.pull_uint_var()
        elif 8 <= kind <= 15:
            stream = data.pull_uint_var()
            offset = data.pull_uint_var() if kind & 4 else 0
            length = data.pull_uint_var() if kind & 2 else len(payload) - data.tell()
            body = data.pull_bytes(length)
            result["streams"].append({"stream": stream, "offset": offset,
                "length": length, "fin": bool(kind & 1),
                "sha256": hashlib.sha256(body).hexdigest()})
        elif kind in (0x30, 0x31):
            length = data.pull_uint_var() if kind & 1 else len(payload) - data.tell()
            body = data.pull_bytes(length)
            if body.startswith(b"DGCC"):
                sequence = int.from_bytes(body[4:8], "big")
                assert 0 <= sequence < 64 and body == b"DGCC" + sequence.to_bytes(4, "big") + b"x" * 1092
                result["datagrams"].append({"kind": "burst", "id": sequence, "length": length})
            else:
                assert body == b"DGCMfresh", repr(body)
                result["datagrams"].append({"kind": "marker", "length": length})
        elif kind == 4:
            for _ in range(3):
                data.pull_uint_var()
        elif kind in (5, 0x11, 0x15):
            data.pull_uint_var()
            data.pull_uint_var()
        elif kind == 6:
            data.pull_uint_var()
            data.pull_bytes(data.pull_uint_var())
        elif kind == 7:
            data.pull_bytes(data.pull_uint_var())
        elif kind in (0x10, 0x12, 0x13, 0x14, 0x16, 0x17, 0x19):
            data.pull_uint_var()
        elif kind == 0x18:
            data.pull_uint_var()
            data.pull_uint_var()
            data.pull_bytes(data.pull_uint8())
            data.pull_bytes(16)
        elif kind in (0x1A, 0x1B):
            data.pull_bytes(8)
        elif kind in (0x1C, 0x1D):
            data.pull_uint_var()
            if kind == 0x1C:
                data.pull_uint_var()
            data.pull_bytes(data.pull_uint_var())
        else:
            raise AssertionError(f"unhandled frame {kind:#x}")
    return result
