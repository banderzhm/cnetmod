from __future__ import annotations

import uuid
from concurrent.futures import ThreadPoolExecutor

import pytest


pytestmark = pytest.mark.postgresql_example


def _shutdown(process, base_url: str) -> None:
    response = process.request(base_url, "POST", "/admin/shutdown")
    assert response.status in {200, 202, 204}
    assert process.wait(15) == 0


def _data(response):
    assert response.body["code"] == response.status
    assert isinstance(response.body["message"], str)
    return response.body["data"]


def test_health_readiness_and_graceful_shutdown(postgresql_service_factory) -> None:
    process, base_url = postgresql_service_factory()
    live = process.request(base_url, "GET", "/health/live")
    ready = process.request(base_url, "GET", "/health/ready")
    assert live.status == 200
    assert _data(live)["status"] in {"UP", "live"}
    assert ready.status == 200
    ready_data = _data(ready)
    assert ready_data["status"] in {"UP", "ready"}
    assert ready_data.get("active_endpoint")
    assert int(ready_data.get("pool_size", 0)) >= 2
    assert int(ready_data.get("idle_connections", 0)) >= 1
    _shutdown(process, base_url)


def test_concurrent_crud_transaction_and_idempotency(
    postgresql_service_factory,
) -> None:
    process, base_url = postgresql_service_factory()
    request_ids = [f"pg-e2e-{uuid.uuid4()}" for _ in range(128)]

    def create(item: tuple[int, str]):
        sequence, request_id = item
        return process.request(
            base_url,
            "POST",
            "/api/requests",
            {"request_id": request_id, "sequence_number": sequence},
            timeout_seconds=30,
        )

    with ThreadPoolExecutor(max_workers=32) as executor:
        created = list(executor.map(create, enumerate(request_ids)))
    assert all(response.status in {200, 201} for response in created)
    assert [_data(response).get("request_id") for response in created] == request_ids

    duplicate = process.request(
        base_url,
        "POST",
        "/api/requests",
        {"request_id": request_ids[0], "sequence_number": 0},
    )
    assert duplicate.status == 200
    assert _data(duplicate).get("id") == _data(created[0]).get("id")
    assert _data(duplicate).get("sequence_number") == 0

    with ThreadPoolExecutor(max_workers=32) as executor:
        read = list(
            executor.map(
                lambda request_id: process.request(
                    base_url,
                    "GET",
                    f"/api/requests/{request_id}",
                    timeout_seconds=30,
                ),
                request_ids,
            )
        )
    assert all(response.status == 200 for response in read)

    updated_ids = request_ids[:64]
    with ThreadPoolExecutor(max_workers=16) as executor:
        updated = list(
            executor.map(
                lambda request_id: process.request(
                    base_url,
                    "PUT",
                    f"/api/requests/{request_id}",
                    {"sequence_number": 100_000},
                    timeout_seconds=30,
                ),
                updated_ids,
            )
        )
    assert all(response.status == 200 for response in updated)

    with ThreadPoolExecutor(max_workers=32) as executor:
        deleted = list(
            executor.map(
                lambda request_id: process.request(
                    base_url, "DELETE", f"/api/requests/{request_id}",
                    timeout_seconds=30,
                ),
                request_ids,
            )
        )
    assert all(response.status in {200, 204} for response in deleted)
    assert process.request(
        base_url, "GET", f"/api/requests/{request_ids[0]}"
    ).status == 404
    _shutdown(process, base_url)


def test_unreachable_first_endpoint_falls_back_to_healthy_database(
    postgresql_service_factory,
    postgresql_example_environment: dict[str, str],
) -> None:
    healthy = postgresql_example_environment["CNETMOD_POSTGRESQL_ENDPOINTS"]
    process, base_url = postgresql_service_factory(
        endpoints=f"127.0.0.1:1,{healthy}"
    )
    ready = process.request(base_url, "GET", "/health/ready")
    assert ready.status == 200
    assert _data(ready).get("active_endpoint") == healthy
    _shutdown(process, base_url)
