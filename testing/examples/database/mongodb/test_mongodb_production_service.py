from __future__ import annotations

import os
import time
import uuid

import pytest
from pymongo import MongoClient
from pymongo.errors import AutoReconnect, OperationFailure

from database.common.service_process import ExampleProcess, wait_until


pytestmark = pytest.mark.mongodb_example


def _passed(result: dict) -> dict:
    assert result["status"] == "passed", result.get("error")
    return result.get("metrics", {})


def test_health_readiness_and_graceful_drain(mongodb_scenario) -> None:
    health = mongodb_scenario("health")
    _passed(health)
    assert health["ready"] is True

    served = mongodb_scenario(
        "serve",
        environment={
            "CNETMOD_MONGODB_WORKERS": "32",
            "CNETMOD_MONGODB_REQUESTS": "5000",
        },
        timeout_seconds=120,
    )
    _passed(served)
    assert served["completed"] is True
    assert served["completed_count"] == 5000
    assert served.get("graceful_shutdown") is True


def test_crud_and_transaction_commit_abort(mongodb_scenario) -> None:
    crud = _passed(mongodb_scenario("repository"))
    assert crud["created"] > 0
    assert crud["read"] == crud["created"]
    assert crud["updated"] == crud["created"]
    assert crud["deleted"] == crud["created"]

    transactions = _passed(mongodb_scenario("transaction"))
    assert transactions["committed"] > 0
    assert transactions["aborted"] > 0
    assert transactions.get("aborted_visible", 0) == 0


def test_change_stream_delivers_and_resumes(mongodb_scenario) -> None:
    metrics = _passed(mongodb_scenario("change-stream", timeout_seconds=120))
    assert metrics["events"] > 0
    assert metrics["resumed"] > 0
    assert metrics.get("duplicates", 0) == 0


@pytest.mark.destructive
def test_primary_stepdown_keeps_high_availability(
    mongodb_uri: str,
    mongodb_example_executable,
    mongodb_example_environment: dict[str, str],
    mongodb_client: MongoClient,
) -> None:
    if os.environ.get("CNETMOD_MONGODB_FAILOVER_TEST") != "1":
        pytest.skip("set CNETMOD_MONGODB_FAILOVER_TEST=1 for primary step-down")
    if len(mongodb_client.nodes) < 3:
        pytest.skip("three-node MongoDB replica set required")
    run_id = f"mongo-failover-{uuid.uuid4()}"
    collection = mongodb_client["cnetmod_interop"]["cnetmod_example_test_results"]
    result_filter = {"run_id": run_id, "scenario": "failover-watch"}
    collection.delete_many(result_filter)
    process = ExampleProcess(
        mongodb_example_executable,
        {
            **mongodb_example_environment,
            "CNETMOD_MONGODB_TEST_RUN_ID": run_id,
        },
        arguments=("--scenario", "failover-watch"),
    ).start()
    try:
        time.sleep(3)
        with MongoClient(mongodb_uri, serverSelectionTimeoutMS=10_000) as controller:
            try:
                controller.admin.command({"replSetStepDown": 10, "force": True})
            except (AutoReconnect, OperationFailure):
                pass
        assert process.wait(60) == 0
        result = wait_until(
            lambda: collection.find_one(result_filter),
            10,
            "MongoDB failover result",
        )
        metrics = _passed(result)
        assert metrics["successful_during_failover"] > 0
        assert metrics.get("observed_primaries", 0) >= 2
    finally:
        process.stop()
        collection.delete_many(result_filter)
