from __future__ import annotations

import asyncio
import uuid

import pytest
from proton import Message
from proton.utils import BlockingConnection


def _url(endpoint) -> str:
    return f"amqp://{endpoint.host}:{endpoint.port}"


def _reference_send(endpoint, address: str, body) -> None:
    connection = BlockingConnection(
        _url(endpoint), user=endpoint.username, password=endpoint.password,
        allowed_mechs="PLAIN", timeout=10
    )
    sender = connection.create_sender(address)
    sender.send(Message(body=body, content_type="application/json"))
    connection.close()


def _reference_receive(endpoint, address: str):
    connection = BlockingConnection(
        _url(endpoint), user=endpoint.username, password=endpoint.password,
        allowed_mechs="PLAIN", timeout=10
    )
    receiver = connection.create_receiver(address, credit=1)
    message = receiver.receive(timeout=10)
    receiver.accept()
    connection.close()
    return message


@pytest.mark.interoperability
@pytest.mark.asyncio
async def test_transfer_and_accepted_outcome(artemis_service, amqp10_driver):
    _, endpoint = artemis_service
    address = f"cnetmod.amqp10.accepted.{uuid.uuid4().hex}"
    result = await asyncio.to_thread(
        amqp10_driver.request,
        "send",
        host=endpoint.host,
        port=endpoint.port,
        username=endpoint.username,
        password=endpoint.password,
        address=address,
        body={"text": "cnetmod", "sequence": 7},
        settlement="unsettled",
    )
    assert result["remote_outcome"] == "accepted"
    message = await asyncio.to_thread(_reference_receive, endpoint, address)
    assert message.body == {"text": "cnetmod", "sequence": 7}


@pytest.mark.interoperability
@pytest.mark.asyncio
async def test_receive_message_sections_and_settlement(artemis_service, amqp10_driver):
    _, endpoint = artemis_service
    address = f"cnetmod.amqp10.receive.{uuid.uuid4().hex}"
    await asyncio.to_thread(_reference_send, endpoint, address, "from-proton")
    result = await asyncio.to_thread(
        amqp10_driver.request,
        "receive_one",
        host=endpoint.host,
        port=endpoint.port,
        username=endpoint.username,
        password=endpoint.password,
        address=address,
        link_credit=1,
        outcome="accepted",
    )
    assert result["body"] == "from-proton"
    assert result["content_type"] == "application/json"
    assert result["settled"] is True


@pytest.mark.interoperability
def test_link_credit_blocks_second_delivery(artemis_service, amqp10_driver):
    _, endpoint = artemis_service
    address = f"cnetmod.amqp10.credit.{uuid.uuid4().hex}"
    _reference_send(endpoint, address, "first")
    _reference_send(endpoint, address, "second")
    result = amqp10_driver.request(
        "link_credit_probe",
        host=endpoint.host,
        port=endpoint.port,
        username=endpoint.username,
        password=endpoint.password,
        address=address,
        initial_credit=1,
        replenish_credit=1,
    )
    assert result["before_replenish_count"] == 1
    assert result["after_replenish_count"] == 2
    assert result["bodies"] == ["first", "second"]


@pytest.mark.interoperability
def test_released_and_rejected_outcomes(artemis_service, amqp10_driver):
    _, endpoint = artemis_service
    address = f"cnetmod.amqp10.outcomes.{uuid.uuid4().hex}"
    result = amqp10_driver.request(
        "delivery_outcome_probe",
        host=endpoint.host,
        port=endpoint.port,
        username=endpoint.username,
        password=endpoint.password,
        address=address,
        outcomes=["released", "rejected", "modified"],
    )
    assert result["observed_outcomes"] == ["released", "rejected", "modified"]


@pytest.mark.stability
def test_session_link_recovery_after_broker_restart(artemis_service, amqp10_driver):
    service, endpoint = artemis_service
    if service is None:
        pytest.skip("broker restart injection requires container mode")
    result = amqp10_driver.request_during_fault(
        "reconnect_link_probe",
        service.restart,
        fault_delay_seconds=2,
        process_timeout_seconds=120,
        host=endpoint.host,
        port=endpoint.port,
        username=endpoint.username,
        password=endpoint.password,
        address=f"cnetmod.amqp10.reconnect.{uuid.uuid4().hex}",
        idle_timeout_milliseconds=1000,
    )
    assert result["connection_reopened"] is True
    assert result["session_rebegun"] is True
    assert result["link_reattached"] is True
    assert result["unsettled_deliveries_resolved"] is True


@pytest.mark.security
def test_tls_sasl_and_hostname_verification(amqp10_driver, amqp10_security_endpoint):
    security_endpoint = amqp10_security_endpoint
    result = amqp10_driver.request(
        "connect_security_probe",
        **security_endpoint,
        tls=True,
        sasl_mechanisms=["PLAIN"],
        verify_hostname=True,
    )
    assert result["tls_verified"] is True
    assert result["sasl_mechanism"] == "PLAIN"


@pytest.mark.interoperability
def test_empty_data_unicode_properties_and_remote_frame_limit(
    artemis_service, amqp10_driver
):
    _, endpoint = artemis_service
    result = amqp10_driver.request(
        "message_boundary_probe",
        host=endpoint.host,
        port=endpoint.port,
        username=endpoint.username,
        password=endpoint.password,
        address=f"cnetmod.amqp10.boundary.{uuid.uuid4().hex}",
        bodies=[None, "", "紫微-消息", "x" * 262144],
        application_properties={"unicode": "边界", "maximum_unsigned": 18446744073709551615},
        honor_remote_max_frame_size=True,
    )
    assert result["round_trip_bodies"] == [None, "", "紫微-消息", "x" * 262144]
    assert result["transfers_fragmented_to_remote_limit"] is True
    assert result["connection_remained_open"] is True


@pytest.mark.interoperability
def test_transaction_coordinator_commit_and_rollback(artemis_service, amqp10_driver):
    _, endpoint = artemis_service
    if not endpoint.supports_transactions:
        pytest.skip("configured AMQP 1.0 broker does not support transactions")
    result = amqp10_driver.request(
        "transaction_coordinator_probe",
        host=endpoint.host,
        port=endpoint.port,
        username=endpoint.username,
        password=endpoint.password,
        address=f"cnetmod.amqp10.transaction.{uuid.uuid4().hex}",
        committed_bodies=["committed-1", "committed-2"],
        rolled_back_bodies=["rolled-back"],
    )
    assert result["declared_transaction"] is True
    assert result["discharged_commit"] is True
    assert result["discharged_rollback"] is True
    assert result["visible_bodies"] == ["committed-1", "committed-2"]


@pytest.mark.stability
@pytest.mark.slow
def test_sustained_unsettled_window_resolves_every_delivery(
    artemis_service, amqp10_driver
):
    _, endpoint = artemis_service
    result = amqp10_driver.request(
        "sustained_unsettled_delivery_probe",
        process_timeout_seconds=180,
        host=endpoint.host,
        port=endpoint.port,
        username=endpoint.username,
        password=endpoint.password,
        address=f"cnetmod.amqp10.stability.{uuid.uuid4().hex}",
        delivery_count=10000,
        unsettled_window=256,
        receiver_credit=128,
    )
    assert result["sent_count"] == 10000
    assert result["accepted_count"] == 10000
    assert result["remaining_unsettled_count"] == 0
    assert result["duplicate_delivery_count"] == 0
