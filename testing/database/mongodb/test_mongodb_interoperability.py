from __future__ import annotations

import os
import time
import uuid
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager

import pytest
from pymongo import MongoClient
from pymongo.errors import AutoReconnect, OperationFailure


pytestmark = pytest.mark.mongodb


def test_pymongo_reference_round_trip(mongodb_uri: str) -> None:
    marker = f"python-{uuid.uuid4()}"
    with MongoClient(mongodb_uri, serverSelectionTimeoutMS=10_000) as client:
        reply = client.admin.command({"ping": 1, "comment": marker})
    assert reply["ok"] == 1


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_connect_command_and_bson(
    mongodb_native_options: dict[str, object], mongodb_driver
) -> None:
    marker = f"cnetmod-文档-{uuid.uuid4()}"
    result = mongodb_driver.request(
        "round_trip", **mongodb_native_options, marker=marker
    )
    assert result["marker"] == marker
    assert result["ping_ok"] is True
    assert result["request_id_correlated"] is True


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_concurrent_connections(
    mongodb_native_options: dict[str, object], mongodb_driver
) -> None:
    markers = [f"worker-{index}-{uuid.uuid4()}" for index in range(16)]

    def invoke(marker: str) -> str:
        return mongodb_driver.request(
            "round_trip", **mongodb_native_options, marker=marker
        )["marker"]

    with ThreadPoolExecutor(max_workers=8) as workers:
        assert list(workers.map(invoke, markers)) == markers


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_rejects_unreachable_endpoint(mongodb_driver) -> None:
    with pytest.raises(Exception, match="connect|transport|timed out"):
        mongodb_driver.request(
            "connect_failure",
            host="127.0.0.1",
            port=1,
            connect_timeout_milliseconds=250,
            timeout_seconds=5,
        )


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_complete_bson_server_round_trip(
    mongodb_native_options: dict[str, object], mongodb_driver
) -> None:
    result = mongodb_driver.request(
        "bson_types", **mongodb_native_options, marker=f"bson-{uuid.uuid4()}"
    )
    assert result["all_types_round_tripped"] is True
    assert result["field_count"] == 23


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_pool_wait_queue_timeout(
    mongodb_native_options: dict[str, object], mongodb_driver
) -> None:
    result = mongodb_driver.request(
        "pool_wait_timeout",
        **mongodb_native_options,
        pool_wait_timeout_milliseconds=150,
    )
    assert result["timed_out"] is True
    assert result["checked_out"] == 1
    assert 100 <= result["elapsed_milliseconds"] < 2_000


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_pool_close_cancels_waiter(
    mongodb_native_options: dict[str, object], mongodb_driver
) -> None:
    result = mongodb_driver.request(
        "pool_wait_cancel",
        **mongodb_native_options,
        cancel_after_milliseconds=100,
    )
    assert result["cancelled"] is True
    assert 50 <= result["elapsed_milliseconds"] < 2_000


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_pool_cancels_only_targeted_waiter(
    mongodb_native_options: dict[str, object], mongodb_driver
) -> None:
    result = mongodb_driver.request(
        "pool_wait_targeted_cancel",
        **mongodb_native_options,
        cancel_after_milliseconds=100,
    )
    assert result["cancelled"] is True
    assert result["pool_still_open"] is True
    assert 50 <= result["elapsed_milliseconds"] < 2_000


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_retryable_read_write(
    mongodb_uri: str,
    mongodb_native_options: dict[str, object],
    mongodb_driver,
) -> None:
    marker = f"retry-{uuid.uuid4()}"
    result = mongodb_driver.request(
        "retryable_read_write", **mongodb_native_options, marker=marker
    )
    assert result == {"write_ok": True, "read_marker": marker}
    with MongoClient(mongodb_uri) as client:
        client.get_database(str(mongodb_native_options["database"]))[
            "cnetmod_retryable_interop"
        ].delete_one({"_id": marker})


def _require_replica_set(client: MongoClient) -> None:
    if "setName" not in client.admin.command({"hello": 1}):
        pytest.skip("MongoDB transactions and change streams require a replica set")


def _topology_parameters(
    mongodb_uri: str, mongodb_native_options: dict[str, object]
) -> dict[str, object]:
    from pymongo.uri_parser import parse_uri

    parsed = parse_uri(mongodb_uri)

    def address(host: str, port: int) -> str:
        return f"[{host}]:{port}" if ":" in host else f"{host}:{port}"

    result: dict[str, object] = {
        **mongodb_native_options,
        "seeds": [address(host, port) for host, port in parsed["nodelist"]],
    }
    uri_options = {str(key).lower(): value for key, value in parsed["options"].items()}
    if replica_set_name := uri_options.get("replicaset"):
        result["replica_set_name"] = replica_set_name
    return result


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_topology_discovery_selects_writable_server(
    mongodb_uri: str,
    mongodb_native_options: dict[str, object],
    mongodb_driver,
) -> None:
    result = mongodb_driver.request(
        "topology_status", **_topology_parameters(mongodb_uri, mongodb_native_options)
    )
    assert result["servers"]
    assert any(server["readable"] for server in result["servers"])
    assert any(server["writable"] for server in result["servers"])


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_transaction_commit_and_abort_visibility(
    mongodb_uri: str,
    mongodb_native_options: dict[str, object],
    mongodb_driver,
) -> None:
    committed = f"commit-{uuid.uuid4()}"
    aborted = f"abort-{uuid.uuid4()}"
    with MongoClient(mongodb_uri) as client:
        _require_replica_set(client)
    result = mongodb_driver.request(
        "transaction_commit_abort",
        **mongodb_native_options,
        committed_marker=committed,
        aborted_marker=aborted,
    )
    assert result == {
        "committed": committed,
        "aborted": aborted,
        "commit_retried": False,
    }
    with MongoClient(mongodb_uri) as client:
        collection = client.get_database(str(mongodb_native_options["database"]))[
            "cnetmod_transaction_interop"
        ]
        assert collection.count_documents({"_id": committed}) == 1
        assert collection.count_documents({"_id": aborted}) == 0
        collection.delete_one({"_id": committed})


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_retries_unknown_transaction_commit_result(
    mongodb_uri: str,
    mongodb_native_options: dict[str, object],
    mongodb_driver,
) -> None:
    if os.environ.get("CNETMOD_MONGODB_FAILPOINT_TEST") != "1":
        pytest.skip("set CNETMOD_MONGODB_FAILPOINT_TEST=1 on a testCommands server")
    committed = f"retry-commit-{uuid.uuid4()}"
    aborted = f"retry-abort-{uuid.uuid4()}"
    with MongoClient(mongodb_uri) as client:
        _require_replica_set(client)
    with _close_first_command(mongodb_uri, "commitTransaction"):
        result = mongodb_driver.request(
            "transaction_commit_abort",
            **mongodb_native_options,
            committed_marker=committed,
            aborted_marker=aborted,
            inject_commit_disconnect=True,
        )
    assert result == {
        "committed": committed,
        "aborted": aborted,
        "commit_retried": True,
    }
    with MongoClient(mongodb_uri) as client:
        collection = client.get_database(str(mongodb_native_options["database"]))[
            "cnetmod_transaction_interop"
        ]
        assert collection.count_documents({"_id": committed}) == 1
        assert collection.count_documents({"_id": aborted}) == 0
        collection.delete_one({"_id": committed})


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_change_stream_resume_token(
    mongodb_uri: str,
    mongodb_native_options: dict[str, object],
    mongodb_driver,
) -> None:
    first = f"change-first-{uuid.uuid4()}"
    second = f"change-second-{uuid.uuid4()}"
    with MongoClient(mongodb_uri) as client:
        _require_replica_set(client)
    result = mongodb_driver.request(
        "change_stream_resume",
        timeout_seconds=30,
        **mongodb_native_options,
        first_marker=first,
        second_marker=second,
    )
    assert result == {
        "first_marker": first,
        "resumed_marker": second,
        "resume_token_present": True,
    }
    with MongoClient(mongodb_uri) as client:
        client.get_database(str(mongodb_native_options["database"]))[
            "cnetmod_change_stream_interop"
        ].delete_many({"marker": {"$in": [first, second]}})


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_change_stream_automatically_resumes_after_disconnect(
    mongodb_uri: str,
    mongodb_native_options: dict[str, object],
    mongodb_driver,
) -> None:
    if os.environ.get("CNETMOD_MONGODB_FAILPOINT_TEST") != "1":
        pytest.skip("set CNETMOD_MONGODB_FAILPOINT_TEST=1 on a testCommands server")
    first = f"auto-resume-first-{uuid.uuid4()}"
    second = f"auto-resume-second-{uuid.uuid4()}"
    with MongoClient(mongodb_uri) as client:
        _require_replica_set(client)
    with _close_first_command(mongodb_uri, "getMore"):
        result = mongodb_driver.request(
            "change_stream_resume",
            timeout_seconds=30,
            **mongodb_native_options,
            first_marker=first,
            second_marker=second,
            inject_get_more_disconnect=True,
        )
    assert result == {
        "first_marker": first,
        "resumed_marker": second,
        "resume_token_present": True,
        "automatic_resume": True,
    }
    with MongoClient(mongodb_uri) as client:
        client.get_database(str(mongodb_native_options["database"]))[
            "cnetmod_change_stream_interop"
        ].delete_many({"marker": {"$in": [first, second]}})


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_zlib_negotiation_and_uncompressed_fallback(
    mongodb_native_options: dict[str, object], mongodb_driver
) -> None:
    zlib_result = mongodb_driver.request(
        "round_trip",
        **mongodb_native_options,
        marker=f"zlib-{uuid.uuid4()}",
        enable_zlib_compression=True,
        compression_minimum_bytes=0,
    )
    plain_result = mongodb_driver.request(
        "round_trip",
        **mongodb_native_options,
        marker=f"plain-{uuid.uuid4()}",
        enable_zlib_compression=False,
    )
    assert zlib_result["compressor"] == "zlib"
    assert plain_result["compressor"] == "none"


@contextmanager
def _blocked_ping(mongodb_uri: str, milliseconds: int = 2_000):
    with MongoClient(mongodb_uri, serverSelectionTimeoutMS=10_000) as client:
        client.admin.command(
            {
                "configureFailPoint": "failCommand",
                "mode": {"times": 1},
                "data": {
                    "failCommands": ["ping"],
                    "blockConnection": True,
                    "blockTimeMS": milliseconds,
                },
            }
        )
        try:
            yield
        finally:
            client.admin.command({"configureFailPoint": "failCommand", "mode": "off"})


@contextmanager
def _close_first_command(mongodb_uri: str, command_name: str):
    with MongoClient(mongodb_uri, serverSelectionTimeoutMS=10_000) as client:
        client.admin.command(
            {
                "configureFailPoint": "failCommand",
                "mode": {"times": 1},
                "data": {
                    "failCommands": [command_name],
                    "closeConnection": True,
                },
            }
        )
        try:
            yield
        finally:
            client.admin.command({"configureFailPoint": "failCommand", "mode": "off"})


@pytest.mark.cnetmod_driver
@pytest.mark.parametrize("failed_command", ["insert", "find"])
def test_cnetmod_mongodb_retries_transient_read_and_write_disconnects(
    failed_command: str,
    mongodb_uri: str,
    mongodb_native_options: dict[str, object],
    mongodb_driver,
) -> None:
    if os.environ.get("CNETMOD_MONGODB_FAILPOINT_TEST") != "1":
        pytest.skip("set CNETMOD_MONGODB_FAILPOINT_TEST=1 on a testCommands server")
    marker = f"retry-{failed_command}-{uuid.uuid4()}"
    with _close_first_command(mongodb_uri, failed_command):
        result = mongodb_driver.request(
            "retryable_read_write", **mongodb_native_options, marker=marker
        )
    assert result == {"write_ok": True, "read_marker": marker}
    with MongoClient(mongodb_uri) as client:
        client.get_database(str(mongodb_native_options["database"]))[
            "cnetmod_retryable_interop"
        ].delete_one({"_id": marker})


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_command_timeout_closes_tainted_connection(
    mongodb_uri: str,
    mongodb_native_options: dict[str, object],
    mongodb_driver,
) -> None:
    if os.environ.get("CNETMOD_MONGODB_FAILPOINT_TEST") != "1":
        pytest.skip("set CNETMOD_MONGODB_FAILPOINT_TEST=1 on a testCommands server")
    with _blocked_ping(mongodb_uri):
        result = mongodb_driver.request(
            "timeout_probe",
            **mongodb_native_options,
            command_timeout_milliseconds=100,
        )
    assert result == {"timed_out": True, "connection_closed": True}


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_explicit_command_cancellation(
    mongodb_uri: str,
    mongodb_native_options: dict[str, object],
    mongodb_driver,
) -> None:
    if os.environ.get("CNETMOD_MONGODB_FAILPOINT_TEST") != "1":
        pytest.skip("set CNETMOD_MONGODB_FAILPOINT_TEST=1 on a testCommands server")
    with _blocked_ping(mongodb_uri):
        result = mongodb_driver.request(
            "cancel_probe",
            **mongodb_native_options,
            cancel_after_milliseconds=100,
            command_timeout_milliseconds=5_000,
        )
    assert result == {"cancelled": True, "connection_closed": True}


@pytest.mark.cnetmod_driver
def test_cnetmod_mongodb_three_node_primary_failover(
    mongodb_uri: str,
    mongodb_native_options: dict[str, object],
    mongodb_driver,
) -> None:
    if os.environ.get("CNETMOD_MONGODB_FAILOVER_TEST") != "1":
        pytest.skip("set CNETMOD_MONGODB_FAILOVER_TEST=1 for destructive primary step-down")
    from pymongo.uri_parser import parse_uri

    parsed = parse_uri(mongodb_uri)
    if len(parsed["nodelist"]) < 3:
        pytest.skip("three-node replica set URI required")
    uri_options = {str(key).lower(): value for key, value in parsed["options"].items()}
    replica_set_name = uri_options.get("replicaset")
    if not replica_set_name:
        pytest.skip("replicaSet URI option required")

    parameters = {
        **_topology_parameters(mongodb_uri, mongodb_native_options),
        "replica_set_name": replica_set_name,
        "duration_milliseconds": 25_000,
        "interval_milliseconds": 150,
    }
    with ThreadPoolExecutor(max_workers=1) as executor:
        watching = executor.submit(
            mongodb_driver.request, "failover_watch", timeout_seconds=40, **parameters
        )
        time.sleep(3)
        with MongoClient(mongodb_uri, serverSelectionTimeoutMS=10_000) as client:
            try:
                client.admin.command({"replSetStepDown": 8, "force": True})
            except (AutoReconnect, OperationFailure):
                pass
        result = watching.result(timeout=40)
    assert result["successful_writes"] > 0
    assert len(result["observed_primaries"]) >= 2
