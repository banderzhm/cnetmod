"""Public QUIC initial credit, empty FIN and cancelled receive regression."""
from __future__ import annotations

import argparse
import asyncio
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import time
import types

try:
    from aioquic.asyncio import QuicConnectionProtocol, connect
    from aioquic.quic.configuration import QuicConfiguration
    from aioquic.quic.events import ConnectionTerminated, StopSendingReceived, StreamDataReceived, StreamReset
    from aioquic.quic.packet_builder import QuicDeliveryState
    from aioquic.quic.connection import CONNECTION_LIMIT_FRAME_CAPACITY, MAX_STREAM_DATA_FRAME_CAPACITY
except ImportError:
    raise SystemExit(77)

from http_client_e2e import make_certificate, reserve_port, stop_process


async def run(binary: Path, scenario: str, directory: Path) -> None:
    certificate, key = make_certificate(directory)
    port = reserve_port()
    listening, result = directory / "listening", directory / "result"
    failure = directory / "result.failed"
    process = subprocess.Popen(
        [str(binary), "--port", str(port), "--scenario", scenario,
         "--cert", str(certificate), "--key", str(key), "--listening-report", str(listening),
         "--result-report", str(result)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    bodies, finished, resets, stops, terminated, crafted = {}, set(), set(), set(), [], []
    succeeded = False

    async def until(predicate):
        deadline = time.monotonic() + 12
        while not predicate():
            assert not failure.exists(), failure.read_text(errors="replace")
            assert scenario == "close-count-zero" or not terminated, terminated
            assert process.poll() is None, "fixture exited unexpectedly"
            if result.exists():
                assert result.read_text().strip() == "passed", result.read_text()
            assert time.monotonic() < deadline, "initial credit peer stage timed out"
            await asyncio.sleep(0.01)

    def limits(quic, builder, space):
        # The peer deliberately delays its response to STOP so valid data sent
        # before that STOP was observed can still arrive at the cancelled side.
        while crafted:
            kind, sid, offset, data = crafted[0]
            frame = 0x0e if kind == "stream" else 0x04
            def delivered(state, item=(kind, sid, offset, data)):
                if state != QuicDeliveryState.ACKED:
                    crafted.append(item)
            buf = builder.start_frame(frame, capacity=64 + len(data), handler=delivered)
            buf.push_uint_var(sid)
            if kind == "stream":
                buf.push_uint_var(offset)
                buf.push_uint_var(len(data))
                buf.push_bytes(data)
            else:
                buf.push_uint_var(31)
                buf.push_uint_var(offset)
            crafted.pop(0)
        for limit in (quic._local_max_data, quic._local_max_streams_bidi, quic._local_max_streams_uni):
            if limit.value != limit.sent:
                buf = builder.start_frame(limit.frame_type, capacity=CONNECTION_LIMIT_FRAME_CAPACITY,
                                          handler=quic._on_connection_limit_delivery, handler_args=(limit,))
                buf.push_uint_var(limit.value)
                limit.sent = limit.value

    def stream_limit(quic, builder, space, stream):
        if stream.max_stream_data_local != stream.max_stream_data_local_sent:
            buf = builder.start_frame(0x11, capacity=MAX_STREAM_DATA_FRAME_CAPACITY,
                                      handler=quic._on_max_stream_data_delivery, handler_args=(stream,))
            buf.push_uint_var(stream.stream_id)
            buf.push_uint_var(stream.max_stream_data_local)
            stream.max_stream_data_local_sent = stream.max_stream_data_local

    def delay_stop(quic, context, frame_type, buf):
        sid, error = buf.pull_uint_var(), buf.pull_uint_var()
        quic._assert_stream_can_send(frame_type, sid)
        quic._get_or_create_stream(frame_type, sid)
        stops.add(sid)

    class Peer(QuicConnectionProtocol):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            quic = self._quic
            quic._local_max_stream_data_bidi_remote = 17
            quic._local_max_stream_data_bidi_local = 41
            quic._local_max_stream_data_uni = 29
            quic._local_max_streams_bidi.value = quic._local_max_streams_bidi.sent = (0 if scenario == "close-count-zero" else 3)
            quic._local_max_streams_uni.value = quic._local_max_streams_uni.sent = 1
            if scenario == "close-count-zero":
                quic._local_max_streams_uni.value = quic._local_max_streams_uni.sent = 0
            quic._write_connection_limits = types.MethodType(limits, quic)
            quic._write_stream_limits = types.MethodType(stream_limit, quic)
            if scenario != "credit":
                _, epochs = quic._QuicConnection__frame_handlers[0x05]
                quic._QuicConnection__frame_handlers[0x05] = (types.MethodType(delay_stop, quic), epochs)

        def quic_event_received(self, event):
            if isinstance(event, StreamDataReceived):
                bodies.setdefault(event.stream_id, bytearray()).extend(event.data)
                if event.end_stream:
                    finished.add(event.stream_id)
            elif isinstance(event, StreamReset):
                resets.add(event.stream_id)
            elif isinstance(event, StopSendingReceived):
                stops.add(event.stream_id)
            elif isinstance(event, ConnectionTerminated):
                terminated.append(repr(event))
            else:
                super().quic_event_received(event)

    try:
        await until(listening.exists)
        configuration = QuicConfiguration(is_client=True, alpn_protocols=["quic-initial-credit"],
                                          max_data=1024, max_stream_data=17)
        configuration.verify_mode = ssl.CERT_NONE
        async with connect("127.0.0.1", port, configuration=configuration, create_protocol=Peer) as peer:
            if scenario == "credit":
                peer._quic.send_stream_data(0, b"first", end_stream=True)
                peer.transmit()
                await until(lambda: {1, 5} <= finished and len(bodies.get(9, [])) == 17 and
                            len(bodies.get(3, [])) == 29 and len(bodies.get(0, [])) == 41)
                assert not ({0, 3, 9} & finished), "send exceeded directional TP before MAX_STREAM_DATA"
                for sid, maximum in ((9, 18), (3, 30), (0, 42)):
                    peer._quic._streams[sid].max_stream_data_local = maximum
                peer.transmit()
                await until(lambda: {0, 1, 3, 5, 9} <= finished)
                assert bytes(bodies[9]) == b"p" * 18 and bytes(bodies[3]) == b"p" * 30
                assert bytes(bodies[0]) == b"r" * 42
            elif scenario == "cancel-final":
                await until(lambda: resets == stops == {1, 5})
                assert not bodies and not finished
                crafted.append(("stream", 1, 8, b"abc"))
                peer._quic.send_stream_data(0, b"first", end_stream=True)
                peer.transmit()
                await until(lambda: 0 in finished)
                assert bytes(bodies[0]) == b"rr"
                crafted.extend([("stream", 1, 12, b"de"), ("reset", 1, 19, b""),
                                ("reset", 5, 0, b"")])
            else:
                peer._quic.send_stream_data(0, b"first", end_stream=True)
                peer.transmit()
                await until(result.exists)
                assert result.read_text().strip() == "passed"
                assert not resets and not stops and not bodies and not finished
            if scenario != "close-count-zero":
                peer._quic.send_stream_data(4, b"finish", end_stream=True)
                peer.transmit()
                await until(lambda: 4 in finished and result.exists())
                assert result.read_text().strip() == "passed" and bytes(bodies[4]) == b"rr"
        succeeded = True
    finally:
        stdout, stderr = stop_process(process)
        (directory / "stdout.log").write_text(stdout, encoding="utf-8")
        (directory / "stderr.log").write_text(stderr, encoding="utf-8")
        if not succeeded:
            # Preserve evidence in CTest output even when its temporary folder
            # is removed; do not replace the original assertion/peer exception.
            report = result.read_text(errors="replace") if result.exists() else "<missing>"
            print(f"initial-credit scenario={scenario} result={report}\nstdout:\n{stdout}\nstderr:\n{stderr}", file=sys.stderr)
        else:
            assert not failure.exists(), failure.read_text(errors="replace")
            assert "initial-credit fixture:" not in stdout + stderr, stdout + stderr
            assert "CREDIT_RECORD preTP_pending" in stdout + stderr, stdout + stderr


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("server", type=Path)
    parser.add_argument("--scenario", choices=["credit", "cancel-final", "close-count-zero"], required=True)
    parser.add_argument("--output-dir", type=Path, help="keep peer/server logs in a fresh directory")
    args = parser.parse_args()
    if args.output_dir is not None:
        args.output_dir.mkdir(parents=True, exist_ok=False)
        asyncio.run(run(args.server.resolve(), args.scenario, args.output_dir.resolve()))
        return
    with tempfile.TemporaryDirectory(prefix="cnetmod-quic-initial-credit-") as temporary:
        asyncio.run(run(args.server.resolve(), args.scenario, Path(temporary)))


if __name__ == "__main__":
    main()
