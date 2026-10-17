"""Regressions for real aioquic dispatch of a locally initiated child stream."""
import unittest

from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.connection import QuicConnection
from aioquic.quic.events import ConnectionTerminated, StreamDataReceived

from h3_webtransport_aioquic_probe import WebTransportProbeProtocol


class WebTransportProbeTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        connection = QuicConnection(configuration=QuicConfiguration(is_client=True))
        self.protocol = WebTransportProbeProtocol(connection)
        self.stream = self.protocol.open_child_stream(4)

    def receive(self, data, ended=False):
        self.protocol.quic_event_received(StreamDataReceived(
            data=data, end_stream=ended, stream_id=self.stream))

    async def test_unframed_peer_echo_is_webtransport_data(self):
        # The initiator already sent the preface. The echo direction carries
        # only application bytes; parsing it as H3 produces H3_FRAME_ERROR.
        self.receive(b"strm", ended=True)
        await self.protocol.expect_child_echo(0.1)

    async def test_partial_echo_is_not_success_until_fin(self):
        for part in (b"s", b"tr", b"m"):
            self.receive(part)
            self.assertFalse(self.protocol.stream_echo.done())
        self.receive(b"", ended=True)
        await self.protocol.expect_child_echo(0.1)

    async def test_truncated_echo_fails(self):
        self.receive(b"str", ended=True)
        with self.assertRaisesRegex(RuntimeError, "echo mismatch"):
            await self.protocol.expect_child_echo(0.1)

    async def test_extra_bytes_fail(self):
        self.receive(b"strm-extra", ended=True)
        with self.assertRaisesRegex(RuntimeError, "echo mismatch"):
            await self.protocol.expect_child_echo(0.1)

    async def test_unrelated_stream_is_not_delivered_as_child_echo(self):
        self.protocol.quic_event_received(StreamDataReceived(
            data=b"strm", end_stream=True, stream_id=self.stream + 4))
        self.assertFalse(self.protocol.stream_echo.done())

    async def test_connection_error_is_visible_in_timeout(self):
        self.protocol.quic_event_received(ConnectionTerminated(
            error_code=262, frame_type=None, reason_phrase="Frame is truncated"))
        with self.assertRaisesRegex(RuntimeError, "Frame is truncated"):
            await self.protocol.expect_child_echo(0.01)


if __name__ == "__main__":
    unittest.main()
