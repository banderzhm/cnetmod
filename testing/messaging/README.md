# Messaging client interoperability tests

This directory verifies the cnetmod **client** implementations against real
brokers.  It does not contain an MQTT test, broker implementation, socket mock,
or handwritten server-side protocol frame.

## Test architecture

| Client under test | Real service | Independent reference library |
|---|---|---|
| AMQP 0-9-1 | RabbitMQ 4.1 | `aio-pika` |
| AMQP 1.0 | Apache ActiveMQ Artemis 2.40 | `python-qpid-proton` |
| Kafka | Apache Kafka 7.9 container | `confluent-kafka` |

In container mode, `testcontainers` and the Docker API own service lifecycle
and restart fault injection.  In external mode, the same client checks run
against addresses supplied through environment variables.  Protocol behavior
is generated only by cnetmod, the reference client library, or the real
service.

Container readiness is verified with the corresponding mature protocol
library (`pika`, `python-qpid-proton`, or `confluent-kafka`), not by matching a
possibly stale log line.  A failed start removes the partial container, and a
driver timeout terminates its child process before the suite continues.

## Local machine behavior

RabbitMQ, Artemis, Kafka, and Docker do **not** have to be installed on the
local machine.  When neither Docker nor an external endpoint is available, the
CTest entry point prints the reason and exits with skip code 77.  CTest reports
the interoperability suite as skipped rather than failed.

Installing the Python libraries locally is optional.  If local execution is
wanted, use the machine's Anaconda installation.  Do not create a virtual
environment on drive C.  From PowerShell:

Use the machine's Anaconda installation.  Do not create a virtual environment
on drive C.  From PowerShell:

```powershell
& 'E:\anaconda3\python.exe' -m pip install -r 'E:\beifen\cnetmod\testing\messaging\requirements.txt'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

For optional local container mode, Docker must be running and able to download:

- `rabbitmq:4.1-management`
- `apache/activemq-artemis:2.40.0`
- `confluentinc/cp-kafka:7.9.1`

Select a mode with `CNETMOD_MESSAGING_SERVICE_MODE`:

- `auto` (default): prefer a configured external endpoint, otherwise Docker.
- `container`: always create real service containers.
- `external`: never contact Docker; use only configured external endpoints.

## Continuous integration

The Release variant of `.github/workflows/linux-clang.yml` installs the Python
libraries with the GitHub runner's Python 3.12 and selects `container` mode.
The Linux runner pulls the official RabbitMQ, Artemis, and Kafka images and
runs the registered CTest suite automatically.  The `E:\anaconda3` path is
only for this Windows workstation and is never used by CI.

Until the three C++ interoperability driver targets are present, the workflow
prints a clear notice and does not attempt the broker suite.  Once those
targets are added, CMake registers the test and CI starts validating it without
another workflow change.

## C++ driver contract

Each executable accepts `--json-lines`, reads exactly one JSON request from
standard input, writes exactly one JSON response to standard output, and exits.
Diagnostics belong on standard error.  The executable must not log to standard
output.

Request envelope:

```json
{
  "contract_version": 1,
  "protocol": "amqp091",
  "operation": "publish",
  "parameters": {}
}
```

Success response:

```json
{
  "contract_version": 1,
  "status": "ok",
  "result": {}
}
```

Failure response:

```json
{
  "contract_version": 1,
  "status": "error",
  "error_code": "authentication_failed",
  "message": "broker rejected credentials"
}
```

The expected CMake target names and environment variables are:

| Protocol | CMake target | Environment variable |
|---|---|---|
| AMQP 0-9-1 | `amqp091_interop_driver` | `CNETMOD_AMQP091_INTEROP_DRIVER` |
| AMQP 1.0 | `amqp10_interop_driver` | `CNETMOD_AMQP10_INTEROP_DRIVER` |
| Kafka | `kafka_interop_driver` | `CNETMOD_KAFKA_INTEROP_DRIVER` |

Operation names and their exact parameters/results are executable
specifications in the three `test_*_client_interoperability.py` files.  Binary
payloads use lower-case hexadecimal.  Kafka offsets and AMQP delivery state
must be returned as JSON integers/strings, never inferred from log text.

Required driver operations:

| AMQP 0-9-1 | AMQP 1.0 | Kafka |
|---|---|---|
| `publish` | `send` | `produce_batch` |
| `consume_one` | `receive_one` | `consume_and_commit` |
| `transaction_probe` | `link_credit_probe` | `consumer_group_rebalance_probe` |
| `reconnect_and_publish` | `delivery_outcome_probe` | `idempotence_transaction_probe` |
| `connect_security_probe` | `reconnect_link_probe` | `broker_restart_probe` |
| `message_boundary_probe` | `connect_security_probe` | `connect_security_probe` |
| `qos_prefetch_probe` | `message_boundary_probe` | `record_size_boundary_probe` |
| `sustained_delivery_probe` | `transaction_coordinator_probe` | `sustained_delivery_probe` |
|  | `sustained_unsettled_delivery_probe` |  |

### Pending C++ driver alignment

The Kafka C++ public surface is still being finalized around `client_facade`,
producer, consumer, metadata, SASL, and transaction management.  These Python
tests deliberately do not compile against those classes or freeze their
constructor signatures.  `kafka_driver_contract.py` is not introduced; the
single `_driver_endpoint_parameters()` adapter in the Kafka test file is the
only mapping from external connection settings into the JSON driver contract.

When the C++ API is stable, the driver still needs to define:

- how `security_protocol`, `sasl_mechanism`, username, and password map into
  the final client connection options;
- how producer delivery metadata and negotiated API versions are serialized;
- how consumer generation, assignment, and committed offsets are observed;
- how idempotent producer identity and transaction commit/abort results are
  exposed without relying on log parsing.

For AMQP 0-9-1, the driver must pass `virtual_host` through connection setup.
For AMQP 1.0, it must use the AMQP 1.0 SASL/open/session/link implementation
even when the endpoint happens to be the same RabbitMQ listener.

## Coverage matrix

| Area | AMQP 0-9-1 | AMQP 1.0 | Kafka |
|---|:---:|:---:|:---:|
| Publish/consume accuracy | yes | yes | yes |
| Binary, empty, Unicode boundaries | yes | yes | yes |
| Confirm/remote outcome | publisher confirm | accepted/released/rejected/modified | `acks=all` delivery report |
| Settlement/acknowledgement | ack + nack/requeue | settled/unsettled disposition | manual offset commit |
| Transaction | commit + rollback | coordinator declare/discharge commit + rollback | commit + abort visibility |
| Flow control | QoS exercised by consume path | link credit explicitly verified | fetch/batch limits |
| Metadata/topology | durable queue recovery | session/link recovery | metadata and partitions |
| Consumer coordination | n/a | n/a | group generations and rebalance |
| Idempotence | confirm semantics | unsettled delivery reconciliation | producer ID and duplicate suppression |
| Heartbeat/idle timeout | heartbeat + reconnect | idle timeout + reconnect | request timeout + metadata refresh |
| TLS/SASL | TLS + PLAIN | TLS + PLAIN | SASL_SSL + PLAIN |
| Broker restart fault | RabbitMQ restart | Artemis restart | Kafka restart |
| Server-enforced boundary | field/body round trip | remote max-frame fragmentation | oversized-record rejection |

The AMQP 1.0 transaction check requires a real coordinator declaration and
discharge.  Ordinary accepted outcomes do not satisfy that contract.

## Optional local container mode

Set the driver paths, then run all suites:

```powershell
$env:CNETMOD_AMQP091_INTEROP_DRIVER = 'E:\beifen\cnetmod\build\bin\amqp091_interop_driver.exe'
$env:CNETMOD_AMQP10_INTEROP_DRIVER = 'E:\beifen\cnetmod\build\bin\amqp10_interop_driver.exe'
$env:CNETMOD_KAFKA_INTEROP_DRIVER = 'E:\beifen\cnetmod\build\bin\kafka_interop_driver.exe'
$env:CNETMOD_MESSAGING_SERVICE_MODE = 'container'
& 'E:\anaconda3\python.exe' 'E:\beifen\cnetmod\testing\messaging\run_messaging_interoperability.py'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

## External endpoint mode

External mode never starts or stops the supplied services.  Broker-restart
fault tests are skipped, while accuracy, acknowledgement, transaction, flow
control, boundary, and stability tests still run.  Configure:

| Protocol | Required endpoint variables |
|---|---|
| AMQP 0-9-1 | `CNETMOD_AMQP091_HOST`, `CNETMOD_AMQP091_PORT`, `CNETMOD_AMQP091_USERNAME`, `CNETMOD_AMQP091_PASSWORD`, `CNETMOD_AMQP091_VHOST` |
| AMQP 1.0 | `CNETMOD_AMQP10_HOST`, `CNETMOD_AMQP10_PORT`, `CNETMOD_AMQP10_USERNAME`, `CNETMOD_AMQP10_PASSWORD` |
| Kafka | `CNETMOD_KAFKA_HOST`, `CNETMOD_KAFKA_PORT`, `CNETMOD_KAFKA_SECURITY_PROTOCOL`, `CNETMOD_KAFKA_SASL_MECHANISM`, `CNETMOD_KAFKA_USERNAME`, `CNETMOD_KAFKA_PASSWORD` |

`CNETMOD_AMQP091_VHOST` is passed to both `aio-pika` and the cnetmod driver;
special characters are URI-escaped for the reference connection.  AMQP 1.0
remains a separate protocol suite even when its endpoint is the RabbitMQ AMQP
1.0 plugin on the same host and port 5672.

Kafka security values are applied to every reference `AdminClient`, producer,
and consumer, as well as every cnetmod driver request.  The tests do not assume
`PLAINTEXT` or anonymous access in external mode.

The runner automatically reads
`testing/messaging/.env.external.local` when present, without overriding values
already supplied by the process environment.  This local credential file is
ignored by the repository and must not be copied into CI artifacts.

Then set:

```powershell
$env:CNETMOD_MESSAGING_SERVICE_MODE = 'external'
& 'E:\anaconda3\python.exe' 'E:\beifen\cnetmod\testing\messaging\run_messaging_interoperability.py'
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 77) { exit $LASTEXITCODE }
```

Security tests use separately provisioned real TLS/SASL broker endpoints so
the suite can validate production-like certificates and hostname checks.  Set
the five suffixes `HOST`, `PORT`, `USERNAME`, `PASSWORD`, and `CA_FILE` for
each prefix: `CNETMOD_AMQP091_SECURITY_`, `CNETMOD_AMQP10_SECURITY_`, and
`CNETMOD_KAFKA_SECURITY_`.  A protocol's security test is skipped when its
values are absent.
