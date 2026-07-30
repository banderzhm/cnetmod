"""CTest entry point with a clear skip result for unavailable infrastructure."""

from __future__ import annotations

import importlib.util
import os
import sys
from pathlib import Path


SKIP_RETURN_CODE = 77


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
        client.ping()
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
        print(
            "SKIP: messaging interoperability dependencies are not installed: "
            + ", ".join(missing)
        )
        return SKIP_RETURN_CODE
    from dotenv import load_dotenv

    load_dotenv(Path(__file__).resolve().parent / ".env.external.local", override=False)
    mode = os.environ.get("CNETMOD_MESSAGING_SERVICE_MODE", "auto").lower()
    if mode not in {"auto", "container", "external"}:
        print("ERROR: CNETMOD_MESSAGING_SERVICE_MODE must be auto, container, or external")
        return 2
    external_exists = _external_endpoint_exists(protocol)
    docker_usable = False if mode == "external" else _docker_is_usable()
    if mode == "container" and not docker_usable:
        print("SKIP: container mode requires a usable Docker engine")
        return SKIP_RETURN_CODE
    if mode == "external" and not external_exists:
        print("SKIP: external mode requires at least one configured broker endpoint")
        return SKIP_RETURN_CODE
    if mode == "auto" and not external_exists and not docker_usable:
        print(
            "SKIP: messaging interoperability requires Docker or an external "
            "AMQP/Kafka endpoint"
        )
        return SKIP_RETURN_CODE

    import pytest

    directory = Path(__file__).resolve().parent
    return pytest.main(
        [
            "-c",
            str(directory / "pytest.ini"),
            str(directory / protocol),
            "--strict-markers",
        ]
    )


if __name__ == "__main__":
    raise SystemExit(main())
