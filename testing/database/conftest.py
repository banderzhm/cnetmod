"""Shared environment-only configuration for live database tests."""

from __future__ import annotations

import os
from urllib.parse import parse_qs, unquote, urlsplit

import pytest

from database_driver_process import DatabaseDriver


class _Secret(str):
    """A string whose pytest/debug representation never exposes its value."""

    def __repr__(self) -> str:
        return "<redacted>"


def _required_environment(name: str) -> _Secret:
    value = os.environ.get(name)
    if not value:
        pytest.skip(f"set {name} to run this live interoperability test")
    return _Secret(value)


@pytest.fixture(scope="session")
def postgresql_uri() -> str:
    return _required_environment("CNETMOD_POSTGRESQL_URI")


@pytest.fixture(scope="session")
def mongodb_uri() -> str:
    return _required_environment("CNETMOD_MONGODB_URI")


@pytest.fixture(scope="session")
def postgresql_native_options(postgresql_uri: str) -> dict[str, object]:
    parsed = urlsplit(postgresql_uri)
    if parsed.scheme not in {"postgresql", "postgres"}:
        raise ValueError("CNETMOD_POSTGRESQL_URI must use postgresql://")
    query = parse_qs(parsed.query)
    return {
        "host": parsed.hostname or "localhost",
        "port": parsed.port or 5432,
        "username": unquote(parsed.username or "postgres"),
        "password": _Secret(unquote(parsed.password or "")),
        "database": unquote(parsed.path.removeprefix("/") or "postgres"),
        "tls_mode": query.get("sslmode", ["prefer"])[0],
    }


@pytest.fixture(scope="session")
def mongodb_native_options(mongodb_uri: str) -> dict[str, object]:
    from pymongo import MongoClient
    from pymongo.errors import (
        ConfigurationError,
        OperationFailure,
        ServerSelectionTimeoutError,
    )
    from pymongo.uri_parser import parse_uri

    if mongodb_uri.lower().startswith("mongodb+srv://"):
        pytest.skip("the physical-connection driver does not perform SRV discovery")
    try:
        parsed = parse_uri(mongodb_uri)
    except (ConfigurationError, ValueError):
        raise RuntimeError(
            "CNETMOD_MONGODB_URI is not a valid MongoDB connection URI"
        ) from None
    connection_failed = False
    address = None
    with MongoClient(mongodb_uri, serverSelectionTimeoutMS=10_000) as client:
        try:
            client.admin.command({"ping": 1})
            address = client.primary or client.address
        except (OperationFailure, ServerSelectionTimeoutError):
            connection_failed = True
    if connection_failed:
        pytest.fail(
            "MongoDB test deployment is unreachable or rejected authentication "
            "(connection details redacted)",
            pytrace=False,
        )
    if address is None:
        pytest.skip("MongoDB deployment has no selectable server")
    host, port = address
    options = parsed["options"]
    normalized_options = {str(key).lower(): value for key, value in options.items()}
    return {
        "host": host,
        "port": port,
        "username": parsed.get("username") or "",
        "password": _Secret(parsed.get("password") or ""),
        # Keep all mutable interoperability fixtures isolated from application
        # databases even when the authentication URI names another database.
        "database": "cnetmod_interop",
        "authentication_database": normalized_options.get("authsource", "admin"),
        "tls": bool(normalized_options.get("tls", False)),
        "tls_verify": not bool(
            normalized_options.get("tlsallowinvalidcertificates", False)
        ),
        "tls_ca_file": str(normalized_options.get("tlscafile", "")),
    }


@pytest.fixture(scope="session")
def postgresql_driver() -> DatabaseDriver:
    try:
        return DatabaseDriver.from_environment(
            "CNETMOD_POSTGRESQL_DRIVER", "postgresql"
        )
    except FileNotFoundError as error:
        pytest.skip(str(error))


@pytest.fixture(scope="session")
def mongodb_driver() -> DatabaseDriver:
    try:
        return DatabaseDriver.from_environment("CNETMOD_MONGODB_DRIVER", "mongodb")
    except FileNotFoundError as error:
        pytest.skip(str(error))
