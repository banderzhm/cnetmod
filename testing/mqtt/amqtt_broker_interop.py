#!/usr/bin/env python3
import asyncio
import contextlib
import socket
import subprocess
import sys
import uuid

try:
    from amqtt.broker import Broker
except Exception as exc:
    print(f"SKIP: amqtt is not installed: {exc}")
    sys.exit(0)


HOST = "127.0.0.1"
TIMEOUT = 30.0


def free_port() -> int:
    with contextlib.closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
        sock.bind((HOST, 0))
        return sock.getsockname()[1]


async def wait_for_port(port: int) -> None:
    deadline = asyncio.get_running_loop().time() + 8.0
    last_error = None
    while asyncio.get_running_loop().time() < deadline:
        try:
            reader, writer = await asyncio.open_connection(HOST, port)
            writer.close()
            await writer.wait_closed()
            return
        except Exception as exc:
            last_error = exc
            await asyncio.sleep(0.05)
    raise RuntimeError(f"broker did not open port {port}: {last_error}")


async def run_client(client_exe: str, port: int) -> None:
    prefix = f"amqtt/{uuid.uuid4().hex}"
    proc = await asyncio.create_subprocess_exec(
        client_exe,
        HOST,
        str(port),
        prefix,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=TIMEOUT)
    except asyncio.TimeoutError:
        proc.kill()
        await proc.wait()
        raise AssertionError("cnetmod mqtt client interop timed out")

    out = stdout.decode("utf-8", "replace")
    err = stderr.decode("utf-8", "replace")
    if out:
        print(out, end="")
    if err:
        print(err, end="", file=sys.stderr)
    if proc.returncode != 0:
        raise AssertionError(f"cnetmod mqtt client exited with {proc.returncode}")


async def main_async(client_exe: str) -> None:
    port = free_port()
    config = {
        "listeners": {
            "default": {
                "type": "tcp",
                "bind": f"{HOST}:{port}",
            }
        },
        "sys_interval": 0,
        "topic-check": {"enabled": False},
        "auth": {"allow-anonymous": True},
    }
    broker = Broker(config)
    await broker.start()
    try:
        await wait_for_port(port)
        await run_client(client_exe, port)
    finally:
        await broker.shutdown()


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: amqtt_broker_interop.py <mqtt_interop_client_exe>", file=sys.stderr)
        return 2
    try:
        asyncio.run(main_async(sys.argv[1]))
    except Exception as exc:
        print(f"FAIL amqtt broker interop: {exc}", file=sys.stderr)
        return 1
    print("PASS amqtt broker interop")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
