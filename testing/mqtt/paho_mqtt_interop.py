#!/usr/bin/env python3
import contextlib
import ssl
import os
import queue
import socket
import subprocess
import sys
import time
import uuid

try:
    import paho.mqtt.client as mqtt
    from paho.mqtt.packettypes import PacketTypes
    from paho.mqtt.properties import Properties
except Exception as exc:
    print(f"SKIP: paho-mqtt is not installed: {exc}")
    sys.exit(0)


HOST = "127.0.0.1"
TIMEOUT = 8.0
TLS_CERT = None


def free_port() -> int:
    with contextlib.closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
        sock.bind((HOST, 0))
        return sock.getsockname()[1]


def wait_until(predicate, timeout=TIMEOUT, detail="condition") -> None:
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            if predicate():
                return
        except Exception as exc:
            last = exc
        time.sleep(0.01)
    if last:
        raise AssertionError(f"timeout waiting for {detail}: {last}")
    raise AssertionError(f"timeout waiting for {detail}")


def new_client(client_id: str, *, protocol=mqtt.MQTTv311, clean_session=True, userdata=None):
    cb_api = getattr(mqtt, "CallbackAPIVersion", None)
    if cb_api is not None:
        client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=client_id,
            clean_session=clean_session if protocol == mqtt.MQTTv311 else None,
            protocol=protocol,
            userdata=userdata,
        )
    else:
        client = mqtt.Client(
            client_id=client_id,
            clean_session=clean_session if protocol == mqtt.MQTTv311 else None,
            protocol=protocol,
            userdata=userdata,
        )
    client.enable_logger()
    if TLS_CERT:
        client.tls_set(ca_certs=TLS_CERT, cert_reqs=ssl.CERT_NONE)
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
        # Paho v2 ReasonCode stringifies successful MQTT 3.x CONNACK as
        # "Success" while older versions pass a plain integer.
        if str(reason_code).lower() == "success":
            return 0
        return -1

    def on_connect(c, userdata, flags, reason_code, properties=None):
        if reason_value(reason_code) == 0:
            connected.put(True)
        else:
            connected.put(RuntimeError(f"connect failed: {reason_code}"))

    client.on_connect = on_connect
    kwargs = {}
    if clean_start is not None:
        kwargs["clean_start"] = clean_start
    if properties is not None:
        kwargs["properties"] = properties
    client.connect(HOST, port, keepalive=10, **kwargs)
    client.loop_start()
    try:
        item = connected.get(timeout=TIMEOUT)
    except queue.Empty as exc:
        raise AssertionError("timeout waiting for MQTT CONNACK") from exc
    if item is not True:
        raise item


def disconnect_client(client):
    try:
        client.disconnect()
    finally:
        client.loop_stop()


class Subscriber:
    def __init__(self, port: int, topic: str, qos=0, *, client_id=None,
                 protocol=mqtt.MQTTv311, clean_session=True,
                 clean_start=None, connect_properties=None):
        self.port = port
        self.topic = topic
        self.client_id = client_id or f"sub-{uuid.uuid4()}"
        self.messages = queue.Queue()
        self.subscribed = queue.Queue()
        self.client = new_client(
            self.client_id,
            protocol=protocol,
            clean_session=clean_session,
        )

        def on_message(c, userdata, msg):
            self.messages.put({
                "topic": msg.topic,
                "payload": msg.payload.decode("utf-8", "replace"),
                "qos": msg.qos,
                "retain": msg.retain,
            })

        def on_subscribe(c, userdata, mid, reason_codes, properties=None):
            self.subscribed.put(reason_codes)

        self.client.on_message = on_message
        self.client.on_subscribe = on_subscribe
        connect_client(self.client, port,
                       clean_start=clean_start,
                       properties=connect_properties)
        self.client.subscribe(topic, qos=qos)
        self.subscribed.get(timeout=TIMEOUT)

    def get(self, timeout=TIMEOUT):
        return self.messages.get(timeout=timeout)

    def drain(self):
        out = []
        while True:
            try:
                out.append(self.messages.get_nowait())
            except queue.Empty:
                return out

    def close(self):
        disconnect_client(self.client)


def publish(port: int, topic: str, payload: str, qos=0, retain=False, *,
            client_id=None, protocol=mqtt.MQTTv311):
    client = new_client(client_id or f"pub-{uuid.uuid4()}", protocol=protocol)
    connect_client(client, port)
    info = client.publish(topic, payload=payload, qos=qos, retain=retain)
    info.wait_for_publish(timeout=TIMEOUT)
    if info.rc != mqtt.MQTT_ERR_SUCCESS:
        raise AssertionError(f"publish failed rc={info.rc}")
    disconnect_client(client)


def assert_payload(msg, payload: str, topic_prefix=None):
    if topic_prefix and not msg["topic"].startswith(topic_prefix):
        raise AssertionError(f"unexpected topic: {msg}")
    if msg["payload"] != payload:
        raise AssertionError(f"unexpected payload: {msg}, expected={payload}")


def protocol_name(protocol) -> str:
    return "v5" if protocol == mqtt.MQTTv5 else "v3"


def session_expiry_props(seconds=3600):
    props = Properties(PacketTypes.CONNECT)
    props.SessionExpiryInterval = seconds
    return props


def test_qos_roundtrip(port: int, prefix: str, protocol):
    for qos in (0, 1, 2):
        sub = Subscriber(port, f"{prefix}/qos/{qos}", qos=qos, protocol=protocol)
        try:
            publish(port, f"{prefix}/qos/{qos}", f"qos-{qos}", qos=qos, protocol=protocol)
            msg = sub.get()
            assert_payload(msg, f"qos-{qos}")
            if msg["qos"] > qos:
                raise AssertionError(f"broker elevated qos: requested={qos} got={msg}")
        finally:
            sub.close()
    print(f"ok {protocol_name(protocol)} qos0/qos1/qos2")


def test_qos2_exactly_once_bulk(port: int, prefix: str, protocol):
    topic = f"{prefix}/qos2/bulk"
    sub = Subscriber(port, topic, qos=2, protocol=protocol)
    try:
        expected = [f"qos2-{i}" for i in range(32)]
        for payload in expected:
            publish(port, topic, payload, qos=2, protocol=protocol)

        got = []
        deadline = time.monotonic() + TIMEOUT
        while time.monotonic() < deadline and len(got) < len(expected):
            try:
                got.append(sub.messages.get(timeout=0.05)["payload"])
            except queue.Empty:
                pass

        if got != expected:
            raise AssertionError(f"qos2 order/count mismatch: got={got} expected={expected}")
        time.sleep(0.2)
        extra = sub.drain()
        if extra:
            raise AssertionError(f"qos2 duplicate deliveries: {extra}")
    finally:
        sub.close()
    print(f"ok {protocol_name(protocol)} qos2 exactly-once bulk")


def test_retained(port: int, prefix: str, protocol):
    topic = f"{prefix}/retain/item"
    publish(port, topic, "retained-value", qos=2, retain=True, protocol=protocol)
    sub = Subscriber(port, topic, qos=2, protocol=protocol)
    try:
        msg = sub.get()
        assert_payload(msg, "retained-value")
        if not msg["retain"]:
            raise AssertionError(f"expected retained flag, got {msg}")
    finally:
        sub.close()
    publish(port, topic, "", qos=2, retain=True, protocol=protocol)
    print(f"ok {protocol_name(protocol)} retained qos2 publish/delivery/clear")


def test_wildcard(port: int, prefix: str, protocol):
    sub = Subscriber(port, f"{prefix}/+/sensor/#", qos=2, protocol=protocol)
    try:
        publish(port, f"{prefix}/a/sensor/temp", "match-a", qos=2, protocol=protocol)
        publish(port, f"{prefix}/b/other/temp", "no-match", qos=2, protocol=protocol)
        msg = sub.get()
        assert_payload(msg, "match-a")
        time.sleep(0.2)
        extra = sub.drain()
        if any(m["payload"] == "no-match" for m in extra):
            raise AssertionError(f"wildcard matched wrong topic: {extra}")
    finally:
        sub.close()
    print(f"ok {protocol_name(protocol)} wildcard qos2")


def test_shared_subscription(port: int, prefix: str, protocol):
    topic = f"{prefix}/shared/source"
    filter_name = f"$share/group1/{prefix}/shared/#"
    sub1 = Subscriber(port, filter_name, qos=2, client_id=f"shared-a-{uuid.uuid4()}",
                      protocol=protocol)
    sub2 = Subscriber(port, filter_name, qos=2, client_id=f"shared-b-{uuid.uuid4()}",
                      protocol=protocol)
    try:
        expected = {f"shared-{i}" for i in range(12)}
        for payload in expected:
            publish(port, topic, payload, qos=2, protocol=protocol)
        got = set()
        deadline = time.monotonic() + TIMEOUT
        while time.monotonic() < deadline and len(got) < len(expected):
            for sub in (sub1, sub2):
                try:
                    got.add(sub.messages.get(timeout=0.05)["payload"])
                except queue.Empty:
                    pass
        if got != expected:
            raise AssertionError(f"shared subscription mismatch: got={sorted(got)} expected={sorted(expected)}")
    finally:
        sub1.close()
        sub2.close()
    print(f"ok {protocol_name(protocol)} shared subscription qos2")


def test_will_message(port: int, prefix: str, protocol):
    will_topic = f"{prefix}/will"
    sub = Subscriber(port, will_topic, qos=2, protocol=protocol)
    will_client = new_client(
        f"will-{uuid.uuid4()}",
        protocol=protocol,
        clean_session=True,
    )
    will_client.will_set(will_topic, payload="gone", qos=2, retain=False)
    connect_client(will_client, port)
    try:
        # Force an ungraceful TCP close; broker should publish the will.
        sock = getattr(will_client, "_sock", None)
        if sock is not None:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        with contextlib.suppress(Exception):
            will_client._sock_close()
        will_client.loop_stop()
        msg = sub.get(timeout=TIMEOUT)
        assert_payload(msg, "gone")
    finally:
        sub.close()
    print(f"ok {protocol_name(protocol)} will message qos2")


def test_persistent_offline_queue(port: int, prefix: str, protocol):
    client_id = f"persist-{uuid.uuid4()}"
    topic = f"{prefix}/offline/qos2"

    if protocol == mqtt.MQTTv5:
        props = session_expiry_props()
        sub = Subscriber(port, topic, qos=2, client_id=client_id,
                         clean_session=True, protocol=protocol,
                         clean_start=False, connect_properties=props)
    else:
        sub = Subscriber(port, topic, qos=2, client_id=client_id,
                         clean_session=False, protocol=protocol)
    sub.close()

    publish(port, topic, "offline-1", qos=2, protocol=protocol)

    if protocol == mqtt.MQTTv5:
        sub2 = Subscriber(port, topic, qos=2, client_id=client_id,
                          clean_session=True, protocol=protocol,
                          clean_start=False, connect_properties=session_expiry_props())
    else:
        sub2 = Subscriber(port, topic, qos=2, client_id=client_id,
                          clean_session=False, protocol=protocol)
    try:
        msg = sub2.get()
        assert_payload(msg, "offline-1")
    finally:
        sub2.close()
    print(f"ok {protocol_name(protocol)} persistent offline qos2 queue")


def start_broker(exe: str, port: int, tls_cert: str | None, tls_key: str | None):
    args = [exe, str(port)]
    if tls_cert and tls_key:
        args.extend(["--tls", tls_cert, tls_key])
    proc = subprocess.Popen(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    assert proc.stdout is not None
    deadline = time.monotonic() + TIMEOUT
    lines = []
    while time.monotonic() < deadline:
        line = proc.stdout.readline()
        if line:
            print("broker:", line.rstrip())
            lines.append(line)
            if f"READY {port}" in line:
                return proc
        if proc.poll() is not None:
            raise RuntimeError(f"broker exited early rc={proc.returncode}; output={''.join(lines)}")
    proc.terminate()
    raise RuntimeError(f"broker did not become ready; output={''.join(lines)}")


def main() -> int:
    global TLS_CERT
    if len(sys.argv) not in (2, 5):
        print("usage: paho_mqtt_interop.py <mqtt_interop_broker_exe> [--tls <cert.pem> <key.pem>]", file=sys.stderr)
        return 2

    exe = sys.argv[1]
    if not os.path.exists(exe):
        print(f"broker executable not found: {exe}", file=sys.stderr)
        return 2

    tls_key = None
    if len(sys.argv) == 5:
        if sys.argv[2] != "--tls":
            print("usage: paho_mqtt_interop.py <mqtt_interop_broker_exe> [--tls <cert.pem> <key.pem>]", file=sys.stderr)
            return 2
        TLS_CERT = sys.argv[3]
        tls_key = sys.argv[4]
        if not os.path.exists(TLS_CERT) or not os.path.exists(tls_key):
            print(f"TLS cert/key not found: {TLS_CERT} {tls_key}", file=sys.stderr)
            return 2

    port = free_port()
    proc = start_broker(exe, port, TLS_CERT, tls_key)
    prefix = f"paho/{uuid.uuid4().hex}"
    try:
        for protocol in (mqtt.MQTTv311, mqtt.MQTTv5):
            proto_prefix = f"{prefix}/{protocol_name(protocol)}"
            test_qos_roundtrip(port, proto_prefix, protocol)
            test_qos2_exactly_once_bulk(port, proto_prefix, protocol)
            test_retained(port, proto_prefix, protocol)
            test_wildcard(port, proto_prefix, protocol)
            test_shared_subscription(port, proto_prefix, protocol)
            test_will_message(port, proto_prefix, protocol)
            test_persistent_offline_queue(port, proto_prefix, protocol)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)
    print("PASS paho mqtt interop")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
