from __future__ import annotations

import uuid
from concurrent.futures import ThreadPoolExecutor

import psycopg
import pytest


pytestmark = pytest.mark.postgresql


def test_psycopg_reference_round_trip(postgresql_uri: str) -> None:
    marker = f"python-{uuid.uuid4()}"
    with psycopg.connect(postgresql_uri, connect_timeout=10, autocommit=True) as connection:
        with connection.cursor() as cursor:
            cursor.execute("SELECT %s::text, current_setting('server_version_num')::int", (marker,))
            returned_marker, version_number = cursor.fetchone()
    assert returned_marker == marker
    assert version_number >= 120000


@pytest.mark.cnetmod_driver
def test_cnetmod_postgresql_connect_query_and_utf8(
    postgresql_native_options: dict[str, object], postgresql_driver
) -> None:
    marker = f"cnetmod-数据库-{uuid.uuid4()}"
    result = postgresql_driver.request(
        "round_trip", **postgresql_native_options, marker=marker
    )
    assert result["marker"] == marker
    assert result["ready_for_query"] is True
    assert int(result["server_version_number"]) >= 120000


@pytest.mark.cnetmod_driver
def test_cnetmod_postgresql_concurrent_connections(
    postgresql_native_options: dict[str, object], postgresql_driver
) -> None:
    markers = [f"worker-{index}-{uuid.uuid4()}" for index in range(16)]

    def invoke(marker: str) -> str:
        return postgresql_driver.request(
            "round_trip", **postgresql_native_options, marker=marker
        )["marker"]

    with ThreadPoolExecutor(max_workers=8) as workers:
        assert list(workers.map(invoke, markers)) == markers


@pytest.mark.cnetmod_driver
def test_cnetmod_postgresql_rejects_unreachable_endpoint(postgresql_driver) -> None:
    with pytest.raises(Exception, match="connect|transport|timed out|refused"):
        postgresql_driver.request(
            "connect_failure",
            host="127.0.0.1",
            port=1,
            username="invalid",
            password="invalid",
            database="invalid",
            connect_timeout_milliseconds=250,
            timeout_seconds=5,
        )
