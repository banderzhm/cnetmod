from __future__ import annotations

import asyncio
import uuid
from urllib.parse import quote

import aio_pika
import pytest


def _connection_url(endpoint) -> str:
    return (
        f"amqp://{quote(endpoint.username, safe='')}:{quote(endpoint.password, safe='')}@"
        f"{endpoint.host}:{endpoint.port}/{quote(endpoint.virtual_host, safe='')}"
    )


def _driver_endpoint_parameters(endpoint) -> dict[str, object]:
    return {
        "host": endpoint.host,
        "port": endpoint.port,
        "username": endpoint.username,
        "password": endpoint.password,
        "virtual_host": endpoint.virtual_host,
    }


async def _declare_queue(
    endpoint, name: str, durable: bool = False, auto_delete: bool | None = None
):
    connection = await aio_pika.connect(_connection_url(endpoint), heartbeat=3)
    channel = await connection.channel(publisher_confirms=True)
    queue = await channel.declare_queue(
        name,
        durable=durable,
        auto_delete=not durable if auto_delete is None else auto_delete,
    )
    return connection, channel, queue


@pytest.mark.interoperability
@pytest.mark.asyncio
async def test_publish_confirm_preserves_message_body_and_properties(
    rabbitmq_service, amqp091_driver
):
    _, endpoint = rabbitmq_service
    queue_name = f"cnetmod-confirm-{uuid.uuid4().hex}"
    connection, _, queue = await _declare_queue(endpoint, queue_name)
    body = bytes(range(256)) * 8
    result = await asyncio.to_thread(
        amqp091_driver.request,
        "publish",
        **_driver_endpoint_parameters(endpoint),
        exchange="",
        routing_key=queue_name,
        body_hex=body.hex(),
        properties={"content_type": "application/octet-stream", "correlation_id": "interop-1"},
        publisher_confirm=True,
    )
    assert result["confirmed"] is True
    message = await queue.get(timeout=10, fail=False)
    assert message is not None
    assert message.body == body
    assert message.content_type == "application/octet-stream"
    assert message.correlation_id == "interop-1"
    await message.ack()
    await connection.close()


@pytest.mark.interoperability
@pytest.mark.asyncio
async def test_consume_ack_and_nack_requeue(rabbitmq_service, amqp091_driver):
    _, endpoint = rabbitmq_service
    queue_name = f"cnetmod-ack-{uuid.uuid4().hex}"
    connection, channel, queue = await _declare_queue(
        endpoint, queue_name, auto_delete=False
    )
    await channel.default_exchange.publish(
        aio_pika.Message(b"consume-from-cnetmod"), routing_key=queue_name
    )
    rejected = await asyncio.to_thread(
        amqp091_driver.request,
        "consume_one",
        **_driver_endpoint_parameters(endpoint),
        queue=queue_name,
        settlement="nack_requeue",
    )
    assert bytes.fromhex(rejected["body_hex"]) == b"consume-from-cnetmod"
    redelivered = await queue.get(timeout=10, fail=False)
    assert redelivered is not None
    assert redelivered.redelivered is True
    await redelivered.ack()
    assert await queue.get(timeout=1, fail=False) is None
    await queue.delete()
    await connection.close()


@pytest.mark.interoperability
@pytest.mark.asyncio
async def test_transaction_commit_and_rollback(rabbitmq_service, amqp091_driver):
    _, endpoint = rabbitmq_service
    queue_name = f"cnetmod-tx-{uuid.uuid4().hex}"
    connection, _, queue = await _declare_queue(endpoint, queue_name)
    result = await asyncio.to_thread(
        amqp091_driver.request,
        "transaction_probe",
        **_driver_endpoint_parameters(endpoint),
        queue=queue_name,
        committed_body_hex=b"committed".hex(),
        rolled_back_body_hex=b"rolled-back".hex(),
    )
    assert result == {"committed": True, "rolled_back": True}
    delivered = await queue.get(timeout=10, fail=False)
    assert delivered is not None and delivered.body == b"committed"
    await delivered.ack()
    assert await queue.get(timeout=1, fail=False) is None
    await connection.close()


@pytest.mark.interoperability
@pytest.mark.stability
def test_heartbeat_reconnect_and_durable_topology_recovery(
    rabbitmq_service, amqp091_driver
):
    service, endpoint = rabbitmq_service
    if service is None:
        pytest.skip("broker restart injection requires container mode")
    queue_name = f"cnetmod-recovery-{uuid.uuid4().hex}"
    result = amqp091_driver.request_during_fault(
        "reconnect_and_publish",
        service.restart,
        fault_delay_seconds=2,
        process_timeout_seconds=90,
        **_driver_endpoint_parameters(endpoint),
        heartbeat_seconds=1,
        reconnect_timeout_seconds=60,
        durable_queue=queue_name,
        body_hex=b"after-restart".hex(),
    )
    assert result["reconnected"] is True
    assert result["topology_restored"] is True
    assert result["publish_confirmed"] is True


@pytest.mark.security
def test_tls_sasl_plain_and_bad_credentials(amqp091_driver, amqp091_security_endpoint):
    security_endpoint = amqp091_security_endpoint
    accepted = amqp091_driver.request(
        "connect_security_probe",
        protocol="amqp091",
        **security_endpoint,
        tls=True,
        expected_authentication=True,
    )
    assert accepted["tls_verified"] is True
    rejected = amqp091_driver.request(
        "connect_security_probe",
        protocol="amqp091",
        **{**security_endpoint, "password": "intentionally-invalid"},
        tls=True,
        expected_authentication=False,
    )
    assert rejected["authentication_rejected"] is True


@pytest.mark.interoperability
def test_empty_binary_and_large_header_boundaries(rabbitmq_service, amqp091_driver):
    _, endpoint = rabbitmq_service
    result = amqp091_driver.request(
        "message_boundary_probe",
        **_driver_endpoint_parameters(endpoint),
        queue=f"cnetmod-boundary-{uuid.uuid4().hex}",
        bodies_hex=["", bytes(range(256)).hex(), b"after-boundary".hex()],
        headers={"unicode-key": "紫微-消息", "long-value": "x" * 32768},
    )
    assert result["round_trip_bodies_hex"] == [
        "",
        bytes(range(256)).hex(),
        b"after-boundary".hex(),
    ]
    assert result["connection_remained_open"] is True


@pytest.mark.interoperability
def test_qos_prefetch_never_exceeds_credit(rabbitmq_service, amqp091_driver):
    _, endpoint = rabbitmq_service
    result = amqp091_driver.request(
        "qos_prefetch_probe",
        **_driver_endpoint_parameters(endpoint),
        queue=f"cnetmod-qos-{uuid.uuid4().hex}",
        published_message_count=32,
        prefetch_count=3,
        hold_acknowledgements=True,
    )
    assert result["maximum_simultaneous_unacknowledged"] == 3
    assert result["received_after_ack"] == 32


@pytest.mark.stability
@pytest.mark.slow
def test_sustained_confirm_and_consume_has_no_loss_or_duplicate(
    rabbitmq_service, amqp091_driver
):
    _, endpoint = rabbitmq_service
    result = amqp091_driver.request(
        "sustained_delivery_probe",
        process_timeout_seconds=180,
        **_driver_endpoint_parameters(endpoint),
        queue=f"cnetmod-stability-{uuid.uuid4().hex}",
        message_count=10000,
        payload_size=1024,
        publisher_confirm_window=256,
        prefetch_count=128,
    )
    assert result["confirmed_count"] == 10000
    assert result["consumed_count"] == 10000
    assert result["duplicate_count"] == 0
    assert result["payload_mismatch_count"] == 0
