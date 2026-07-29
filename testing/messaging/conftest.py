from __future__ import annotations

import os
from pathlib import Path

import pytest
from docker import from_env as docker_from_environment
from dotenv import load_dotenv

from container_services import (
    Amqp091BrokerEndpoint,
    Amqp10BrokerEndpoint,
    ArtemisService,
    KafkaBrokerEndpoint,
    KafkaService,
    RabbitMqService,
)
from driver_process import MessagingDriver


load_dotenv(Path(__file__).resolve().parent / ".env.external.local", override=False)


def _service_mode() -> str:
    mode = os.environ.get("CNETMOD_MESSAGING_SERVICE_MODE", "auto").lower()
    if mode not in {"auto", "container", "external"}:
        pytest.fail(
            "CNETMOD_MESSAGING_SERVICE_MODE must be auto, container, or external"
        )
    return mode


def _required_environment(
    prefix: str, suffixes: tuple[str, ...]
) -> dict[str, str] | None:
    names = tuple(f"CNETMOD_{prefix}_{suffix}" for suffix in suffixes)
    present = [name for name in names if os.environ.get(name)]
    if not present:
        return None
    missing = [name for name in names if not os.environ.get(name)]
    if missing:
        pytest.fail("incomplete external broker endpoint: " + ", ".join(missing))
    return {suffix: os.environ[f"CNETMOD_{prefix}_{suffix}"] for suffix in suffixes}


def _amqp091_external_endpoint():
    values = _required_environment(
        "AMQP091", ("HOST", "PORT", "USERNAME", "PASSWORD", "VHOST")
    )
    if values is None:
        return None
    return Amqp091BrokerEndpoint(
        values["HOST"],
        int(values["PORT"]),
        values["USERNAME"],
        values["PASSWORD"],
        values["VHOST"],
    )


def _amqp10_external_endpoint():
    values = _required_environment(
        "AMQP10", ("HOST", "PORT", "USERNAME", "PASSWORD")
    )
    if values is None:
        return None
    return Amqp10BrokerEndpoint(
        values["HOST"], int(values["PORT"]), values["USERNAME"], values["PASSWORD"]
    )


def _kafka_external_endpoint():
    values = _required_environment(
        "KAFKA",
        (
            "HOST",
            "PORT",
            "SECURITY_PROTOCOL",
            "SASL_MECHANISM",
            "USERNAME",
            "PASSWORD",
        ),
    )
    if values is None:
        return None

    return KafkaBrokerEndpoint(
        values["HOST"],
        int(values["PORT"]),
        values["SECURITY_PROTOCOL"],
        values["SASL_MECHANISM"],
        values["USERNAME"],
        values["PASSWORD"],
    )


def _require_docker_or_skip(protocol: str) -> None:
    try:
        client = docker_from_environment()
        client.ping()
        client.close()
    except Exception as error:
        pytest.skip(
            f"{protocol}: neither an external endpoint nor a usable Docker engine "
            f"is available ({error})"
        )


def _driver(variable: str, protocol: str) -> MessagingDriver:
    try:
        return MessagingDriver.from_environment(variable, protocol)
    except FileNotFoundError as error:
        pytest.skip(str(error))


@pytest.fixture(scope="session")
def amqp091_driver() -> MessagingDriver:
    return _driver("CNETMOD_AMQP091_INTEROP_DRIVER", "amqp091")


@pytest.fixture(scope="session")
def amqp10_driver() -> MessagingDriver:
    return _driver("CNETMOD_AMQP10_INTEROP_DRIVER", "amqp10")


@pytest.fixture(scope="session")
def kafka_driver() -> MessagingDriver:
    return _driver("CNETMOD_KAFKA_INTEROP_DRIVER", "kafka")


@pytest.fixture(scope="session")
def rabbitmq_service(amqp091_driver: MessagingDriver):
    mode = _service_mode()
    external = _amqp091_external_endpoint() if mode != "container" else None
    if external is not None:
        yield None, external
        return
    if mode == "external":
        pytest.skip("AMQP 0-9-1 external endpoint is not configured")
    _require_docker_or_skip("AMQP 0-9-1")
    service = RabbitMqService()
    endpoint = service.start()
    yield service, endpoint
    service.stop()


@pytest.fixture(scope="session")
def artemis_service(amqp10_driver: MessagingDriver):
    mode = _service_mode()
    external = _amqp10_external_endpoint() if mode != "container" else None
    if external is not None:
        yield None, external
        return
    if mode == "external":
        pytest.skip("AMQP 1.0 external endpoint is not configured")
    _require_docker_or_skip("AMQP 1.0")
    service = ArtemisService()
    endpoint = service.start()
    yield service, endpoint
    service.stop()


@pytest.fixture(scope="session")
def kafka_service(kafka_driver: MessagingDriver):
    mode = _service_mode()
    external = _kafka_external_endpoint() if mode != "container" else None
    if external is not None:
        yield None, external
        return
    if mode == "external":
        pytest.skip("Kafka external endpoint is not configured")
    _require_docker_or_skip("Kafka")
    service = KafkaService()
    endpoint = service.start()
    yield service, endpoint
    service.stop()


def _security_endpoint(prefix: str):
    required = tuple(
        f"CNETMOD_{prefix}_{suffix}"
        for suffix in ("HOST", "PORT", "USERNAME", "PASSWORD", "CA_FILE")
    )
    missing = [name for name in required if not os.environ.get(name)]
    if missing:
        pytest.skip("security endpoint is not configured: " + ", ".join(missing))
    return {
        "host": os.environ[f"CNETMOD_{prefix}_HOST"],
        "port": int(os.environ[f"CNETMOD_{prefix}_PORT"]),
        "username": os.environ[f"CNETMOD_{prefix}_USERNAME"],
        "password": os.environ[f"CNETMOD_{prefix}_PASSWORD"],
        "ca_file": os.environ[f"CNETMOD_{prefix}_CA_FILE"],
    }


@pytest.fixture
def amqp091_security_endpoint():
    return _security_endpoint("AMQP091_SECURITY")


@pytest.fixture
def amqp10_security_endpoint():
    return _security_endpoint("AMQP10_SECURITY")


@pytest.fixture
def kafka_security_endpoint():
    return _security_endpoint("KAFKA_SECURITY")
