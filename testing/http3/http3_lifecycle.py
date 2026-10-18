"""Low-load QUIC RESET, QPACK cancellation and buffered request regressions."""

import asyncio
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile

try:
    import pylsqpack
    from aioquic.asyncio import QuicConnectionProtocol, connect
    from aioquic.h3.connection import H3Connection, H3_ALPN, encode_frame
    from aioquic.h3.events import DataReceived, HeadersReceived
    from aioquic.quic.configuration import QuicConfiguration
    from aioquic.quic.events import StreamReset, ConnectionTerminated
except ImportError:
    raise SystemExit(77)

from http_client_e2e import make_certificate, reserve_port, stop_process


class Probe(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.h3 = H3Connection(self._quic)
        self.responses = {}
        self.resets = {}
        self.terminated = None
        self.reset_frames = []
        original_write_reset = self._quic._write_reset_stream_frame
        def write_reset(builder, stream):
            self.reset_frames.append(stream.sender._stream_id)
            return original_write_reset(builder, stream)
        self._quic._write_reset_stream_frame = write_reset

    def quic_event_received(self, event):
        if isinstance(event, ConnectionTerminated):
            self.terminated = event
        if isinstance(event, StreamReset):
            future = self.resets.get(event.stream_id)
            if future is not None and not future.done():
                future.set_result(event.error_code)
        for item in self.h3.handle_event(event):
            response = self.responses.get(getattr(item, "stream_id", None))
            if response is None:
                continue
            if isinstance(item, HeadersReceived):
                response["status"] = dict(item.headers).get(b":status")
            elif isinstance(item, DataReceived):
                response["body"].extend(item.data)
            if getattr(item, "stream_ended", False) and not response["done"].done():
                response["done"].set_result(None)

    def headers(self, port, path=b"/health"):
        return [(b":method", b"GET"), (b":scheme", b"https"),
                (b":authority", f"localhost:{port}".encode()), (b":path", path)]

    async def get(self, port, path=b"/health"):
        stream = self._quic.get_next_available_stream_id()
        response = {"status": None, "body": bytearray(),
                    "done": asyncio.get_running_loop().create_future()}
        self.responses[stream] = response
        self.h3.send_headers(stream, self.headers(port, path), end_stream=True)
        self.transmit()
        await asyncio.wait_for(response["done"], 5)
        assert response["status"] == b"200", response
        assert self.terminated is None, self.terminated
        self.responses.pop(stream)
        return bytes(response["body"])

    def expect_reset(self, stream):
        future = asyncio.get_running_loop().create_future()
        self.resets[stream] = future
        return future


def configuration():
    return QuicConfiguration(is_client=True, alpn_protocols=H3_ALPN,
                             verify_mode=ssl.CERT_NONE, idle_timeout=10)


async def check(port):
    async with connect("127.0.0.1", port, configuration=configuration(),
                       create_protocol=Probe) as client:
        assert await client.get(port) == b"ok\n"
        # More resets than the initial MAX_STREAMS grant must replenish credit.
        for index in range(120):
            stream = client._quic.get_next_available_stream_id()
            reset = client.expect_reset(stream)
            client._quic.reset_stream(stream, 0x10C)
            client.transmit()
            await asyncio.wait_for(reset, 5)
            if index % 30 == 29:
                assert await client.get(port) == b"ok\n"
        print("PASS RESET-before-HEADERS churn and connection reuse")

        # RESET's final size consumes connection credit even without STREAM
        # data. Discarding that receive direction must return the same credit.
        initial_window = client._quic._remote_max_data
        stream_window = client._quic._remote_max_stream_data_bidi_remote
        chunk = min(stream_window, max(1, initial_window // 8))
        assert initial_window > 0 and chunk > 0
        reset_bytes = 0
        while reset_bytes <= initial_window + chunk:
            for _ in range(100):
                if client._quic._remote_max_data - client._quic._remote_max_data_used >= chunk:
                    break
                client.transmit()
                await asyncio.sleep(0.01)
            else:
                raise AssertionError("RESET did not replenish connection credit")
            stream = client._quic.get_next_available_stream_id()
            reset = client.expect_reset(stream)
            client._quic.reset_stream(stream, 0x10C)
            sender = client._quic._streams[stream].sender
            sender.highest_offset = chunk
            client._quic._remote_max_data_used += chunk
            reset_bytes += chunk
            client.transmit()
            assert stream in client.reset_frames, "nonzero RESET not encoded"
            await asyncio.wait_for(reset, 5)
        assert client._quic._remote_max_data > initial_window
        assert await client.get(port) == b"ok\n"
        print(f"PASS RESET final-size credit: initial={initial_window}, "
              f"reset_bytes={reset_bytes}, advertised={client._quic._remote_max_data}")

        # A complete request with too much DATA must reset only its own stream.
        stream = client._quic.get_next_available_stream_id()
        reset = client.expect_reset(stream)
        client.h3.send_headers(stream, client.headers(port), end_stream=False)
        client.h3.send_data(stream, b"x" * 2048, end_stream=True)
        client.transmit()
        await asyncio.wait_for(reset, 5)
        assert await client.get(port) == b"ok\n"

        # No FIN: the buffered wire bound must stop continued credit replenishment.
        stream = client._quic.get_next_available_stream_id()
        reset = client.expect_reset(stream)
        client.h3.send_headers(stream, client.headers(port), end_stream=False)
        client.h3.send_data(stream, b"x" * 70000, end_stream=False)
        client.transmit()
        await asyncio.wait_for(reset, 5)
        assert await client.get(port) == b"ok\n"
        print("PASS bounded buffered requests with and without FIN")

        async def blocked_request(peer):
            stream = peer._quic.get_next_available_stream_id()
            encoder = peer.h3._encoder
            headers = peer.headers(port) + [(f"x-lifecycle-{stream}".encode(), b"dynamic")]
            encoder.encode(stream + 4000, headers)
            instructions, block = encoder.encode(stream, headers)
            assert instructions and block[0] != 0, "fixture did not create a QPACK dependency"
            peer._quic.send_stream_data(stream, encode_frame(1, block), end_stream=True)
            peer.transmit()
            await asyncio.sleep(0.03)
            return stream, instructions

        for mode in ("RESET", "STOP_SENDING"):
            for _ in range(4):
                stream, instructions = await blocked_request(client)
                reset = client.expect_reset(stream)
                if mode == "RESET":
                    # aioquic suppresses its reset_stream API after FIN ACK.
                    # Force the RFC-valid late RESET with the same final size,
                    # and require evidence that the frame reached the encoder.
                    sender = client._quic._streams[stream].sender
                    sender._reset_error_code = 0x10C
                    sender.reset_pending = True
                else:
                    client._quic.stop_stream(stream, 0x10C)
                client.transmit()
                if mode == "RESET":
                    assert stream in client.reset_frames, "late RESET was not encoded"
                await asyncio.wait_for(reset, 5)
                client._quic.send_stream_data(client.h3._local_encoder_stream_id, instructions)
                client.transmit()
                assert await client.get(port) == b"ok\n"
            print(f"PASS {mode} cancels QPACK waits and preserves connection reuse")

        # Repeated control frames on one long-lived stream must be consumed.
        for _ in range(100):
            client._quic.send_stream_data(client.h3._local_control_stream_id,
                                         encode_frame(0x21, b"x" * 1024))
            client.transmit()
            await asyncio.sleep(0.001)
        assert await client.get(port) == b"ok\n"
        print("PASS incremental control stream above incomplete-frame bound")

        # A closed peer must release a session whose child waits for QPACK inserts.
        async with connect("127.0.0.1", port, configuration=configuration(),
                           create_protocol=Probe) as abandoned:
            await abandoned.get(port)
            await blocked_request(abandoned)
        for _ in range(20):
            if int(await client.get(port, b"/active-connections")) == 1:
                break
            await asyncio.sleep(0.1)
        else:
            raise AssertionError("QPACK-blocked closed peer was retained")
        print("PASS connection close releases QPACK-blocked session")


async def reject_invalid_resets(port):
    cases = [("invalid-direction", 3, 0, 0x5),
             ("unopened-local-bidi", 1, 0, 0x5),
             ("over-MAX_STREAMS", 4000000, 0, 0x4),
             ("over-stream-credit", None, 1 << 40, 0x3)]
    for name, invalid_id, final_size, expected_error in cases:
        async with connect("127.0.0.1", port, configuration=configuration(),
                           create_protocol=Probe) as peer:
            assert await peer.get(port) == b"ok\n"
            stream = peer._quic.get_next_available_stream_id()
            peer._quic.reset_stream(stream, 0x10C)
            sender = peer._quic._streams[stream].sender
            original = sender.get_reset_frame
            encoded = []
            def malformed():
                frame = original()
                frame.stream_id = stream if invalid_id is None else invalid_id
                frame.final_size = final_size
                encoded.append((frame.stream_id, frame.final_size))
                return frame
            sender.get_reset_frame = malformed
            peer.transmit()
            for _ in range(100):
                if peer.terminated is not None:
                    break
                await asyncio.sleep(0.02)
            assert encoded, (name, "RESET not encoded")
            assert peer.terminated is not None, (name, "no transport close")
            assert peer.terminated.reason_phrase != "Idle timeout", (name, peer.terminated)
            assert peer.terminated.error_code == expected_error, (name, peer.terminated)
            assert peer.terminated.frame_type == 0x4, (name, peer.terminated)
            print("PASS explicit transport CLOSE", name, peer.terminated)
    async with connect("127.0.0.1", port, configuration=configuration(),
                       create_protocol=Probe) as peer:
        assert await peer.get(port) == b"ok\n"
    print("PASS listener survives rejected RESET connections")


async def critical_stream_fin(port):
    for kind in ("control", "encoder", "decoder"):
        async with connect("127.0.0.1", port, configuration=configuration(),
                           create_protocol=Probe) as peer:
            assert await peer.get(port) == b"ok\n"
            stream = getattr(peer.h3, f"_local_{kind}_stream_id")
            peer._quic.send_stream_data(stream, b"", end_stream=True)
            peer.transmit()
            for _ in range(100):
                if peer.terminated is not None:
                    break
                await asyncio.sleep(0.02)
            assert peer.terminated is not None, (kind, "no application close")
            assert peer.terminated.error_code == 0x104, (kind, peer.terminated)
            assert peer.terminated.frame_type is None, (kind, peer.terminated)
            print("PASS critical stream FIN application CLOSE", kind)


async def max_stream_credit_window(port):
    async with connect("127.0.0.1", port, configuration=configuration(),
                       create_protocol=Probe) as peer:
        assert await peer.get(port) == b"ok\n"
        initial = peer._quic._remote_max_streams_bidi
        available = initial - peer._quic.get_next_available_stream_id() // 4
        assert available > 0
        streams = []
        resets = []
        for _ in range(available):
            stream = peer._quic.get_next_available_stream_id()
            resets.append(peer.expect_reset(stream))
            streams.append(stream)
            peer.h3.send_headers(stream, peer.headers(port), end_stream=False)
        peer.transmit()
        await asyncio.sleep(0.03)
        peer._quic.reset_stream(streams[-1], 0x10C)
        peer.transmit()
        await asyncio.wait_for(resets[-1], 5)
        await asyncio.sleep(0.03)
        after_high = peer._quic._remote_max_streams_bidi
        # One actual retirement can return at most one stream's credit.
        assert after_high <= initial + 1, (initial, after_high, streams[-1])
        for stream in streams[:-1]:
            peer._quic.reset_stream(stream, 0x10C)
        peer.transmit()
        await asyncio.wait_for(asyncio.gather(*resets[:-1]), 5)
        for _ in range(100):
            if peer._quic._remote_max_streams_bidi > initial:
                break
            await asyncio.sleep(0.01)
        assert await peer.get(port) == b"ok\n"
        print(f"PASS MAX_STREAMS high-ID retirement: initial={initial}, "
              f"after_high={after_high}, after_cleanup={peer._quic._remote_max_streams_bidi}")


async def run(port):
    await check(port)
    await max_stream_credit_window(port)
    await reject_invalid_resets(port)
    await critical_stream_fin(port)


def main():
    server_binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as directory:
        cert, key = make_certificate(Path(directory))
        port = reserve_port()
        with tempfile.TemporaryFile(mode="w+") as log:
            server = subprocess.Popen([str(server_binary), "--port", str(port),
                "--cert", str(cert), "--key", str(key), "--lifecycle-test"],
                cwd=directory, text=True, stdout=log, stderr=log)
            try:
                asyncio.run(run(port))
            except BaseException:
                log.seek(0)
                print(log.read(), file=sys.stderr)
                raise
            finally:
                stop_process(server)


if __name__ == "__main__":
    main()
