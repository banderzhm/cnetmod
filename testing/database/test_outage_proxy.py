"""Verify isolation, acknowledgement and cleanup of the outage fixture."""

import asyncio
import unittest

from outage_proxy import OutageProxy


class ProxyTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.echo_tasks = set()
        self.clients = []

        async def echo(reader, writer):
            task = asyncio.current_task()
            self.echo_tasks.add(task)
            try:
                while data := await reader.read(65536):
                    writer.write(data)
                    await writer.drain()
            finally:
                writer.close()
                await writer.wait_closed()
                self.echo_tasks.discard(task)

        self.echo = await asyncio.start_server(echo, '127.0.0.1', 0)
        self.upstream = self.echo.sockets[0].getsockname()[1]
        self.proxy = await OutageProxy(self.upstream).start()

    async def asyncTearDown(self):
        await self.proxy.close()
        for writer in self.clients:
            await self.close_writer(writer)
        self.echo.close()
        await self.echo.wait_closed()
        tasks = list(self.echo_tasks)
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)

    @staticmethod
    async def close_writer(writer):
        writer.close()
        try:
            await asyncio.wait_for(writer.wait_closed(), 2)
        except (OSError, asyncio.TimeoutError):
            # Windows' proactor can leave a peer that was concurrently closed
            # by the server in a non-completing close state. This is a test
            # client only; the proxy task must already have been joined above.
            writer.transport.abort()

    async def connect(self, port):
        reader, writer = await asyncio.open_connection('127.0.0.1', port)
        self.clients.append(writer)
        return reader, writer

    async def command(self, command, token=None):
        reader, writer = await self.connect(self.proxy.control_port)
        writer.write(f'{token or self.proxy.token} {command}\n'.encode())
        await writer.drain()
        return await asyncio.wait_for(reader.readline(), 2)

    async def exchange(self, reader, writer):
        writer.write(b'test-bytes')
        await writer.drain()
        self.assertEqual(await asyncio.wait_for(reader.readexactly(10), 2), b'test-bytes')

    async def test_outage_settles_existing_connections_and_recovers(self):
        direct = await self.connect(self.upstream)
        proxied = await self.connect(self.proxy.port)
        await self.exchange(*proxied)
        self.assertEqual(await self.command('down'), b'OK\n')
        self.assertEqual(await asyncio.wait_for(proxied[0].read(), 2), b'')
        self.assertFalse(self.proxy.sessions)
        rejected = await self.connect(self.proxy.port)
        self.assertEqual(await asyncio.wait_for(rejected[0].read(), 2), b'')
        await self.exchange(*direct)
        self.assertEqual(await self.command('up'), b'OK\n')
        recovered = await self.connect(self.proxy.port)
        await self.exchange(*recovered)

    async def test_invalid_control_does_not_interrupt_connection(self):
        connection = await self.connect(self.proxy.port)
        self.assertEqual(await self.command('down', 'wrong-token'), b'ERROR\n')
        self.assertEqual(await self.command('unknown'), b'ERROR\n')
        await self.exchange(*connection)

    async def test_close_joins_relays_and_partial_control_requests(self):
        connection = await self.connect(self.proxy.port)
        await self.exchange(*connection)
        await self.connect(self.proxy.control_port)
        await asyncio.sleep(0)
        await self.proxy.close()
        self.assertFalse(self.proxy.sessions)
        self.assertFalse(self.proxy.controls)


if __name__ == '__main__':
    unittest.main()
