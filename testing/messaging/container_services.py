"""Real broker containers used by the interoperability suites."""

from __future__ import annotations

import socket
import time
from dataclasses import dataclass
from typing import Callable

@dataclass(frozen=True)
class Amqp091BrokerEndpoint:
    host: str
    port: int
    username: str
    password: str
    virtual_host: str = "/"


@dataclass(frozen=True)
class Amqp10BrokerEndpoint:
    host: str
    port: int
    username: str
    password: str
    supports_transactions: bool = False


@dataclass(frozen=True)
class KafkaBrokerEndpoint:
    host: str
    port: int
    security_protocol: str = "PLAINTEXT"
    sasl_mechanism: str = ""
    username: str = ""
    password: str = ""


def _wait_until_usable(
    name: str, probe: Callable[[], None], timeout_seconds: float
) -> None:
    deadline = time.monotonic() + timeout_seconds
    last_error: BaseException | None = None
    while time.monotonic() < deadline:
        try:
            probe()
            return
        except Exception as error:
            last_error = error
            time.sleep(0.5)
    raise TimeoutError(f"{name} did not become usable: {last_error}")


def _probe_rabbitmq(endpoint: Amqp091BrokerEndpoint) -> None:
    import pika

    connection = pika.BlockingConnection(
        pika.ConnectionParameters(
            endpoint.host,
            endpoint.port,
            endpoint.virtual_host,
            pika.PlainCredentials(endpoint.username, endpoint.password),
            heartbeat=0,
            connection_attempts=1,
            socket_timeout=2,
            stack_timeout=3,
            blocked_connection_timeout=2,
        )
    )
    connection.close()


def _probe_artemis(endpoint: Amqp10BrokerEndpoint) -> None:
    from proton.utils import BlockingConnection

    connection = BlockingConnection(
        f"amqp://{endpoint.host}:{endpoint.port}",
        user=endpoint.username,
        password=endpoint.password,
        timeout=3,
    )
    connection.close()


def kafka_reference_configuration(endpoint: KafkaBrokerEndpoint) -> dict[str, object]:
    configuration: dict[str, object] = {
        "bootstrap.servers": f"{endpoint.host}:{endpoint.port}",
        "security.protocol": endpoint.security_protocol,
    }
    if endpoint.sasl_mechanism:
        configuration.update(
            {
                "sasl.mechanism": endpoint.sasl_mechanism,
                "sasl.username": endpoint.username,
                "sasl.password": endpoint.password,
            }
        )
    return configuration


def _probe_kafka(endpoint: KafkaBrokerEndpoint) -> None:
    from confluent_kafka.admin import AdminClient

    configuration = kafka_reference_configuration(endpoint)
    configuration["socket.timeout.ms"] = 2000
    AdminClient(configuration).list_topics(timeout=3)


def _available_loopback_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


class RabbitMqService:
    def __init__(self) -> None:
        from testcontainers.rabbitmq import RabbitMqContainer

        self.host_port = _available_loopback_port()
        self.container = (
            RabbitMqContainer(
                "rabbitmq:4.1-management",
                username="cnetmod",
                password="cnetmod-test-password",
            )
            # Bound Erlang scheduler creation so the disposable broker starts
            # deterministically on high-core CI and WSL hosts.
            .with_env("RABBITMQ_SERVER_ADDITIONAL_ERL_ARGS", "+S 2:2")
            .with_bind_ports(5672, self.host_port)
        )
        self.endpoint: Amqp091BrokerEndpoint | None = None

    def start(self) -> Amqp091BrokerEndpoint:
        try:
            self.container.start()
            endpoint = Amqp091BrokerEndpoint(
                self.container.get_container_host_ip(),
                self.host_port,
                "cnetmod",
                "cnetmod-test-password",
            )
            self.endpoint = endpoint
            _wait_until_usable("RabbitMQ", lambda: _probe_rabbitmq(endpoint), 60)
            return endpoint
        except Exception:
            try:
                self.stop()
            except Exception:
                pass
            raise

    def restart(self) -> None:
        wrapped = self.container.get_wrapped_container()
        wrapped.stop(timeout=10)
        wrapped.start()
        if self.endpoint is None:
            raise RuntimeError("RabbitMQ service has not been started")
        try:
            _wait_until_usable("RabbitMQ", lambda: _probe_rabbitmq(self.endpoint), 60)
        except Exception as error:
            wrapped.reload()
            logs = wrapped.logs(tail=40).decode("utf-8", errors="replace")
            raise RuntimeError(
                f"RabbitMQ restart failed with container status {wrapped.status}:\n{logs}"
            ) from error

    def stop(self) -> None:
        self.container.stop()


class ArtemisService:
    def __init__(self) -> None:
        from testcontainers.core.container import DockerContainer

        self.host_port = _available_loopback_port()
        self.container = (
            DockerContainer("apache/activemq-artemis:2.40.0")
            .with_bind_ports(5672, self.host_port)
            .with_env("ARTEMIS_USER", "cnetmod")
            .with_env("ARTEMIS_PASSWORD", "cnetmod-test-password")
            .with_env("ANONYMOUS_LOGIN", "false")
        )
        self.endpoint: Amqp10BrokerEndpoint | None = None

    def start(self) -> Amqp10BrokerEndpoint:
        try:
            self.container.start()
            endpoint = Amqp10BrokerEndpoint(
                self.container.get_container_host_ip(),
                self.host_port,
                "cnetmod",
                "cnetmod-test-password",
                supports_transactions=True,
            )
            self.endpoint = endpoint
            _wait_until_usable("ActiveMQ Artemis", lambda: _probe_artemis(endpoint), 90)
            return endpoint
        except Exception:
            try:
                self.stop()
            except Exception:
                pass
            raise

    def restart(self) -> None:
        wrapped = self.container.get_wrapped_container()
        wrapped.stop(timeout=10)
        wrapped.start()
        if self.endpoint is None:
            raise RuntimeError("ActiveMQ Artemis service has not been started")
        _wait_until_usable("ActiveMQ Artemis", lambda: _probe_artemis(self.endpoint), 90)

    def stop(self) -> None:
        self.container.stop()


class KafkaService:
    def __init__(self) -> None:
        from testcontainers.kafka import KafkaContainer

        self.host_port = _available_loopback_port()
        container = KafkaContainer("confluentinc/cp-kafka:7.9.1").with_kraft()
        container.security_protocol_map = "BROKER:PLAINTEXT,PLAINTEXT:SASL_PLAINTEXT"
        self.container = (
            container
            .with_bind_ports(9093, self.host_port)
            .with_env("KAFKA_TRANSACTION_STATE_LOG_REPLICATION_FACTOR", "1")
            .with_env("KAFKA_TRANSACTION_STATE_LOG_MIN_ISR", "1")
            .with_env("KAFKA_SASL_ENABLED_MECHANISMS", "PLAIN")
            .with_env(
                "KAFKA_LISTENER_NAME_PLAINTEXT_PLAIN_SASL_JAAS_CONFIG",
                "org.apache.kafka.common.security.plain.PlainLoginModule required "
                'username="cnetmod" password="cnetmod-test-password" '
                'user_cnetmod="cnetmod-test-password";',
            )
        )
        self.endpoint: KafkaBrokerEndpoint | None = None

    def start(self) -> KafkaBrokerEndpoint:
        try:
            self.container.start()
            bootstrap = self.container.get_bootstrap_server().removeprefix("PLAINTEXT://")
            host, port = bootstrap.rsplit(":", 1)
            endpoint = KafkaBrokerEndpoint(
                host,
                int(port),
                security_protocol="SASL_PLAINTEXT",
                sasl_mechanism="PLAIN",
                username="cnetmod",
                password="cnetmod-test-password",
            )
            self.endpoint = endpoint
            _wait_until_usable("Kafka", lambda: _probe_kafka(endpoint), 90)
            return endpoint
        except Exception:
            try:
                self.stop()
            except Exception:
                pass
            raise

    def restart(self) -> None:
        wrapped = self.container.get_wrapped_container()
        wrapped.restart(timeout=10)
        if self.endpoint is None:
            raise RuntimeError("Kafka service has not been started")
        try:
            _wait_until_usable("Kafka", lambda: _probe_kafka(self.endpoint), 90)
        except Exception as error:
            wrapped.reload()
            logs = wrapped.logs(tail=80).decode("utf-8", errors="replace")
            raise RuntimeError(
                f"Kafka restart failed with container status {wrapped.status}:\n{logs}"
            ) from error

    def stop(self) -> None:
        self.container.stop()
