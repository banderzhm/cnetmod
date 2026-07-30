from __future__ import annotations

import os
import socket
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlsplit

import pytest

from database.common.service_process import ExampleProcess, required_executable, wait_until


def _required(name: str) -> str:
    value = os.environ.get(name)
    if not value:
        pytest.skip(f"set {name} to run PostgreSQL production example E2E tests")
    return value


def _free_port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


@pytest.fixture(scope="session")
def postgresql_example_executable() -> Path:
    try:
        return required_executable("CNETMOD_POSTGRESQL_EXAMPLE")
    except FileNotFoundError as error:
        pytest.skip(str(error))


@pytest.fixture(scope="session")
def postgresql_example_environment() -> dict[str, str]:
    parsed = urlsplit(_required("CNETMOD_POSTGRESQL_URI"))
    if parsed.scheme not in {"postgres", "postgresql"}:
        raise ValueError("CNETMOD_POSTGRESQL_URI must use postgresql://")
    query = parse_qs(parsed.query)
    host = parsed.hostname or "127.0.0.1"
    port = parsed.port or 5432
    return {
        "CNETMOD_POSTGRESQL_ENDPOINTS": f"{host}:{port}",
        "CNETMOD_POSTGRESQL_USERNAME": unquote(parsed.username or "postgres"),
        "CNETMOD_POSTGRESQL_PASSWORD": unquote(parsed.password or ""),
        "CNETMOD_POSTGRESQL_DATABASE": unquote(
            parsed.path.removeprefix("/") or "cnetmod_interop"
        ),
        "CNETMOD_POSTGRESQL_CA_FILE": query.get("sslrootcert", [""])[0],
        "CNETMOD_POSTGRESQL_POOL_MIN": "2",
        "CNETMOD_POSTGRESQL_POOL_MAX": "32",
        "CNETMOD_POSTGRESQL_ACQUIRE_TIMEOUT_MS": "2000",
        "CNETMOD_POSTGRESQL_FAILOVER_ATTEMPTS": "5",
        "CNETMOD_POSTGRESQL_RETRY_BACKOFF_MS": "100",
        "CNETMOD_POSTGRESQL_HTTP_HOST": "127.0.0.1",
        "CNETMOD_POSTGRESQL_STATEMENT_TIMEOUT_MS": "10000",
        "CNETMOD_POSTGRESQL_LOCK_TIMEOUT_MS": "3000",
        "CNETMOD_POSTGRESQL_IDLE_TRANSACTION_TIMEOUT_MS": "10000",
        "CNETMOD_POSTGRESQL_SHUTDOWN_GRACE_MS": "10000",
        "CNETMOD_EXAMPLE_ENABLE_SHUTDOWN": "true",
    }


@pytest.fixture
def postgresql_service_factory(
    postgresql_example_executable: Path,
    postgresql_example_environment: dict[str, str],
):
    processes: list[ExampleProcess] = []

    def start(*, endpoints: str | None = None) -> tuple[ExampleProcess, str]:
        port = _free_port()
        environment = {
            **postgresql_example_environment,
            "CNETMOD_POSTGRESQL_HTTP_PORT": str(port),
        }
        if endpoints is not None:
            environment["CNETMOD_POSTGRESQL_ENDPOINTS"] = endpoints
        process = ExampleProcess(postgresql_example_executable, environment).start()
        processes.append(process)
        base_url = f"http://127.0.0.1:{port}"

        def ready() -> bool:
            if process.poll() is not None:
                raise RuntimeError(f"service exited during startup: {process.log_tail()}")
            response = process.request(base_url, "GET", "/health/ready", timeout_seconds=1)
            data = response.body.get("data") or {}
            return response.status == 200 and data.get("status") in {"UP", "ready"}

        wait_until(ready, 20, "PostgreSQL example readiness")
        return process, base_url

    yield start

    for process in processes:
        process.stop()
