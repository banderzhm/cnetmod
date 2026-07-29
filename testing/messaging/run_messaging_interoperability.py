"""CTest entry point with a clear skip result for unavailable infrastructure."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path


SKIP_RETURN_CODE = 77


def _external_endpoint_exists() -> bool:
    return any(
        os.environ.get(f"CNETMOD_{protocol}_HOST")
        and os.environ.get(f"CNETMOD_{protocol}_PORT")
        for protocol in ("AMQP091", "AMQP10", "KAFKA")
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
    required_modules = (
        "pytest",
        "testcontainers",
        "aio_pika",
        "pika",
        "proton",
        "confluent_kafka",
        "docker",
        "dotenv",
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
    external_exists = _external_endpoint_exists()
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
            str(directory),
            "--strict-markers",
        ]
    )


if __name__ == "__main__":
    raise SystemExit(main())
