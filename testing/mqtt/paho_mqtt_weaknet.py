#!/usr/bin/env python3
import asyncio
import contextlib
import os
import queue
import random
import socket
import ssl
import subprocess
import sys
import threading
import time
import uuid

try:
    import paho.mqtt.client as mqtt
    from paho.mqtt.packettypes import PacketTypes
    from paho.mqtt.properties import Properties
except Exception as exc:
    print(f"SKIP: paho-mqtt is not installed: {exc}")
    sys.exit(0)

import paho_mqtt_interop as interop


HOST = "127.0.0.1"
TIMEOUT = 12.0


def free_port() -> int:
    with contextlib.closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
        sock.bind((HOST, 0))
        return sock.getsockname()[1]


class WeakTcpProxy:
    def __init__(self, listen_port: int, target_port: int, *, seed: int = 0xC0FFEE):
        self.listen_port = listen_port
        self.target_port = target_port
        self.seed = seed
        self.thread = None
        self.loop = None
        self.server = None
        self.ready = threading.Event()
        self.stop_event = None
        self.connections = []

    async def _pipe(self, reader, writer, rng: random.Random, label: str):
        try:
            while not self.stop_event.is_set():
                size = rng.randint(1, 384)
                data = await reader.read(size)
                if not data:
                    break
                pos = 0
                while pos < len(data):
                    step = rng.randint(1, min(96, len(data) - pos))
                    if rng.random() < 0.70:
                        await asyncio.sleep(rng.uniform(0.0005, 0.008))
                    writer.write(data[pos:pos + step])
                    await writer.drain()
                    pos += step
        except (ConnectionError, OSError, asyncio.IncompleteReadError):
            pass
        finally:
            writer.close()
            with contextlib.suppress(Exception):
                await writer.wait_closed()

    async def _handle(self, client_reader, client_writer):
        conn_id = len(self.connections) + 1
        self.connections.append(client_writer)
        rng = random.Random(self.seed + conn_id * 131)
        try:
            backend_reader, backend_writer = await asyncio.open_connection(HOST, self.target_port)
        except Exception:
            client_writer.close()
            with contextlib.suppress(Exception):
                await client_writer.wait_closed()
            return

        await asyncio.gather(
            self._pipe(client_reader, backend_writer, rng, "c2s"),
            self._pipe(backend_reader, client_writer, rng, "s2c"),
            return_exceptions=True,
        )

    async def _run(self):
        self.stop_event = asyncio.Event()
        self.server = await asyncio.start_server(self._handle, HOST, self.listen_port)
        self.ready.set()
        async with self.server:
            await self.stop_event.wait()
            self.server.close()
            await self.server.wait_closed()
        for writer in list(self.connections):
            writer.close()
            with contextlib.suppress(Exception):
                await writer.wait_closed()

    def start(self):
        def runner():
            self.loop = asyncio.new_event_loop()
            asyncio.set_event_loop(self.loop)
            self.loop.run_until_complete(self._run())
            self.loop.close()

        self.thread = threading.Thread(target=runner, daemon=True)
        self.thread.start()
        if not self.ready.wait(timeout=TIMEOUT):
            raise RuntimeError("weak proxy did not start")

    def reset_all(self):
        if not self.loop:
            return

        async def closer():
            for writer in list(self.connections):
                writer.close()
                with contextlib.suppress(Exception):
                    await writer.wait_closed()
            self.connections.clear()

        fut = asyncio.run_coroutine_threadsafe(closer(), self.loop)
        fut.result(timeout=TIMEOUT)

    def stop(self):
        if self.loop and self.stop_event:
            self.loop.call_soon_threadsafe(self.stop_event.set)
        if self.thread:
            self.thread.join(timeout=TIMEOUT)


def make_client(client_id: str, *, protocol, clean_session=True):
    cb_api = getattr(mqtt, "CallbackAPIVersion", None)
    kwargs = {
        "client_id": client_id,
        "clean_session": clean_session if protocol == mqtt.MQTTv311 else None,
        "protocol": protocol,
        "reconnect_on_failure": False,
    }
    if cb_api is not None:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, **kwargs)
    else:
        kwargs.pop("reconnect_on_failure", None)
        client = mqtt.Client(**kwargs)
    if interop.TLS_CERT:
        client.tls_set(ca_certs=interop.TLS_CERT, cert_reqs=ssl.CERT_NONE)
        client.tls_insecure_set(True)
    return client


def connect_client(client, port: int, *, clean_start=None, properties=None):
    connected = queue.Queue()

    def reason_value(reason_code):
        if isinstance(reason_code, int):
            return reason_code
        value = getattr(reason_code, "value", None)
        if isinstance(value, int):
            return value
        if str(reason_code).lower() == "success":
            return 0
        return -1

    def on_connect(c, userdata, flags, reason_code, props=None):
        connected.put(reason_value(reason_code))

    client.on_connect = on_connect
    kwargs = {}
    if clean_start is not None:
        kwargs["clean_start"] = clean_start
    if properties is not None:
        kwargs["properties"] = properties
    client.connect(HOST, port, keepalive=5, **kwargs)
    client.loop_start()
    try:
        rc = connected.get(timeout=TIMEOUT)
    except queue.Empty as exc:
        raise AssertionError("timeout waiting for weaknet CONNACK") from exc
    if rc != 0:
        raise AssertionError(f"connect failed rc={rc}")


def subscribe_wait(client, topic: str, qos: int):
    subscribed = queue.Queue()

    def on_subscribe(c, userdata, mid, reason_codes, properties=None):
        subscribed.put(reason_codes)

    client.on_subscribe = on_subscribe
    result, _ = client.subscribe(topic, qos=qos)
    if result != mqtt.MQTT_ERR_SUCCESS:
        raise AssertionError(f"subscribe failed rc={result}")
    try:
        subscribed.get(timeout=TIMEOUT)
    except queue.Empty as exc:
        raise AssertionError("timeout waiting for weaknet SUBACK") from exc


def session_expiry_props(seconds=3600):
    props = Properties(PacketTypes.CONNECT)
    props.SessionExpiryInterval = seconds
    return props


def test_forced_drop_persistent_qos(proxy: WeakTcpProxy, port: int, prefix: str, protocol):
    topic = f"{prefix}/{interop.protocol_name(protocol)}/drop/offline"
    client_id = f"weak-persist-{uuid.uuid4()}"
    messages = queue.Queue()

    if protocol == mqtt.MQTTv5:
        connect_kwargs = {
            "clean_start": False,
            "properties": session_expiry_props(),
        }
        clean_session = True
    else:
        connect_kwargs = {}
        clean_session = False

    sub = make_client(client_id, protocol=protocol, clean_session=clean_session)
    sub.on_message = lambda c, userdata, msg: messages.put(msg.payload.decode("utf-8", "replace"))
    connect_client(sub, port, **connect_kwargs)
    subscribe_wait(sub, topic, 2)
    time.sleep(0.2)

    proxy.reset_all()
    sub.loop_stop()
    with contextlib.suppress(Exception):
        sub.disconnect()

    for idx in range(8):
        interop.publish(port, topic, f"offline-{idx}", qos=2, protocol=protocol)

    sub2 = make_client(client_id, protocol=protocol, clean_session=clean_session)
    sub2.on_message = lambda c, userdata, msg: messages.put(msg.payload.decode("utf-8", "replace"))
    connect_client(sub2, port, **connect_kwargs)
    subscribe_wait(sub2, topic, 2)

    got = []
    deadline = time.monotonic() + TIMEOUT
    while time.monotonic() < deadline and len(got) < 8:
        try:
            got.append(messages.get(timeout=0.05))
        except queue.Empty:
            pass
    sub2.disconnect()
    sub2.loop_stop()

    expected = [f"offline-{idx}" for idx in range(8)]
    if got != expected:
        raise AssertionError(f"forced-drop offline QoS2 mismatch: got={got} expected={expected}")
    time.sleep(0.2)
    extras = []
    while True:
        try:
            extras.append(messages.get_nowait())
        except queue.Empty:
            break
    if extras:
        raise AssertionError(f"forced-drop QoS2 duplicate deliveries: {extras}")
    print(f"ok weaknet forced-drop {interop.protocol_name(protocol)} persistent qos2")


def run_suite_through_proxy(proxy_port: int, *, full_matrix: bool):
    prefix = f"weaknet/{uuid.uuid4().hex}"
    protocols = (mqtt.MQTTv311, mqtt.MQTTv5) if full_matrix else (mqtt.MQTTv5,)
    for protocol in protocols:
        proto_prefix = f"{prefix}/{interop.protocol_name(protocol)}"
        interop.test_qos_roundtrip(proxy_port, proto_prefix, protocol)
        interop.test_persistent_offline_queue(proxy_port, proto_prefix, protocol)
        if not full_matrix:
            continue
        interop.test_qos2_exactly_once_bulk(proxy_port, proto_prefix, protocol)
        interop.test_retained(proxy_port, proto_prefix, protocol)
        interop.test_wildcard(proxy_port, proto_prefix, protocol)
        interop.test_shared_subscription(proxy_port, proto_prefix, protocol)


def main() -> int:
    if len(sys.argv) not in (2, 5):
        print("usage: paho_mqtt_weaknet.py <mqtt_interop_broker_exe> [--tls <cert.pem> <key.pem>]",
              file=sys.stderr)
        return 2

    broker_exe = sys.argv[1]
    if not os.path.exists(broker_exe):
        print(f"broker executable not found: {broker_exe}", file=sys.stderr)
        return 2

    tls_key = None
    if len(sys.argv) == 5:
        if sys.argv[2] != "--tls":
            print("usage: paho_mqtt_weaknet.py <mqtt_interop_broker_exe> [--tls <cert.pem> <key.pem>]",
                  file=sys.stderr)
            return 2
        interop.TLS_CERT = sys.argv[3]
        tls_key = sys.argv[4]

    broker_port = free_port()
    proxy_port = free_port()
    broker = interop.start_broker(broker_exe, broker_port, interop.TLS_CERT, tls_key)
    proxy = WeakTcpProxy(proxy_port, broker_port)
    proxy.start()
    try:
        full_matrix = interop.TLS_CERT is None or os.environ.get("CNETMOD_MQTT_WEAKNET_FULL") == "1"
        transport = "mqtts" if interop.TLS_CERT else "mqtt"
        matrix = "full" if full_matrix else "reduced"
        print(f"INFO paho mqtt weaknet transport={transport} matrix={matrix} proxy=chunk+jitter+forced-drop")
        run_suite_through_proxy(proxy_port, full_matrix=full_matrix)
        drop_protocols = (mqtt.MQTTv311, mqtt.MQTTv5) if full_matrix else (mqtt.MQTTv5,)
        for protocol in drop_protocols:
            test_forced_drop_persistent_qos(proxy, proxy_port,
                                            f"weaknet/drop/{uuid.uuid4().hex}",
                                            protocol)
    finally:
        proxy.stop()
        broker.terminate()
        try:
            broker.wait(timeout=3)
        except subprocess.TimeoutExpired:
            broker.kill()
            broker.wait(timeout=3)
    print("PASS paho mqtt weaknet")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
