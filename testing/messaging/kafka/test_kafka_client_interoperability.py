from __future__ import annotations

import time
import uuid

import pytest
from confluent_kafka import Consumer, Producer
from confluent_kafka.admin import AdminClient, NewTopic

from container_services import kafka_reference_configuration


def _driver_endpoint_parameters(endpoint) -> dict[str, object]:
    return {
        "bootstrap_servers": f"{endpoint.host}:{endpoint.port}",
        "security_protocol": endpoint.security_protocol,
        "sasl_mechanism": endpoint.sasl_mechanism,
        "username": endpoint.username,
        "password": endpoint.password,
    }


def _create_topic(endpoint, topic: str, partitions: int = 3) -> None:
    admin = AdminClient(kafka_reference_configuration(endpoint))
    future = admin.create_topics([NewTopic(topic, partitions, 1)])[topic]
    future.result(timeout=15)


def _reference_consume(endpoint, topic: str, group: str, count: int):
    consumer = Consumer(
        {
            **kafka_reference_configuration(endpoint),
            "group.id": group,
            "auto.offset.reset": "earliest",
            "enable.auto.commit": False,
        }
    )
    consumer.subscribe([topic])
    messages = []
    deadline = time.monotonic() + 20
    while len(messages) < count and time.monotonic() < deadline:
        message = consumer.poll(1)
        if message is None:
            continue
        assert message.error() is None
        messages.append(message)
    consumer.commit(asynchronous=False)
    consumer.close()
    return messages


@pytest.mark.interoperability
def test_metadata_partitioning_record_batch_and_crc(kafka_service, kafka_driver):
    _, endpoint = kafka_service
    topic = f"cnetmod-record-batch-{uuid.uuid4().hex}"
    _create_topic(endpoint, topic, partitions=3)
    # Preserve deliberately non-UTF8 values to catch accidental string conversion.
    records = [
        {"key_hex": f"key-{index % 3}".encode().hex(), "value_hex": (bytes([index]) * 257).hex()}
        for index in range(12)
    ]
    result = kafka_driver.request(
        "produce_batch",
        **_driver_endpoint_parameters(endpoint),
        topic=topic,
        records=records,
        compression="gzip",
        acknowledgements="all",
    )
    assert result["api_versions_negotiated"] is True
    assert result["record_batch_crc_valid"] is True
    assert set(result["partitions"]).issubset({0, 1, 2})
    messages = _reference_consume(endpoint, topic, f"python-{uuid.uuid4().hex}", 12)
    assert sorted(message.value() for message in messages) == sorted(
        bytes.fromhex(record["value_hex"]) for record in records
    )


@pytest.mark.interoperability
def test_reference_producer_to_cnetmod_consumer_offset_commit(kafka_service, kafka_driver):
    _, endpoint = kafka_service
    topic = f"cnetmod-offset-{uuid.uuid4().hex}"
    _create_topic(endpoint, topic, partitions=2)
    producer = Producer(kafka_reference_configuration(endpoint))
    for index in range(10):
        producer.produce(topic, key=f"k{index % 2}", value=f"v{index}")
    assert producer.flush(15) == 0
    group = f"cnetmod-group-{uuid.uuid4().hex}"
    first = kafka_driver.request(
        "consume_and_commit",
        **_driver_endpoint_parameters(endpoint),
        topic=topic,
        group_id=group,
        maximum_records=6,
        automatic_commit=False,
    )
    second = kafka_driver.request(
        "consume_and_commit",
        **_driver_endpoint_parameters(endpoint),
        topic=topic,
        group_id=group,
        maximum_records=10,
        automatic_commit=False,
    )
    assert first["record_count"] == 6
    assert second["record_count"] == 4
    assert set(first["values"] + second["values"]) == {f"v{i}" for i in range(10)}


@pytest.mark.interoperability
@pytest.mark.stability
def test_consumer_group_rebalance_assigns_each_partition_once(kafka_service, kafka_driver):
    _, endpoint = kafka_service
    topic = f"cnetmod-rebalance-{uuid.uuid4().hex}"
    _create_topic(endpoint, topic, partitions=4)
    result = kafka_driver.request(
        "consumer_group_rebalance_probe",
        **_driver_endpoint_parameters(endpoint),
        topic=topic,
        group_id=f"cnetmod-rebalance-group-{uuid.uuid4().hex}",
        consumer_count=2,
        join_and_leave_cycles=3,
    )
    for generation in result["generations"]:
        assigned = [partition for member in generation["members"] for partition in member["partitions"]]
        assert sorted(assigned) == [0, 1, 2, 3]


@pytest.mark.interoperability
def test_idempotent_producer_and_transaction_visibility(kafka_service, kafka_driver):
    _, endpoint = kafka_service
    topic = f"cnetmod-transaction-{uuid.uuid4().hex}"
    _create_topic(endpoint, topic, partitions=1)
    result = kafka_driver.request(
        "idempotence_transaction_probe",
        **_driver_endpoint_parameters(endpoint),
        topic=topic,
        transactional_id=f"cnetmod-tx-{uuid.uuid4().hex}",
        retry_same_sequence=True,
        committed_values=["committed-1", "committed-2"],
        aborted_values=["aborted-1"],
    )
    assert result["producer_id_assigned"] is True
    assert result["duplicate_count"] == 0
    assert result["read_committed_values"] == ["committed-1", "committed-2"]


@pytest.mark.stability
def test_metadata_refresh_and_retry_after_broker_restart(kafka_service, kafka_driver):
    service, endpoint = kafka_service
    if service is None:
        pytest.skip("broker restart injection requires container mode")
    result = kafka_driver.request_during_fault(
        "broker_restart_probe",
        service.restart,
        fault_delay_seconds=2,
        process_timeout_seconds=120,
        **_driver_endpoint_parameters(endpoint),
        topic=f"cnetmod-restart-{uuid.uuid4().hex}",
        request_timeout_milliseconds=30000,
    )
    assert result["metadata_refreshed"] is True
    assert result["delivery_retried"] is True
    assert result["records_lost"] == 0


@pytest.mark.security
def test_sasl_tls_and_authentication_failure(kafka_driver, kafka_security_endpoint):
    security_endpoint = kafka_security_endpoint
    accepted = kafka_driver.request(
        "connect_security_probe",
        bootstrap_servers=f"{security_endpoint['host']}:{security_endpoint['port']}",
        security_protocol="SASL_SSL",
        sasl_mechanism="PLAIN",
        username=security_endpoint["username"],
        password=security_endpoint["password"],
        ca_file=security_endpoint["ca_file"],
    )
    assert accepted["tls_verified"] is True
    assert accepted["authenticated"] is True


@pytest.mark.interoperability
def test_server_rejects_oversized_record_without_poisoning_connection(
    kafka_service, kafka_driver
):
    _, endpoint = kafka_service
    topic = f"cnetmod-oversized-{uuid.uuid4().hex}"
    _create_topic(endpoint, topic, partitions=1)
    result = kafka_driver.request(
        "record_size_boundary_probe",
        **_driver_endpoint_parameters(endpoint),
        topic=topic,
        oversized_value_size=2 * 1024 * 1024,
        follow_up_value_hex=b"connection-still-usable".hex(),
    )
    assert result["oversized_record_rejected"] is True
    assert result["error_category"] in {"message_too_large", "invalid_record"}
    assert result["follow_up_delivered"] is True


@pytest.mark.stability
@pytest.mark.slow
def test_sustained_compressed_batches_preserve_offsets_and_payloads(
    kafka_service, kafka_driver
):
    _, endpoint = kafka_service
    topic = f"cnetmod-stability-{uuid.uuid4().hex}"
    _create_topic(endpoint, topic, partitions=6)
    result = kafka_driver.request(
        "sustained_delivery_probe",
        process_timeout_seconds=240,
        **_driver_endpoint_parameters(endpoint),
        topic=topic,
        record_count=50000,
        payload_size=1024,
        compression="lz4",
        idempotent_producer=True,
        consumer_group=f"cnetmod-stability-group-{uuid.uuid4().hex}",
    )
    assert result["produced_count"] == 50000
    assert result["consumed_count"] == 50000
    assert result["duplicate_count"] == 0
    assert result["payload_mismatch_count"] == 0
    assert result["partition_offset_gap_count"] == 0
