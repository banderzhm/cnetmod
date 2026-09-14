"""Loopback-only TCP outage fixture; never stops or reconfigures a database.

Run: python outage_proxy.py UPSTREAM_PORT -- COMMAND ... {port} ...
The child receives CNETMOD_OUTAGE_CONTROL_PORT and CNETMOD_OUTAGE_CONTROL_TOKEN.
Control requests are bounded ASCII lines: TOKEN up\n or TOKEN down\n.
A down acknowledgement follows cancellation and settlement of all relay tasks.
"""

import argparse
import asyncio
import hmac
import os
import secrets


class OutageProxy:
    def __init__(self, upstream_port):
        if not 1 <= upstream_port <= 65535:
            raise ValueError('Invalid loopback upstream port')
        self.upstream_port = upstream_port
        self.token = secrets.token_hex(16)
        self.enabled = True
        self.sessions = set()
        self.controls = set()
        self.server = None
        self.control = None
        self.control_lock = asyncio.Lock()

    async def start(self):
        self.server = await asyncio.start_server(self._relay, '127.0.0.1', 0)
        try:
            self.control = await asyncio.start_server(
                self._control, '127.0.0.1', 0, limit=128)
        except BaseException:
            self.server.close()
            await self.server.wait_closed()
            raise
        return self

    @property
    def port(self):
        return self.server.sockets[0].getsockname()[1]

    @property
    def control_port(self):
        return self.control.sockets[0].getsockname()[1]

    async def set_enabled(self, enabled):
        async with self.control_lock:
            self.enabled = enabled
            if not enabled:
                sessions = list(self.sessions)
                for task in sessions:
                    task.cancel()
                await asyncio.gather(*sessions, return_exceptions=True)

    async def close(self):
        for server in (self.control, self.server):
            if server:
                server.close()
        controls = list(self.controls)
        for task in controls:
            task.cancel()
        await asyncio.gather(*controls, return_exceptions=True)
        await self.set_enabled(False)
        # asyncio.Server.wait_closed() waits for active connection handlers.
        # Relays must be cancelled first, otherwise a client that keeps its
        # stream open can deadlock shutdown before set_enabled(False) runs.
        for server in (self.control, self.server):
            if server:
                await server.wait_closed()

    @staticmethod
    async def _pump(reader, writer):
        while data := await reader.read(65536):
            writer.write(data)
            await writer.drain()

    async def _relay(self, reader, writer):
        owner = asyncio.current_task()
        self.sessions.add(owner)
        upstream = None
        pumps = []
        try:
            if not self.enabled:
                return
            remote, upstream = await asyncio.wait_for(
                asyncio.open_connection('127.0.0.1', self.upstream_port), 2)
            pumps = [asyncio.create_task(self._pump(reader, upstream)),
                     asyncio.create_task(self._pump(remote, writer))]
            await asyncio.wait(pumps, return_when=asyncio.FIRST_COMPLETED)
        except (OSError, asyncio.TimeoutError):
            pass
        finally:
            for task in pumps:
                task.cancel()
            await asyncio.gather(*pumps, return_exceptions=True)
            for stream in (writer, upstream):
                if stream:
                    stream.close()
                    try:
                        await asyncio.wait_for(stream.wait_closed(), 1)
                    except (OSError, asyncio.TimeoutError):
                        pass
            self.sessions.discard(owner)

    async def _control(self, reader, writer):
        owner = asyncio.current_task()
        self.controls.add(owner)
        try:
            line = await asyncio.wait_for(reader.readline(), 2)
            parts = line.decode('ascii').strip().split()
            if (len(parts) != 2 or not hmac.compare_digest(parts[0], self.token)
                    or parts[1] not in ('up', 'down')):
                writer.write(b'ERROR\n')
            else:
                await self.set_enabled(parts[1] == 'up')
                writer.write(b'OK\n')
            await writer.drain()
        except (OSError, ValueError, asyncio.TimeoutError):
            pass
        finally:
            writer.close()
            try:
                await asyncio.wait_for(writer.wait_closed(), 1)
            except (OSError, asyncio.TimeoutError):
                pass
            self.controls.discard(owner)


async def run(upstream_port, command):
    proxy = await OutageProxy(upstream_port).start()
    child = None
    try:
        environment = os.environ.copy()
        environment['CNETMOD_OUTAGE_CONTROL_PORT'] = str(proxy.control_port)
        environment['CNETMOD_OUTAGE_CONTROL_TOKEN'] = proxy.token
        command = [str(proxy.port) if argument == '{port}' else argument for argument in command]
        child = await asyncio.create_subprocess_exec(*command, env=environment)
        return await asyncio.wait_for(child.wait(), 180)
    finally:
        if child and child.returncode is None:
            child.kill()
            await child.wait()
        await proxy.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('upstream_port', type=int)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    arguments = parser.parse_args()
    command = arguments.command
    if command and command[0] == '--':
        command = command[1:]
    if not command:
        parser.error('A child command is required')
    raise SystemExit(asyncio.run(run(arguments.upstream_port, command)))
