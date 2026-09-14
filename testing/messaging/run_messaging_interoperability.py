"""CTest entry point with a clear skip result for unavailable infrastructure."""

from __future__ import annotations

import importlib.util
import os
import sys
from pathlib import Path


SKIP_RETURN_CODE = 77


class _RequiredExecutionGate:
    """Fail a required interoperability run when pytest skips any test."""

    def __init__(self) -> None:
        self.skipped = False

    def pytest_runtest_logreport(self, report) -> None:
        self.skipped |= report.skipped

    def pytest_collectreport(self, report) -> None:
        self.skipped |= report.skipped

    def pytest_sessionfinish(self, session, exitstatus) -> None:
        if self.skipped and exitstatus == 0:
            session.exitstatus = 1


def _unavailable(message: str) -> int:
    required = os.environ.get("CNETMOD_MESSAGING_REQUIRED") == "1"
    print(("ERROR: " if required else "SKIP: ") + message)
    return 1 if required else SKIP_RETURN_CODE


def _external_endpoint_exists(protocol: str) -> bool:
    prefix = protocol.upper()
    return bool(
        os.environ.get(f"CNETMOD_{prefix}_HOST")
        and os.environ.get(f"CNETMOD_{prefix}_PORT")
    )


def _docker_is_usable() -> bool:
    try:
        from docker import from_env as docker_from_environment

        client = docker_from_environment()
        try:
            client.ping()
        finally:
            client.close()
        return True
    except Exception:
        return False


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in {"amqp091", "amqp10", "kafka"}:
        print("ERROR: expected one protocol: amqp091, amqp10, or kafka")
        return 2
    protocol = sys.argv[1]
    protocol_modules = {
        "amqp091": ("aio_pika", "pika"),
        "amqp10": ("proton",),
        "kafka": ("confluent_kafka",),
    }
    required_modules = (
        "pytest",
        "testcontainers",
        "docker",
        "dotenv",
        *protocol_modules[protocol],
    )
    missing = [name for name in required_modules if importlib.util.find_spec(name) is None]
    if missing:
        return _unavailable(
            "messaging interoperability dependencies are not installed: "
            + ", ".join(missing)
        )
    from dotenv import load_dotenv

    load_dotenv(Path(__file__).resolve().parent / ".env.external.local", override=False)
    mode = os.environ.get("CNETMOD_MESSAGING_SERVICE_MODE", "auto").lower()
    if mode not in {"auto", "container", "external"}:
        print("ERROR: CNETMOD_MESSAGING_SERVICE_MODE must be auto, container, or external")
        return 2
    external_exists = _external_endpoint_exists(protocol)
    docker_usable = False if mode == "external" else _docker_is_usable()
    if mode == "container" and not docker_usable:
        return _unavailable("container mode requires a usable Docker engine")
    if mode == "external" and not external_exists:
        return _unavailable("external mode requires a configured broker endpoint")
    if mode == "auto" and not external_exists and not docker_usable:
        return _unavailable(
            "messaging interoperability requires Docker or an external "
            "AMQP/Kafka endpoint"
        )

    import pytest

    directory = Path(__file__).resolve().parent
    required = os.environ.get("CNETMOD_MESSAGING_REQUIRED") == "1"
    plugins = [_RequiredExecutionGate()] if required else []
    return pytest.main(
        [
            "-c",
            str(directory / "pytest.ini"),
            str(directory / protocol),
            "--strict-markers",
        ],
        plugins=plugins,
    )


if __name__ == "__main__":
    raise SystemExit(main())
