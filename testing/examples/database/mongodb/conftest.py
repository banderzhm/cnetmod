from __future__ import annotations

import os
import uuid
from pathlib import Path

import pytest
from pymongo import MongoClient
from pymongo.errors import ConfigurationError, OperationFailure, ServerSelectionTimeoutError
from pymongo.uri_parser import parse_uri

from database.common.service_process import required_executable, run_scenario, wait_until


class _Secret(str):
    """A string whose pytest/debug representation never exposes its value."""

    def __repr__(self) -> str:
        return "<redacted>"


def _required(name: str) -> _Secret:
    value = os.environ.get(name)
    if not value:
        pytest.skip(f"set {name} to run MongoDB production example E2E tests")
    return _Secret(value)


def _address(host: str, port: int) -> str:
    return f"[{host}]:{port}" if ":" in host else f"{host}:{port}"


@pytest.fixture(scope="session")
def mongodb_example_executable() -> Path:
    try:
        return required_executable("CNETMOD_MONGODB_EXAMPLE")
    except FileNotFoundError as error:
        pytest.skip(str(error))


@pytest.fixture(scope="session")
def mongodb_uri() -> str:
    return _required("CNETMOD_MONGODB_URI")


@pytest.fixture(scope="session")
def mongodb_example_environment(mongodb_uri: str) -> dict[str, str]:
    try:
        parsed = parse_uri(mongodb_uri)
    except (ConfigurationError, ValueError):
        raise RuntimeError(
            "CNETMOD_MONGODB_URI is not a valid MongoDB connection URI"
        ) from None
    options = {str(key).lower(): value for key, value in parsed["options"].items()}
    return {
        "CNETMOD_MONGODB_SEEDS": ",".join(
            _address(host, port) for host, port in parsed["nodelist"]
        ),
        "CNETMOD_MONGODB_REPLICA_SET": str(options.get("replicaset", "")),
        "CNETMOD_MONGODB_USERNAME": parsed.get("username") or "",
        "CNETMOD_MONGODB_PASSWORD": parsed.get("password") or "",
        "CNETMOD_MONGODB_DATABASE": "cnetmod_interop",
        "CNETMOD_MONGODB_AUTH_DATABASE": str(options.get("authsource", "admin")),
        "CNETMOD_MONGODB_TLS": "true" if options.get("tls", False) else "false",
        "CNETMOD_MONGODB_CA_FILE": str(options.get("tlscafile", "")),
        "CNETMOD_MONGODB_POOL_MIN": "2",
        "CNETMOD_MONGODB_POOL_MAX": "32",
        "CNETMOD_MONGODB_POOL_MAX_CONNECTING": "4",
        "CNETMOD_MONGODB_WAIT_TIMEOUT_MS": "2000",
        "CNETMOD_MONGODB_WORKERS": "32",
        "CNETMOD_MONGODB_REQUESTS": "5000",
        "CNETMOD_MONGODB_QUEUE_CAPACITY": "2048",
        "CNETMOD_MONGODB_HEARTBEAT_MS": "500",
        "CNETMOD_MONGODB_HEALTH_MS": "500",
        "CNETMOD_MONGODB_SHUTDOWN_MS": "10000",
    }


@pytest.fixture(scope="session")
def mongodb_client(mongodb_uri: str):
    client = MongoClient(mongodb_uri, serverSelectionTimeoutMS=10_000)
    connection_failed = False
    try:
        client.admin.command({"ping": 1})
    except (OperationFailure, ServerSelectionTimeoutError):
        connection_failed = True
    if connection_failed:
        client.close()
        pytest.fail(
            "MongoDB test deployment is unreachable or rejected authentication "
            "(connection details redacted)",
            pytrace=False,
        )
    try:
        yield client
    finally:
        client.close()


@pytest.fixture
def mongodb_scenario(
    mongodb_example_executable: Path,
    mongodb_example_environment: dict[str, str],
    mongodb_client: MongoClient,
):
    results = mongodb_client["cnetmod_interop"]["cnetmod_example_test_results"]
    run_ids: list[str] = []

    def execute(
        scenario: str,
        *,
        environment: dict[str, str] | None = None,
        timeout_seconds: float = 90,
    ) -> dict:
        run_id = f"mongo-example-{uuid.uuid4()}"
        run_ids.append(run_id)
        result_filter = {"run_id": run_id, "scenario": scenario}
        results.delete_many(result_filter)
        run_scenario(
            mongodb_example_executable,
            {
                **mongodb_example_environment,
                **(environment or {}),
                "CNETMOD_MONGODB_TEST_RUN_ID": run_id,
            },
            None if scenario == "serve" else scenario,
            timeout_seconds,
        )
        return wait_until(
            lambda: results.find_one(result_filter),
            10,
            f"MongoDB {scenario} scenario result",
        )

    yield execute

    results.delete_many({"run_id": {"$in": run_ids}})
