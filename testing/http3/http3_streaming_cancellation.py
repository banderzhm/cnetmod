"""A downstream handler timer must coexist with the request body pump."""

import asyncio
from pathlib import Path
import subprocess
import sys
import tempfile

from http_client_e2e import make_certificate, reserve_port, stop_process, wait_for_server
from http3_lifecycle import Probe, configuration
from aioquic.asyncio import connect


async def check(port):
    async with connect("127.0.0.1", port, configuration=configuration(),
                       create_protocol=Probe) as client:
        baseline = int(await client.get(port, b"/stream-wait-count"))
        for turn in range(3):
            stream = client._quic.get_next_available_stream_id()
            client.h3.send_headers(stream, client.headers(port, b"/stream-wait"),
                                   end_stream=False)
            client.transmit()
            # No body/FIN: pump is waiting for readable while the handler
            # awaits a 30-second cancellable downstream timer.
            await asyncio.sleep(0.05)
            client._quic.reset_stream(stream, 0x10C)
            client.transmit()
            for _ in range(50):
                count = int(await client.get(port, b"/stream-wait-count"))
                if count == baseline + turn + 1:
                    break
                await asyncio.sleep(0.01)
            assert count == baseline + turn + 1, (baseline, turn, count)
            assert stream in client.reset_frames, "RESET was not actually sent"
            assert await client.get(port, b"/get") == b"get-ok"

        stream = client._quic.get_next_available_stream_id()
        response = {"status": None, "body": bytearray(),
                    "done": asyncio.get_running_loop().create_future()}
        client.responses[stream] = response
        client.h3.send_headers(stream, client.headers(port, b"/stream-early"),
                               end_stream=False)
        client.transmit()
        await asyncio.wait_for(response["done"], 2)
        assert response["status"] == b"200" and response["body"] == b"early-ok", response
        client.h3.send_data(stream, b"", end_stream=True)
        client.transmit()
        assert await client.get(port, b"/get") == b"get-ok"
        print("PASS streaming downstream IO + peer RESET x3 + normal early response + reuse")


def main():
    server_binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as directory:
        cert, key = make_certificate(Path(directory))
        port = reserve_port()
        server = subprocess.Popen([str(server_binary), "--port", str(port),
            "--cert", str(cert), "--key", str(key)], cwd=directory,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            wait_for_server(server)
            asyncio.run(check(port))
        finally:
            stop_process(server)


if __name__ == "__main__":
    main()
