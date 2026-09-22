# Observability implementation and acceptance ledger

Current task ordering and remaining acceptance requirements are maintained in
[application-monitoring-plan.md](application-monitoring-plan.md). This file is
historical evidence, not the active backlog. Source paths in historical entries
refer to their location at the time; monitoring sources now live under
`src/application/monitoring/`.

The target is a complete, decoupled instrumentation lifecycle across HTTP,
OpenAI, Redis, MySQL, PostgreSQL, MongoDB, Kafka, MQTT, AMQP and gRPC. Optional
instrumentation must preserve operation results, cancellation, ownership and
disabled-path performance. This ledger records evidence, not completion claims.

## Verified initial changes

- No enabled signal endpoint leaves the OTLP exporter without queue/client state.
- A disabled telemetry hub returns an empty span sink.
- HTTP client dispatch returns the original client task directly when the sink
  is absent: no additional coroutine frame, request copy or trace header changes.
- HTTP server tracing marks escaping exceptions and 5xx responses as errors.
- An exporter exception cannot replace the original route exception.
- Windows Release builds and tests cover Application, Application integrations,
  OTLP exporter and HTTP tracing in `cmake-build-application-core-only`.

## Protocol-independent lifecycle foundation

- Trace identity, W3C parsing and span records now reside in
  `cnetmod.instrumentation.tracing`, which imports no HTTP, I/O or exporter module.
- `operation_scope` contains lazy metadata evaluation, move ownership,
  exactly-once completion, abandonment handling and exporter exception isolation.
- `operation_result` distinguishes success, error, cancellation, timeout and
  abandonment and preserves the original error code.
- HTTP client observations use this scope. Raw URL collection has been removed
  to avoid exporting credentials and query secrets before a redaction policy exists.
- Windows Release regression tests cover this foundation and existing HTTP,
  OTLP and Application behavior. Cross-platform and performance gates remain open.

## Remaining implementation and evidence

OTLP span encoding now preserves operation outcomes and error category/code
without exporting error messages. The real HTTP collector fixture parses the
received JSON and verifies cancellation remains distinct while timeout and
abandonment map to OTLP error status. The existing boolean failure field remains
honored for producers not yet migrated to structured outcomes.

Redis observed command overloads now depend on protocol-independent tracing,
use the shared operation scope, and return the raw command task when the sink is
empty. RESP server-error replies mark the observation as failed without changing
the response. Windows tests verify empty-command failure parity, parent identity,
exact completion and throwing-exporter isolation. A loopback TCP RESP fixture
now verifies identical request bytes and server-error replies for ordinary and
observed calls, correct failure status, connection reuse after exporter failure,
and exclusion of key/error-detail content from span attributes. This is a
scripted protocol peer, not a real Redis broker. Pool/pipeline automatic
instrumentation and real Redis container tests remain open.

Explicit observed Redis `exec` and both `pipe` overloads now share batch scope
handling, skip payload inspection, and directly return their original task with
an empty sink. The loopback RESP regression runs command, request-builder and
both pipeline entry points, checking byte parity, per-batch error detection,
connection reuse and data exclusion. Automatic pool wiring remains unfinished.

OpenAI unobserved run scopes now skip operation ID generation and event copying.
Default scope attributes use null JSON until observation is needed, and names
are copied only for observed scopes. Child cancellation, metadata and upstream
operation identity remain intact without listeners. Windows Release OpenAI
tests pass, including this regression. Explicit attributes built by callers and
caller-owned event construction still needs further lazy-path integration.

Application OpenAI services now construct a listener only for an enabled trace
sink or local metrics. `run_configuration()` preserves caller settings and adds
the listener idempotently; `telemetry_listener()` now returns a nullable pointer.
Trace-only listeners skip GenAI metric aggregation. Regression tests exercise
disabled/enabled service construction, configuration preservation and trace-only
operation completion.

Independent signal export now supports metrics-only and logs-only endpoints.
Explicit signal switches override endpoint inheritance, disabled queues are not
allocated, and Application configuration passes its switches to the exporter.
Windows regression tests cover disabled submission, selective acceptance and
existing three-signal delivery. Producer-side lazy metrics/log creation and
single-signal real delivery still require additional verification.

Lifecycle and health producers now use lazy operation scopes; lifecycle logs and
OTLP metrics use factory-based submission that skips construction when disabled
or closed. Health/lifecycle local metrics and exporter status gauges honor the
local metric switch. Windows tests exercise exact rollback and optional recovery
with local metrics both enabled and disabled, and verify lazy signal factories
are skipped or failure-isolated. This proves these specific producer paths, not
whole-framework allocation or throughput parity.

- Extend the protocol-neutral foundation with events, links and sampling, and
  migrate remaining protocol producers and HTTP-specific record fields.
- Apply independent trace/metric/log configuration consistently to all producers;
  eliminate lifecycle/health instrumentation work when its signals are disabled.
- Integrate automatic spans into all named protocols, including pools, prepared
  statements, retries, streaming completion, message processing and settlement.
- Preserve explicit coroutine-safe parent context; implement span links for
  messaging batches and fan-out, and consistent head sampling/propagation.
- Correlate logs and metrics, constrain attribute cardinality, redact URLs and
  sensitive input, and validate OTLP aggregation and delivery semantics.
- Verify cancellation, timeout, error classification, exact completion and
  telemetry failure isolation using controlled operations and real dependencies.
- Measure allocations, coroutine frames, disabled throughput/latency and enabled
  overhead against uninstrumented baselines; do not infer parity from unit tests.
- Verify all-off, all-on and protocol subsets on Windows/Linux/macOS and CI.
- Update configuration, examples, migration and repository skill sources, then
  regenerate AGENTS.md and validate formatting/document synchronization.

Metric submission rejects non-finite values, empty names, unsupported kinds and
negative monotonic counters before consuming queue capacity. Finite doubles use
round-trip precision in OTLP JSON instead of six-decimal formatting. Windows
Release tests verify rejection accounting and actual HTTP receiver round trips
for tiny values, negative gauges and the largest finite double.

Counter measurements now mean increments and are aggregated cumulatively by a
transport-independent instrumentation module. Gauge observations retain the
latest timestamped value. OTLP emits one metric per instrument, with stable
counter start times and canonical attribute-set identity. Configurable limits
bound retained instruments (default 128) and ordinary attribute sets per
instrument (default 256); additional attribute sets merge into one boolean
`otel.metric.overflow` series. New instruments over the limit, conflicting
definitions, duplicate attribute keys and non-finite sums are rejected. Limits
require restart when configured through Application. Aggregation is constructed
only for an enabled metrics endpoint, and runs in the exporter drain, not in the
business producer. Windows tests cover accumulation across collections, gauge
ordering, overflow totals and definition conflicts; the real HTTP collector
checks cumulative values and stable start times across two flushes and typed
overflow attributes. Periodic collection, payload-size bounds and
full delivery/failure guarantees remain open.

Explicit-boundary cumulative histograms now share the neutral aggregation layer.
Bounds are finite, strictly increasing, and capped at 256; equality is assigned
to the inclusive upper bucket. Counts, per-bucket counts, min/max and start times
survive collection, including cardinality overflow. Histograms containing a
negative observation omit sum but retain their distribution. Invalid definitions
and numeric overflow cannot partially update counts. Windows unit and real HTTP
receiver tests verify bounds, bucket counts, string-encoded uint64 fields,
cumulative temporality and omission of sum for negative observations. Automatic
duration instrumentation and percentile dashboards are not yet covered by this
foundation; these require protocol producers and backend integration.

HTTP client duration now uses an independent measurement sink rather than a
completed-span callback. Trace-only, metrics-only, combined and disabled dispatch
are explicit; disabled dispatch returns the original client task, and
metrics-only does not copy the request or inject trace headers. The hub adapter
records local OpenMetrics histograms and/or queues OTLP measurements; Application
HTTP client services bind both sinks. Real loopback tests exercise success and
503 responses with deliberately throwing sinks, original request preservation,
transport-error parity, metrics-only Application binding, local rendering,
independent export acceptance and expired/closed hub safety. HTTP durations use
seconds and explicit buckets. Server-address attributes, full semantic-convention
typing, batch/streaming APIs, local registry cardinality limits, cancellation
fault injection and disabled-path allocation/throughput measurements remain to
be validated or completed; these tests do not establish whole-framework parity.

HTTP server middleware now publishes duration histograms through the independent
measurement adapter. Application installs it only when a measurement sink exists
and installs tracing only when the hub supplies a nonempty span sink (including
the explicit exporter signal switch). It replaces Application's previous local
HTTP metric names; migration mappings are documented. Unit and real loopback
tests verify unchanged responses with throwing sinks, exact-once reporting,
exception propagation, no trace requirement, 404 versus 503 classification and
bounded method labels. Raw request paths are no longer default metric labels.
Route-template/scheme attributes, full stream completion, measured zero-overhead
builds and end-to-end collector/backend dashboards remain outside this evidence.

A dedicated allocation-regression executable now replaces C++ allocation only
inside that test process. For 256 repeated operations per case, Windows Release
tests compare original HTTP client versus disabled decorator allocation calls
and requested bytes: both task construction/destruction and execution of an
invalid-URI error path match, with and without cancellation-token overloads.
The disabled server metric factory performs no measured C++ allocation. A
positive control with enabled metrics confirms the probe detects additional
instrumentation allocations. Production allocation and coroutine promise code
are unchanged. This evidence covers C++ allocations on the calling thread, not
malloc internals, successful network throughput, tail latency, other protocols,
other allocators or other platforms; those performance gates remain open.

Exporter retry delays now use an allocation-free saturating policy separate
from HTTP delivery. Numeric Retry-After seconds are capped before millisecond
conversion, oversized numeric headers saturate, malformed values fall back to
backoff, and exponential multiplication cannot wrap for extreme attempt counts
or duration values. Unit tests cover arithmetic boundaries. Real HTTP tests
exercise successful retry with a uint64-max header and permanent 503 failure
with a larger-than-uint64 header: attempts stop at the configured budget, the
batch is counted as failed, and flush completes without a stranded drain.
Flush completion means records were delivered or accounted as failed, not
guaranteed delivery. Allocation failure, task-scheduling exception isolation,
partial-success responses and extreme timer-deadline conversion remain open.

OpenAI listener parent resolution now uses explicit valid trace context or an
active operation keyed by both run ID and operation ID. Without either, the
observed operation itself is a root; no synthetic unexported parent is created.
Concurrent operations are not implicitly related merely by sharing a run ID.
Metrics-only listeners skip trace identity generation and the parent-context
index, and no longer retain an unused copy of start-event attributes. Regression
cases cover root/child relationships, invalid inbound identity, cross-run
operation-ID collisions, and metrics-only completion/token accounting. These
cases pass in the Windows Release `test_openai` executable; the rebuilt
`test_application_integrations` executable also passes in the OpenAI-enabled
configuration. Formatting and scoped whitespace validation pass. These
checks do not establish allocation or throughput parity for OpenAI, and lazy
event construction, exporter lifetime, sampling and stream boundaries remain open.

OpenAI terminal metric publication is now isolated from span export, with a
separate metric-failure counter. Span preparation and exporter callbacks share
one exception boundary; rejected-span diagnostic metrics cannot double-count
the rejection or escape. A malformed attribute container and throwing exporter
regression confirms subsequent operations still complete. A 96-position
single-allocation-failure sweep exercises terminal metrics and verifies that
observed metric failures do not suppress the span or prevent the next operation.
This exposed a local histogram initialization bug: moving bounds before count
allocation left a partially initialized histogram after bad_alloc. Both arrays
are now committed only after successful allocation. A separate 32-position
allocation sweep checks histogram reuse and rendering. Windows Release OpenAI
and allocation-regression executables pass. Earlier lifecycle bookkeeping,
global allocator/platform coverage and full no-loss supervision remain open.

OpenAI attribute export now uses fixed lookups for nonnegative integer token
counts, a bounded model string and a boolean stream flag. Arbitrary metadata,
tags, operation IDs and payload attributes are not serialized or traversed by
the exporter. Explicit detail capture does not relax this allowlist. Malformed
attribute containers are now ignored instead of dropping the span. Windows
tests cover default/opt-in detail capture, secret metadata and nested tool
arguments, field type rejection and bounded strings, alongside allocation-fault
regressions. Caller-owned event creation and copying still precede this export
boundary and require further lazy-path work; this is not global redaction or
whole-framework disabled-performance evidence.

Run scopes now provide synchronous lazy success/failure attribute factories.
Inactive or already completed scopes do not evaluate them; factory exceptions
drop optional attributes while preserving the original terminal event. Detail
arguments are borrowed string views and copied only inside active completion.
Chat, streaming chat, image and moderation terminal producers use this path.
Windows Release tests cover skipped evaluation, exactly-once completion and
factory failure. A 256-iteration allocation probe with long names/details shows
zero calling-thread C++ allocations for disabled scope construction, lazy failure
and destruction. OpenAI and allocation-regression tests pass. This does not cover
eager start attributes, every run-event caller, actual network throughput or
other platforms, which remain part of the full acceptance scope.

Lazy start factories now cover streaming chat, image and moderation scope
creation. Scope initialization catches preparation failures and retains source
invocation settings; destructor/move-assignment fallback no longer constructs an
object before entering guarded completion. Allocation injection exposed that
the bundled JSON destructor may itself allocate for nonempty containers, so
operation identity has moved to a dedicated run_event field instead of JSON.
The telemetry listener prefers that field and accepts the legacy manual-event
attribute as fallback. Windows tests cover lazy-start factory failure, disabled
start/end zero allocations over 256 iterations, a 48-position initialization
failure sweep, matching delivered start/end callbacks, preserved child settings
and destructor failure containment. These pass with the OpenAI suite. Arbitrary
nonempty JSON metadata still has an allocating destruction path in the bundled
library; complete failure isolation for such payloads remains unresolved and
must not be inferred from the metadata-free lifecycle tests.

Run-event dispatch now borrows the original event when configuration supplies
no missing context, tags or metadata. Empty event attributes default to null.
Enrichment retains explicit event overrides; recoverable preparation exceptions
deliver the original event once. Each observer has its own exception boundary,
and diagnostic logging suppresses exception text and contains logging failures.
Windows tests verify identity-preserving delivery, merged defaults without input
mutation and allocation-failure fallback. A preconstructed event with nested
JSON incurs zero calling-thread C++ allocations across 256 dispatches. OpenAI
and allocation-regression suites pass. This avoids unnecessary JSON ownership
on the fast path but does not resolve the bundled JSON destructor's allocation
behavior when owned nonempty payloads actually require copying or destruction.

Instant run notifications now expose a synchronous lazy event factory. The
model retry/governance emit helper accepts borrowed strings and a scalar stream
flag, constructing event strings and JSON only inside that factory. Unit tests
verify absent/null observers skip factories, recoverable factory failures do not
prevent later delivery, and real governed-model rejection preserves errors and
delegate call counts with disabled observation or a throwing observer. Stream
rejections retain their flag. A 256-iteration disabled notification probe records
zero C++ allocations on the calling thread. Windows OpenAI and allocation suites
pass. Arbitrary JSON destruction under memory exhaustion and whole-framework
performance/protocol acceptance remain outside this evidence.

Disabled HTTP tracing factories now return an empty middleware, matching the
metric factory convention. server::use ignores empty components during assembly,
so no per-request pass-through coroutine or callback is installed. Custom
middleware dispatchers must test empty callables before invoking them. A
256-iteration registration probe measures zero C++ allocations for disabled
trace/metric factories and registration. The real loopback HTTP fixture installs
both disabled components alongside active metrics, verifies unchanged 200/503
responses, no generated server trace identity and no response traceparent.
Windows Release tracing, HTTP observation and allocation regression tests pass
with OpenAI disabled. Full cross-platform throughput/tail-latency validation
remains outstanding; assembly evidence is not a throughput benchmark.

HTTP server trace preparation now catches recoverable context parsing/identity
and request-context copy failures before invoking the original route. Optional
response trace-header publication has a separate exception boundary. The factory
returns the prepared tracing task directly instead of adding an outer coroutine;
the middleware object must outlive its outstanding tasks. A 48-position allocation
sweep for both successful and throwing handlers verifies one route invocation,
at most one report, original exception identity and throwing-exporter isolation.
Tasks and the test bridge are created before fault injection to target tracing
execution rather than unrelated task allocation. Windows tracing, loopback HTTP
observation and allocation suites pass. This does not prove allocation failure
recovery before coroutine construction or full network/streaming completion.

HTTP server spans now use an owning terminal guard with borrowed coroutine-local
context. Successful completion, errors and destruction finish at most once.
Destroying an unstarted task emits nothing; destroying a started suspended
handler reports abandonment, releases its frame and does not invent HTTP 200.
System-error cancellation, timeout and connection failures preserve their error
codes and rethrow the original exception. OTLP omits unknown HTTP response status
instead of encoding zero. Windows tests cover task move/destruction, throwing
exporters, terminal classification and original error codes; real HTTP OTLP
reception verifies unknown-status omission. Existing preparation-fault and
loopback tests also pass. This establishes middleware task lifetime, not final
socket-write completion or safe cancellation of arbitrary outstanding I/O.

HTTP server duration measurements now distinguish system-error cancellation and
timeout instead of classifying both as generic exceptions, while rethrowing the
original error code and omitting an unknown response status. HTTP client observed
execution explicitly finishes trace and duration scopes before rethrowing:
ordinary exceptions are errors rather than abandonment, and system_error retains
its code/classification. Windows unit tests verify server cancellation/timeout
measurements and a 64-position client allocation-failure sweep verifies that
escaping allocation exceptions with recorded spans are errors with exception
metrics. Existing disabled-allocation and loopback behavior tests pass. Actual
client system-error/network cancellation coverage, final stream consumption and
whole-framework acceptance remain outstanding.

HTTP terminal error classification now uses one protocol-independent adapter for
standard error conditions and framework network errors. Framework
operation_aborted is cancellation, connection_timed_out is timeout, and a
connection_aborted remains an error. The source category and value are retained,
including zero-valued codes. HTTP client trace/metric paths and server exception
paths use the adapter without modifying transport results. Windows Release unit
tests cover both error families and a same-number unrelated category. The real
loopback client fixture exercises pre-cancelled requests with neither signal,
each signal separately and both enabled; original errors, no additional server
requests, throwing-sink containment and a subsequent successful request are
verified. Server tests preserve original thrown codes and omit unknown response
status. The instrumentation, HTTP tracing, HTTP observation and disabled-path
allocation executables pass. This evidence does not cover cancellation after
network I/O starts, real deadline expiry, full streaming lifetime or all-platform
performance and integration acceptance.

OTLP acknowledgements now use a transport-independent SAX reader with a 64 KiB
input limit and a 16-container nesting bound, without constructing an owned JSON
DOM or retaining collector diagnostics. Malformed structures, duplicate known
fields, invalid rejection counts and counts larger than the transmitted batch
fail validation. Partial acceptance is not retried. Collector-rejected spans,
log records and metric data points have separate counters; warning-only responses
retain all acknowledged records. Invalid acknowledgements increment invalid
responses and failed batches, never exported counts. Exported metric accounting
now uses actual data points in the cumulative snapshot, not input measurements.
Windows Release parser tests and a real three-signal HTTP collector validate
partial acceptance, warning-only acceptance, aggregation cardinality and invalid
acknowledgements with no retries. Application, HTTP observation and disabled-path
allocation suites also pass. The parser limit is not a transport receive-size
limit; decompression/receive bounds, full worker lifetime, periodic metrics and
all-platform delivery/performance acceptance remain open.

The OTLP client now selects HTTP/1.1 explicitly, disables cookies, requests
identity encoding, and installs a 64 KiB HTTP/1.1 body budget. Declared lengths
and cumulative chunk sizes are checked before body accumulation; chunk-size
lines and trailers are bounded. Failed bounded single sends close their
connection. Oversized bodies/headers are non-retryable invalid responses, and
unexpected content encodings are rejected without decompression. Windows raw
TCP fixtures omit the oversized payload and verify immediate connection closure
for declared-length, single-chunk and cumulative-chunk overflow; exact-limit
chunked JSON succeeds, and a gzip-labelled response fails. Exporter, HTTP
observation and disabled-allocation regressions pass. This is not yet a unified
HTTP/2/3 response budget, configurable exporter response policy, or comprehensive
HTTP framing validation: close-delimited, truncated and malformed framing paths
still need auditing. The default ordinary HTTP client leaves the new budget off;
successful network throughput and tail-latency parity remain unverified.

Raw TCP regression cases reproduced five false-success acknowledgements: a
truncated Content-Length body containing valid JSON, trailing garbage in a
length, trailing garbage in a chunk size, an invalid chunk delimiter, and a
missing final trailer terminator. The bounded HTTP/1.1 path now rejects these
responses before publishing a body. OTLP counts malformed headers/chunks as
invalid non-retryable responses rather than exported records. The same cases
now pass together with the exact-limit response, three-signal collector, HTTP
observation and disabled-allocation regressions on Windows Release. Ordinary
unbounded clients retain their previous parsing policy; this is not a claim of
complete HTTP framing hardening or cross-platform performance parity.

Bounded HTTP/1.1 reception now consumes close-delimited response bodies through
EOF, recognizing both zero-length reads and the framework EOF error. It checks
remaining capacity before appending, reads at most one extra byte to detect
overflow at the exact boundary, and closes the exhausted connection rather than
retaining it for reuse. HEAD, successful CONNECT and bodyless status responses
do not enter this new EOF loop. Windows raw TCP collector tests verify that a
valid close-delimited acknowledgement is exported and an oversized one is
rejected without retries; HTTP observation and disabled-allocation suites also
pass. HTTPS EOF behavior, informational responses, comprehensive framing rules,
other protocols and full cross-platform acceptance remain unverified.

Redis observed command and batch execution now explicitly completes escaping
exceptions as errors instead of destructor abandonment, preserves and classifies
system_error codes through the neutral adapter, and rethrows the original
exception. Windows Release tests inject allocation failures at 64 positions for
both command and request-batch execution, verify recorded escaping failures have
exactly one error outcome, and exercise throwing exporters. The Redis loopback
wire regressions also pass. Automatic pool observation remains absent: the
Application service owns a raw pool, pool_params has no sink binding, and raw
Redis command I/O lacks cancellation-token overloads. Its current probe reports
pool size rather than performing a remote health check. These are outstanding
integration/lifecycle requirements, not covered by the terminal-outcome fix.

Disabled Redis allocation coverage now includes five existing observed entry
points: span and initializer-list commands, request-builder execution, and span
and initializer-list pipelines. For each entry point, 256 iterations compare
raw versus empty-sink task construction/destruction and execution on a
disconnected client. Calling-thread C++ allocation counts and bytes match, as do
the original failure strings. Inputs and initializer-list backing storage remain
alive throughout execution. Windows Release allocation and Redis wire suites
pass. This proves those disabled entry paths only, not successful-network
throughput, tail latency, pool auto-configuration, other allocators/platforms,
or an absence of every CPU instruction overhead.

Exporter self-observation now has one failure-isolated hub refresh operation,
called by the Application health loop. It publishes all 18 statistics as local
absolute-snapshot gauges, including rejected signal records, worker failures,
partial/warning batches and invalid acknowledgements. Repeated refresh does not
add counts or recursively submit OTLP measurements. Windows tests verify local
OpenMetrics rendering of accepted/dropped and new statistic families, stable
refreshes, recovery after 32 allocation-failure positions, and zero measured C++
allocations across 256 refreshes with local metrics disabled. Application and
exporter regression suites pass. This validates the shared registry publication;
a live management-port scrape and external alert/dashboard integration are still
required. Existing _total names retain their gauge snapshot representation.

A Windows Application integration test now starts a real host with separate
loopback business and management ports and scrapes it with the HTTP client.
With local metrics enabled it waits for health-loop publication and verifies
exporter rejection, invalid-response and worker-failure families in the HTTP
response. With metrics disabled the scrape route returns 404. In both modes the
business port does not expose the management metric route, readiness returns
200, and an explicit stop joins the host successfully. This exercises automatic
host wiring rather than manually refreshing the hub. The new executable builds
and passes on Windows Release. Nonzero fault-to-scrape values, external scraping,
alerts, dashboards and cross-platform execution still require evidence.

The real Application scrape test now includes an in-process HTTP collector
fixture that returns one malformed acknowledgement and then a valid one.
Without directly refreshing the hub, management HTTP responses show one invalid
response, one failed batch, zero exported records and no retries after the first
log. A second submitted log is exported after collector recovery; subsequent
scrapes retain the historical failure counts and show two accepted logs and one
exported record. Collector diagnostics and log body content are absent from the
first scrape. Readiness remains available after the export failure, the collector
sees exactly two requests, and shutdown completes. Windows Release builds and
Application tests pass. This is a controlled local log-delivery fault, not proof
of broker recovery, all-signal failures, external alerting or full acceptance.

A cancellable Redis command-connection health primitive now sends PING using
token-aware transport I/O and validates the exact PONG incrementally with fixed
storage. It returns error codes without retaining server diagnostic payloads.
Invalid, interrupted, or exceptional exchanges close the connection; cancellation
before transmission preserves it. Pending buffered input is rejected without
consuming another exchange. This API requires exclusive command ownership and is
not a subscription/push-stream probe. Existing command and observation entry
paths were not redirected through it.

Windows Release Redis tests use real loopback TCP for PONG, malformed replies,
a silent peer with deadline cancellation and EOF verification, and pre-cancelled
operations with no transmitted command. Redis wire and disabled-observation
allocation regression executables build and pass. TLS and other platforms are
not verified by these tests. Application probe wiring remains outstanding:
the pool currently returns closed clients to its idle state, and its sleeping
maintenance tasks need structured wakeup/cancellation before a failed probe can
be safely recycled and promptly recovered. Merely calling PING from the current
service probe would not complete that lifecycle.

Redis pooled lease return now shares one state transition across its fast,
locked and deferred paths. An open client becomes idle; a closed client becomes
dead and is never published in the idle bitmap. Any maintenance coroutine waiting
for ownership release is resumed in either case. Loopback Windows tests verify
healthy-client reuse, rejection of a returned closed client, and rejection with
an already queued borrower. Redis wire and disabled-observation allocation
regressions pass after rebuilding. This does not establish pool throughput
parity, prompt recovery of sleeping maintenance tasks, or structured shutdown:
the test explicitly allows the existing short maintenance interval to elapse.
Health probe wiring remains pending those lifecycle requirements.

Redis maintenance now uses per-node cancellation tokens for idle/retry waits and
bounded PING I/O, with a separate token for the pool run loop. Returning a dead
lease wakes its sleeping maintenance operation; stopping the pool wakes run and
node waits and clears idle publication. A connection establishment completing
after stop is closed rather than republished as ready. Tokens are not attached
to ordinary user commands.

The Windows loopback lease tests now configure an hour-long heartbeat interval.
After invalidating a lease they acquire a replacement within a one-second
deadline and verify a real PING/PONG on a second accepted TCP connection, both
with and without a queued borrower. The run coroutine exits during the bounded
shutdown check rather than waiting for the heartbeat interval. Rebuilt Redis
and disabled-observation allocation tests pass. These results do not prove a
fully joined pool lifecycle: connection establishment remains non-cancellable,
maintenance coroutines still use detached spawn, and cancellation must still
drain outstanding borrowers and join all work before pool destruction. Pool
throughput and cross-platform/TLS validation also remain outstanding.

Pool stop now closes borrower admission and drains the queued waiter list under
the coroutine lock. It uses the same atomic completion claim as caller
cancellation, preventing a second resume when caller cancellation already owns
completion. Stop-owned waiters return operation_canceled without modifying their
tokens; previously claimed deadline cancellations retain timed_out. Async and
immediate borrowing after stop fail instead of waiting on a stopped pool, and
pre-cancelled async acquisition is rejected before leasing an idle connection.
Windows tests cover eight queued borrowers, caller and deadline cancellation,
idempotent stop, an empty final wait queue, and rejection of new borrowers. The
Redis regression passes. This is not a multithreaded race stress test or proof
that detached maintenance and connection establishment are joined at shutdown.

Redis close now resets receive-buffer contents, cursor and negotiated RESP mode,
and reconnect begins by releasing the previous session (including TLS state).
The buffer capacity and configured push callback are retained. This prevents
stale bytes or a previous TLS stream from being reused across a replacement
socket. Windows loopback tests send a surplus old-session reply and verify that
both explicit close/reconnect and direct reconnect return the new session's
response. Redis and disabled-observation allocation regressions pass. The tested
transport is plaintext; TLS session replacement and connection-establishment
cancellation still require direct validation. No claim of complete lifecycle
or successful-network performance parity follows from these scoped tests.

Redis connection establishment now owns a rollback guard until all configured
transport and protocol handshake phases succeed. Errors and exceptions reset
transport, buffered input and negotiated mode rather than leaving a partially
initialized session open. A real loopback AUTH rejection test verifies the
client reports failure, is closed, and the peer observes EOF.

The Windows OpenAI validation configuration now also enables Redis alongside
SSL/BoringSSL. The core library and Redis, OpenAI and disabled-observation
allocation executables build and all three test executables pass with current
sources. This exercises compilation of the token-aware Redis TLS branch but
the Redis wire fixtures remain plaintext; encrypted transport runtime behavior
is not proven. Existing executor nodiscard warnings remain. Cancellable DNS and
Redis handshake establishment, structured pool joining and the wider OTEL
acceptance gates remain outstanding.

A dedicated Redis TLS runtime test now performs verified TLS handshakes against
a loopback BoringSSL server, reads a PONG split across encrypted writes, cancels
a PING against a silent server, verifies closure on both sides, and reconnects
the same client for a successful second exchange. Both normal and timed-out
first sessions are exercised. CMake generates a test-only certificate/key in
the build directory using an explicit repository-owned OpenSSL configuration;
the client trusts that certificate and keeps peer verification enabled. No
private key is checked into source. The target requires Redis, SSL and an
OpenSSL executable; configuration reports its absence rather than counting an
unavailable TLS test as passed.

Windows Release test_redis_tls, test_redis, test_openai and
test_http_disabled_overhead pass. This adds encrypted PING and reconnection
evidence but does not validate cancellable DNS/TLS establishment, authentication
timeouts, a real Redis broker, other operating systems or complete pool shutdown.

Redis now exposes a separate cancellable, byte-bounded request exchange in
redis_exchange.cpp. It uses the incremental RESP parser without resetting its
state between network fragments, handles nested replies and pipelines, preserves
transport/parser error codes, and returns server errors as reply nodes. The
total received-byte budget spans the entire batch. Partial/invalid exchanges
close the client; pre-cancelled, empty or conflicting buffered exchanges fail
before transmission. Push frames are deliberately unsupported by this exclusive
command-connection API. Original cmd/exec entry paths remain unchanged.

Windows TCP tests cover fragmented RESP3 maps, server errors, malformed types,
silent-peer timeout and a pipeline exceeding the total budget. The TLS test's
reconnected session uses the new exchange and validates its encrypted PONG.
Redis, Redis TLS and disabled-observation allocation executables rebuild and
pass. This primitive is not yet wired into cancellable connection establishment;
DNS, TLS and Redis authentication cancellation plus structured pool ownership
remain open. General RESP conformance and other-platform coverage are also not
established by these cases.

Redis has a typed cancellable connect overload that passes one token through
Happy Eyeballs TCP attempts, TLS handshake and bounded HELLO/AUTH/SELECT
exchanges. Connection-pool establishment uses this overload with its configured
connect timeout and node maintenance token, so stop can interrupt handshake I/O.
Unsuccessful attempts roll back session state. The legacy string-error connect
entry remains available; TLS setup duplication between the two implementations
still needs consolidation without losing existing diagnostic behavior.

Windows tests verify timeout and peer closure while HELLO, AUTH or SELECT is
stalled. Verified TLS success/reconnection uses the new overload, and a separate
test withholds TLS handshake responses and observes timeout plus transport
closure. Redis, TLS and disabled-observation allocation regressions build and
pass. Blocking DNS resolution still must finish before cancellation is observed
by Happy Eyeballs; this is not a bounded DNS cancellation guarantee. Pool task
joining, full caller-cancel race coverage, and real-service/platform acceptance
remain outstanding.

Both Redis connection entry points now share private TLS configuration in
redis_tls.cpp, including trust stores, client identity and peer hostname setup.
A private failure record retains the original error code and static diagnostic
stage; the legacy entry formats its previous stage-prefixed string while the
cancellable entry returns the code. The existing legacy default-trust-loading
behavior is explicit rather than silently changed by this refactor. Network
handshakes remain separately cancellable or non-cancellable as requested by the
caller. No TLS implementation is exported from the module interface.

Windows TLS tests now connect the first session through the legacy entry and
the second through the cancellable entry. Verified connections, PING timeout,
reconnection and stalled cancellable TLS handshake tests pass. Both SSL-enabled
and SSL-disabled configurations rebuild Redis and disabled-observation allocation
tests successfully and those regressions pass. These scoped checks do not close
the remaining DNS cancellation, pool joining or framework-wide OTEL gates.

Redis connection maintenance dispatch now carries a shared RAII completion
ticket and uses guarded spawning. Tickets survive queued dispatch and are
released after completion or scheduling failure. Pool cancel waits for the
maintenance group, and async_run performs cancellation/join before returning or
rethrowing its first recorded maintenance failure. A pending_maintenance query
exposes that owned count without instrumenting ordinary command execution.

Windows tests assert zero pending maintenance immediately after cancel and stop
a pool during an AUTH exchange whose configured connect timeout is one hour.
They observe connection closure, joined maintenance and run completion without
waiting for that timeout. Redis and disabled-observation allocation regressions
build and pass. This does not yet cover allocation/dispatch failure injection,
multithreaded failure races, deferred waiter cleanup tasks or outstanding user
leases; those still prevent claiming that arbitrary pool destruction is safe.
Blocking resolver work remains a shutdown-latency limitation.

The allocation-instrumented Windows executable now injects bad_alloc at the
first twelve calling-thread allocations while initially resuming pool startup.
It disables injection, requests stop before dispatching queued maintenance,
polls completions under a two-second guard, and checks that both run and stop
tasks finish with zero pending maintenance. Propagated allocation exceptions are
observed through the run task. A failed completion assertion terminates the test
process rather than destroying frames still referenced by I/O. This tests the
startup/dispatch ownership path without a broker; it does not inject arbitrary
worker-thread failures or assert full startup transactional rollback/restart.
The rebuilt allocation executable and Redis regression pass on Windows Release.

Application Redis health now acquires an exclusive pool lease and performs a
real PING using the same absolute deadline and cancellation token for both
phases. Reports retain the error code but use fixed safe messages. The loopback
managed-service test covers PONG, an invalid/error reply containing a private
marker, and a silent peer whose connection closes on timeout. Service shutdown
joins the supervisor and leaves zero tracked pool maintenance operations.

The same test holds the sole pool lease while probing: acquisition times out,
the borrowed business connection stays open, and the waiter queue is empty on
return. After releasing that lease, an already-cancelled probe does not consume
the idle connection; a subsequent fresh probe receives PONG on that connection.
The Windows Release test executable was rebuilt and test_application_redis,
test_application and test_http_disabled_overhead all pass. This is direct
managed-service/loopback evidence, not a real Redis container or actuator
readiness recovery end-to-end acceptance. Disabled-path allocation regressions
do not establish framework-wide throughput or tail-latency parity. Sampling,
automatic protocol observation and coroutine context propagation remain open.

The shutdown-order regression now follows the host's actual order: request
supervisor cancellation before redis_service::stop. It first failed because
the Redis task ignored the supervisor token and only pool.cancel could release
its long idle wait. A nonblocking cancellation adapter can now be registered
with a supervised operation. Redis connects that adapter to a permanent
request_stop signal; the existing run coroutine cancels and joins maintenance
before the supervisor marks it stopped. No watcher coroutine or periodic stop
polling was added to production code, and ordinary Redis commands are unchanged.
An additional test requests stop twice before dispatch and verifies no pool
nodes or maintenance tasks start. Pool start/stop publication preserves a stop
request arriving before initial token setup. These tests do not establish
arbitrary concurrent cancellation safety of all underlying platform awaiters,
pool restart correctness, or full application shutdown deadlines.

Supervisor stop dispatch no longer constructs a temporary vector inside its
noexcept request_stop path. Registration rechecks the stopping flag while
holding the membership latch; a stop-side barrier then makes map membership
immutable before cancellation adapters run outside that latch. A 32-entry
allocation-instrumented test arms immediate allocation failure during stop,
verifies zero allocations, exactly-once adapters and reentrant state queries,
then dispatches and joins all cancelled tasks. Application tests also race
registration against stop for 64 trials and require every accepted task to
receive cancellation. This stress test does not enumerate all interleavings.
The rebuilt Windows Release Application, Application Redis and allocation
regression executables pass. Adapter/platform cancellation implementations can
still allocate independently; the zero-allocation assertion covers supervisor
dispatch with the tested nonallocating adapters, not every shutdown operation.

Supervisor state/error queries now use heterogeneous string_view lookup with
matching string/string_view hashing, avoiding temporary owning strings inside
noexcept methods. The allocation-failure stop test now uses 128-character name
prefixes and checks both present and absent names through both query methods
from cancellation adapters. It still records zero allocations. System errors
raised by supervised operations preserve their original error_code rather than
being replaced by io_error; a zero-budget failure test verifies the exhausted
callback and last_error retain permission_denied. Windows Release rebuild and
the Application, Redis Application and allocation regressions pass. Other
exceptional supervisor paths (dispatch failure, callback/logging failure and
completion ownership) still require separate hardening and fault injection.

Supervisor completion is now owned by a shared RAII ticket retained through
guarded dispatch. It holds supervisor implementation lifetime and decrements
the wait group once, including wrapper allocation failure and execution/report
exceptions. A terminal reporting exception does not overwrite an already
recorded service failure or invoke its recovery callback twice. Tests verify
join completes after a throwing exhausted callback, preserving the original
connection_refused error. Allocation injection at the first twenty registration
and dispatch allocations verifies join completes under a two-second guard and
leaves no registered task in starting/running state. Windows Release rebuild and
the three related regression executables pass. This is not exhaustive worker
failure injection or proof of bounded joining for a task that ignores cancellation;
join still exposes completion rather than aggregating terminal service errors.

Supervisor join now returns terminal required-task failures after every owned
completion finishes. Optional failures remain queryable without failing join;
multiple required failures select the lexicographically first task name for a
deterministic original error_code. Application shutdown consumes this result,
continues stopping services and flushing telemetry, and does not overwrite that
earlier failure with a cleanup error. Tests exercise required/optional pairs
and allocation-failure join results. A real application_host run with a fake
managed service verifies a required worker's permission_denied failure triggers
shutdown, the service stop hook executes and returns io_error, and host.run
still returns permission_denied. Telemetry is disabled in that lifecycle test.
Windows Release build and three related regression executables pass. This does
not cover ignored cancellation, all-platform shutdown deadlines or the remaining
protocol/context/sampling/telemetry delivery acceptance requirements.

Supervisor recovery timing now starts at the first observed operation failure,
not initial task startup. Retry delays are capped by the remaining budget and
the deadline is checked again before another attempt begins. Successful timer
completion no longer resets the cancellation token and erases a concurrent stop
request. Tests let a task run for 100 ms before failing with a 50 ms recovery
budget: a short backoff still permits recovery, whereas a configured one-second
backoff is cut short and performs no extra attempt. Windows Release rebuild
and Application, Redis Application and allocation regressions pass. This bounds
retry scheduling, not execution of an already-running recovery attempt that
ignores cancellation; that requires a separate operation deadline contract.

Recovery policy validation is shared by configuration validation and direct
supervisor registration through a separate declaration/implementation module.
It rejects NaN/infinite multipliers or jitter, invalid ordering, nonpositive
delays, negative budgets and durations beyond the scheduling horizon. Direct
supervisor zero-budget operation remains supported as no retries; configuration
retains its existing positive-budget rule. Invalid direct registration is tested
across nine cases and leaves no task or pending join behind. Supervisor jittered
delay is capped before integer conversion and cannot exceed maximum_delay.
Windows Release was explicitly reconfigured for the new implementation unit,
rebuilt and passed the three related regression executables. Custom managed
service policy validation and other recovery consumers still require audit.

Both untyped and typed managed-service registration now validate custom service
recovery policies before mutating registry membership. Typed registration also
rejects empty managed identities before installing its interface binding.
The custom-policy test rejects NaN jitter through both APIs with both registry
counts still zero, rejects builder construction before route configuration or
service startup, and successfully registers the same service after correction.
Windows Release tests pass in the SSL/OpenAI-enabled validation build and in a
freshly reconfigured HTTP+Redis build with SSL and OpenAI disabled. The latter
is not the all-protocols-off matrix; both ran Application, Application Redis
and disabled-observation allocation regressions.

The all-protocols-off Windows configuration was revalidated after the recovery
module changes: HTTP, Redis, OpenAI, all other protocols, SSL and ORM are OFF.
Explicit CMake regeneration, cnetmod_core and five test targets build cleanly;
instrumentation, metric aggregation, deadlines, task groups and task tests pass.
The generated core target excludes src/application and src/observability units,
while retaining protocol-neutral instrumentation. This verifies this feature
combination on MSVC Release only, not all platform/feature matrices or runtime
performance parity of disabled observation in protocol-enabled applications.

Head sampling now occurs at operation creation in operation_scope, HTTP server
tracing and the OpenAI telemetry listener. Child operations inherit the parent
sampled flag; changing the hub ratio affects new roots, not in-flight decisions.
Unsampled scopes retain propagation context, skip lazy annotations and do not
export completed spans. Root sampler exceptions are contained. Hub root sampling
uses a deterministic trace-ID fraction rather than implementation-defined hashing.
Low-level completed-span submission is not a sampling entry point: producers
must decide at creation. Initial span factories still construct metadata before
sampling, so allocation-free unsampled recording is not yet established.

MSVC Release builds and five test executables pass for this change:
instrumentation, HTTP tracing, disabled-observation overhead, OTLP exporter and
OpenAI. Tests cover parent decisions, sampler exceptions and ratio changes while
operations are active. The ORM template sampling changes have not yet been
compiled in an ORM-enabled configuration. This is not evidence of complete
cross-protocol instrumentation or cross-platform closure.

The ORM database_session tracing overloads now depend on protocol-neutral
instrumentation rather than HTTP middleware. Empty sinks return the original
query/execute task directly; they do not copy a sink, construct trace metadata or
allocate an observation coroutine frame. Installed sinks use operation_scope for
once-only completion, exception isolation and parent sampling. Query attributes
are lazy and skipped for unsampled operations. System exceptions retain their
original error code and are rethrown even when the exporter itself throws.

The MySQL+ORM-enabled MSVC Release validation build passes the ORM result-map
tests and disabled-observation allocation executable. New tests cover query and
execute exception preservation, parent suppression, and equal allocation counts
and bytes for both unstarted tasks and completed operations with observation
disabled (including a 4096-byte SQL input and text capture requested). These
measurements do not establish throughput or tail-latency parity.

The separate full-feature cmake-build-mvsc-release build failed in bundled ICU
data generation before framework compilation. This gate remains unresolved;
the narrower ORM validation is not a substitute for full-feature acceptance.

Bound ORM execution now exposes the same explicit observation overload as raw
SQL. It owns the parameterized statement across suspension and shares terminal
handling with query/execute. A fake PostgreSQL-style client verifies lazy wire
dispatch, unchanged binding values and placeholder-only text export when text
capture is explicitly enabled. Binding values are never added as attributes.
The disabled allocation comparison also covers bound execution, including both
unstarted task frames and completed operations. Both ORM and allocation test
executables pass in the MySQL+ORM-enabled MSVC Release validation configuration.
This does not validate actual database wire interoperability.

ORM observation now starts inside the executing coroutine, not when its lazy
task is created. Pending tasks own copied parent/sink inputs; failed observation
input copies are contained and leave database execution available. Discarding
an unstarted task produces neither sampling nor export. Tests verify the start
timestamp is no earlier than first execution, sampling reads the policy at that
point, and a throwing sink copy cannot prevent query/execute. MSVC Release ORM
and disabled-allocation regression executables pass after this timing change.

A fresh Windows Release build (cmake-build-mysql-no-http) enables MySQL and ORM
with HTTP, SSL and all other protocols disabled. The core library, ORM test
executable and instrumentation executable build and both tests pass. ORM tests
now import protocol-neutral tracing directly. This proves the tested database
observation path does not require the HTTP/OTLP transport modules. Test selection
also excludes the HTTP client test when HTTP is disabled, and excludes the ORM
test when either its ORM or MySQL prerequisite is disabled. A matching Windows
CI gate was added locally; it has not been pushed or executed on GitHub.

The full-feature Windows ICU prerequisite failure was investigated separately.
Release data generation was invoking a Debug pkgdata executable from ICU's
shared bin64 directory. CMake now restages matching configuration-specific tools
with copy_if_different before data generation, and maps non-Debug configurations
to ICU Release. A second independent issue was a malformed local icudt78.dll:
dumpbin could not parse its export table. Rebuilding ICU stubdata once restored
the bootstrap DLL; the normal cnetmod_icu Release target then regenerated data
successfully. Its final export table contains icudt78_dat. This unblocks the
prerequisite locally, not the still-pending full framework/platform acceptance.
No automatic damaged-artifact recovery or concurrent ICU build isolation has
been established by this fix.

After ICU recovery, the broad MSVC Release configuration builds cnetmod_core
and passes Application, OTLP exporter, ORM result-map and disabled-observation
allocation executables. PostgreSQL, MongoDB, Kafka, MQTT, both AMQP versions,
gRPC, HTTP, OpenAI, Redis and MySQL are enabled in this build. QUIC is actually
disabled by configuration because BoringSSL QUIC support is unavailable despite
the cached QUIC option being ON; this run is not HTTP/3 coverage. These four
tests are not evidence of real-broker recovery or all integration correctness.

Inspection during this build found unresolved shutdown wiring: MySQL's pool
task ignores the supervisor cancellation token; MongoDB maintenance uses a
separate stop_source signalled only in service stop. Host joins supervised tasks
before service stop, so these paths need cancellation adapters and bounded
maintenance waits, with direct shutdown tests. ICU's repeated data generation
and runtime deployment also remain build-I/O optimization work.

MongoDB service maintenance now receives supervisor stop through an explicit
adapter. Its stop_token wakes the interval timer through a coroutine-owned
cancel_token and stop_callback; a non-cancellation timer failure propagates to
the supervisor instead of silently spinning. Cancellation is rechecked between
health checks and pool warmup. Windows tests pass for an hour-long maintenance
interval interrupted after 10 ms, pre-start cancellation, and real mongodb_service
start -> supervisor stop/join -> service stop using an empty pool. This verifies
idle maintenance shutdown, not cancellation during active MongoDB network I/O.
MySQL pool cancellation wiring and active MongoDB maintenance I/O remain open.

MySQL's primary pool maintenance loop now exposes a permanent, nonblocking
request_stop signal and cancellable warmup/periodic timer waits. Application
supervision forwards stop to this signal. Pre-start stop is retained, and timer
failures unrelated to cancellation propagate. MSVC Release compilation and ORM
tests pass, including empty-pool stop before startup and interruption of an
hour-long interval. This is not complete MySQL pool shutdown: per-connection
tasks remain separately spawned, their I/O/idle waits are not joined by the
primary task, and outstanding leases require explicit lifetime handling.
Recovery that restarts a stopped pool also needs a defined reconstruction path.

MySQL lease-return inspection found a temporary capturing coroutine lambda in
the contended-lock fallback. It is replaced with a named member coroutine so
the node pointer and reset flag are coroutine-frame parameters rather than
members of a destroyed closure. Its acquired metadata lock now uses RAII.
MSVC Release compilation and existing ORM/pool stop regressions pass. They do
not directly exercise a real borrowed connection returned under lock contention;
that branch still needs a dedicated regression. The member coroutine still
requires pool/node lifetime ownership and joining of asynchronous return work.

MySQL connection creation now constructs the client before publishing its pool
slot and rolls back the new slot if the connection coroutine cannot be created.
An RAII guard clears the primary running flag on exceptional exits as well as
normal shutdown. A Windows allocation-fault regression injects bad_alloc at the
first client allocation, verifies no null slot remains, and verifies subsequent
try_get_connection does not grow an erroneously running pool. The allocation
executable builds and passes. Failures after dispatch, already-running siblings,
and complete shutdown ownership still require further work.

MySQL primary-loop normal shutdown now invokes pool cancellation, which wakes
each connection's idle/retry timer. Reconnect backoff uses the same per-node
cancellable timer instead of an uninterruptible sleep. Connection tasks check
stop after connection completion and before arming another maintenance wait.
MSVC Release builds and existing ORM/empty-pool/allocation regressions pass.
These tests do not prove nonempty-pool drain: active wire I/O, borrowed leases,
return tasks and child completion accounting are still unresolved. Exceptional
primary-loop exits also still need structured sibling cancellation and joining.

MySQL cancellation now removes queued acquisition waiters under the metadata
lock and posts only those claimed through their one-shot pending flag. Queued
acquisitions return operation_canceled; both synchronous and asynchronous new
acquisitions reject a stopped pool before selecting an idle connection or
growing demand. The cancellation lock uses RAII. A Windows regression verifies
one queued waiter is resumed, the queue count reaches zero and both post-stop
entry points reject immediately. Concurrent caller-cancel/pool-stop races still
need dedicated stress tests, and child network/task ownership remains open.

MySQL waiter cancellation's contended-lock fallback no longer uses a temporary
capturing coroutine lambda. A member coroutine owns its waiter/handle arguments,
unlinks under an RAII lock and posts completion after releasing the lock. A new
32-waiter test interleaves caller cancellation, pool stop and repeated cancellation
on the owning event loop; every request completes once and the FIFO drains.
MSVC Release compilation and ORM regressions pass. This deterministic ordering
test does not exercise cross-thread lock contention or allocation failure in the
noexcept cancellation callback; those remain open alongside structured joins.

Added a cross-thread MySQL waiter regression: a separate thread cancels 16
armed requests while the owning event loop closes the pool. Each test executes
32 rounds and checks exactly-once completion and an empty queue. Ten consecutive
MSVC Release runs passed (320 rounds, 5,120 requests). The ORM test has a 15-second
CTest timeout to bound regressions in CI. This is probabilistic stress evidence,
not forced lock-contention coverage or proof of cancellation allocation safety;
active connection shutdown and structured child joins remain unresolved.

The MySQL cancellation cleanup design was simplified further: the cancellation
callback claims the waiter once and posts its original coroutine directly. That
coroutine already owns the stack waiter and now unlinks it under an RAII lock
before returning, so no helper coroutine or mutex acquisition is needed in the
callback. The previous remove_waiter_async helper was removed. Pool stop and
assignment still honor the same pending claim. Ten repeated Windows ORM stress
runs pass after this change (another 320 cross-thread rounds). io_context::post
allocation/failure behavior remains an independent cancellation-safety concern;
this change does not claim allocation-free scheduling or full pool lifetime safety.

MySQL waiter completion now embeds a caller-owned post_node in the waiting
coroutine frame. Both caller cancellation and pool stop schedule through the raw
queue instead of allocating a post wrapper. Windows allocation-fault tests cover
both paths with an empty pool and an uncontended metadata lock. The dispatcher
captures node ownership before invoking callbacks or resuming coroutines, since
dispatch may destroy the storage containing a raw node. Two scheduler regressions
cover self-releasing storage and mutation of ownership during dispatch. The raw
queue lifetime contract is documented in an English API block comment.

Pre-cancelled MySQL acquisitions now return immediately without joining the FIFO,
preserving caller-cancel versus deadline error categories. Failed demand-driven
connection creation rolls back its pending-request count. The pre-cancellation
regression and scheduler/ORM/allocation targets build and pass on MSVC Release.
The cross-thread ORM stress suite also passed ten consecutive runs after the raw
node change. Application, OTLP, HTTP tracing, instrumentation, OpenAI and MongoDB
targeted regressions were rebuilt and passed during this verification round.
These are local targeted checks, not full-platform or real-database acceptance;
nonempty pool shutdown, active I/O cancellation and structured child joins remain
open. A new allocation test initially resumed a completed coroutine through the
repeated measurement helper; it now uses a single-operation allocation window.

MySQL connection maintenance now uses spawn_guarded to report child exceptions
to the primary pool task rather than terminating the process. Primary maintenance
runs inside an exception boundary; failure requests cancellation and drains queued
acquisitions before rethrowing the original exception to its supervisor. Wrapper
creation failure rolls back the unpublished connection slot. Windows allocation
tests inject failure at twelve startup allocation positions, request stop before
network work executes, and verify primary completion and rejected new acquisition.
A separate regression verifies startup failure releases a previously queued
acquisition. Both allocation and ORM targets were rebuilt and passed; an initial
transient compiler object-file access denial disappeared on retry. These tests
deliberately avoid network I/O and do not establish complete child joining or
nonempty-pool drain. The client still needs an active-operation cancellation
contract before the pool can guarantee bounded network shutdown.

MySQL client connect and ping now have explicit cancellation-token overloads.
A scoped binding forwards the token through Happy Eyeballs, authentication packet
reads/writes and the encrypted transport branches, restoring the previous binding
on normal or exceptional completion. Existing token-free entry points remain.
Pool maintenance supplies a per-node network token, cancelled alongside idle and
retry timers during pool cancellation. Connect and handshake failures preserve
their transport error in last_error(). A real Windows TCP regression accepts a
connection, withholds the MySQL greeting and keeps the peer socket open while
cancelling the client: connect completes with an error and the client is closed.
The ORM and allocation targets pass with SSL enabled, and the ORM regression also
builds and passes in the HTTP/SSL-disabled MySQL configuration. No real encrypted
connection is claimed by this test. Blocking system DNS resolution may still have
to return before cancellation completes; active child joins, lease ownership,
operation deadlines and complete restart semantics remain unfinished.

The primary MySQL run task now waits for its connection-worker completion group
after cancellation and before returning or rethrowing failure. Shared completion
tickets cover worker dispatch, execution, wrapper allocation failure and discarded
dispatch. Pool cancellation also wakes workers suspended waiting for a borrowed
connection; it no longer overwrites the in-use lease state with dead. A real TCP
regression stops a pool whose worker is waiting for a silent greeting, awaits the
primary task, destroys the pool and drains queued callbacks while keeping the
server-side socket open. Windows ORM tests passed ten repeated runs, allocation
fault tests pass, and the HTTP/SSL-disabled MySQL build and ORM tests also pass.
This is connection-worker joining, not complete pool ownership: asynchronous
lease-return work, borrowed-client lifetime, sharded run tasks, bounded DNS and
operation deadlines still require closure and broader sanitizer/integration tests.

MySQL sharded async_run now retains a completion group until every dispatched
shard lifetime task exits. Each shard reports exceptions into its own slot and
requests stop on all shards; startup failures stop and join already-dispatched
work before propagation. Cancellation requests are forwarded through thread-safe
request_stop rather than executing another shard's metadata cleanup on the
caller's event loop. Empty three-shard lifetime and sixteen startup allocation
fault-position regressions build and pass on Windows alongside ORM tests. These
tests share one event loop; real multi-worker, borrowed-lease and sanitizer
coverage remain pending. The lifetime API change is recorded in the migration
document; production example ownership and generated skill documentation still
need updating before complete acceptance.

Added real multi-worker lifecycle coverage for the sharded MySQL pool: two
independent event loops run on two threads; a third thread requests shutdown
after both loops have processed a timer. The test joins both threads, checks
primary completion without an exception, destroys the pool and drains remaining
callbacks. Each execution runs 32 rounds; ten consecutive MSVC Release ORM test
runs passed (320 dual-worker rounds). This closes the missing empty-pool
multi-worker test, not the outstanding real-database, borrowed-lease, race
sanitizer or production-example migration requirements.

The MySQL skill now documents cancellable connect/ping and the sharded lifetime
contract. Its startup-only await example was replaced with a scoped workload
joined with async_run through when_all; a stop guard handles both success and
exception, and leases have explicit scopes before stop. AGENTS.md was regenerated
and its repository consistency check passes. The generic skill validator does
not support this repository's frontmatter-free skill index. A compiled Windows
regression exercises this empty-pool workload pattern for normal completion and
an exception, verifying join completion and original exception propagation.
The ORM test passes. Real database workload execution and production server
example ownership remain separate unfinished acceptance items.

Application MySQL health no longer treats an allocated pool node as a healthy
connection. Probe now acquires a lease and performs a cancellable COM_PING within
the service-context deadline, reporting only fixed messages and error codes.
Startup acquisition also respects that deadline directly. A Windows integration
regression starts maintenance against a silent TCP peer, verifies that the pool
contains a node, and checks that health reports down/timed_out instead of up.
It then stops and joins the supervisor. The Application target builds and passes.
Real successful authentication/PING and outage recovery remain unverified by
this negative-path test; the overall observability/application goal is still open.

Added an explicitly opt-in real MySQL Application test covering authentication,
three successful health PINGs, supervised stop/join and stopped health. It uses
only loopback and performs no SQL writes. CTest gives it a 30-second limit and
skip code 77 when not enabled; missing credentials after opt-in are failures.
The Linux workflow now provisions a disposable MySQL 8.4 service and invokes
the live test explicitly. Windows compilation and the default skip behavior are
verified, and workflow YAML parses with executable steps. This machine has no
discovered Docker/MySQL server executable, so live interoperability and the new
CI gate are not yet executed or claimed passing. No changes were pushed.

The optional HTTP decorator now owns a parent-context snapshot in its observed
coroutine frame instead of borrowing the caller's context until first resume.
Only tracing snapshots the supplied context; metrics-only uses an empty context,
and fully disabled dispatch still returns the raw client task directly. Failure
to copy the snapshot or allocate the observed task falls back to raw dispatch.
Tests mutate the caller's parent before executing ordinary and cancellable sends
and verify the original trace/parent IDs. A fault-injection regression verifies
snapshot allocation failure preserves the raw transport error and emits no span.
Windows HTTP observation and allocation suites build and pass (after retrying one
transient compiler object-file access denial). This preserves tested disabled
allocation parity; it is not an end-to-end throughput or latency proof.

Redis observation entry points now accept the exporter by const reference and
copy it only inside their exception-isolated observed dispatch. Parent/sink copy
or observed-frame creation failure falls back to the original command/batch task;
the empty-sink direct path is unchanged. A throwing-copy exporter regression
covers both command argument forms, exec and both pipeline forms, comparing the
result against the raw disconnected-client result. Windows allocation parity and
Redis regression suites build and pass. This validates fault isolation at these
entry points, not full-network throughput or all protocol observation coverage.

HTTP client method attributes and Redis command/pipeline attributes now use
operation_scope::annotate after head sampling instead of being materialized in
the initial span factory. Unsampled operations retain their propagation identity
but skip these attribute vectors and batch-count formatting. A measured Redis
command/pipeline regression checks sampled versus unsampled allocations over
256 operations, verifies fewer calls/bytes for unsampled work, and verifies no
unsampled export. Windows HTTP observation, allocation and Redis suites pass
after a transient library-file lock was resolved by retrying the build. Full
disabled dispatch is unchanged; remaining identity/name and context costs are
not claimed eliminated, nor is this a network throughput benchmark.

Application lifecycle and health counters now use telemetry_hub's noexcept
increment_local_counter boundary instead of directly allowing registry allocation
errors to escape into service startup, shutdown, recovery or health processing.
The helper returns before lookup/allocation when local metrics are disabled.
An allocation regression verifies zero disabled allocations, contains an enabled
counter allocation failure and verifies a subsequent counter can still be rendered.
Windows Application and allocation suites build and pass after a transient library
lock was resolved on retry. This covers the local counter boundary and migrated
call sites, not every remaining observability side effect or full lifecycle
fault-injection acceptance.

Telemetry adapter construction now has a noexcept boundary for both spans()
and measurements(). A callback allocation failure returns an empty adapter, so
constructing observation before a lifecycle or health operation cannot itself
prevent that operation. Fault injection covers four allocation positions,
subsequent successful adapter creation, and allocation-free creation after close.
The test explicitly configures a trace endpoint but does not submit network data.
Windows Release builds and all seven targeted suites pass: instrumentation,
Application, HTTP observation, allocation fault injection, OTLP, Redis and ORM.
Changed C++ files pass clang-format validation; generated AGENTS.md is current.
These results do not establish full protocol coverage or cross-platform acceptance.

Application lifecycle and health spans now construct service attributes through
operation_scope::annotate after head sampling. Disabled and unsampled health
checks no longer call the service identity API for telemetry metadata; sampled
checks copy the identity only once for both attributes. A three-mode regression
checks actual probe execution, identity-read counts, readiness and zero accepted
records for disabled/unsampled tracing. The rollback and optional-recovery tests
now configure a trace endpoint and assert that the enabled branch really has a
span sink (the earlier empty-endpoint setup only enabled local metrics).
Windows Application and allocation-fault suites build and pass. This is not a
throughput benchmark or evidence that all protocol instrumentation is complete.

ORM observation input-copy failure now returns the raw query/execute task,
including the owning parameterized-statement overload, rather than allocating
an empty observation coroutine. Allocation fault injection exercises all three
entry points and verifies successful database results, no failed-attempt exports,
and resumed tracing on a subsequent successful operation. Windows observation
allocation and ORM suites pass. MySQL+ORM with HTTP/SSL disabled also builds and
passes its ORM suite. Allocation of the successfully configured observation
coroutine itself remains a separate unisolated boundary; this change does not
claim to solve that case or establish full enabled-path allocation parity.

The ORM observation-frame boundary is now isolated as well. The dispatcher first
allocates a coroutine referencing its still-owned inputs, then primes only a
noexcept ownership transfer into frame locals before an explicit suspension.
Instrumentation and database work remain lazy, and source references are never
accessed after that suspension. Failed frame allocation leaves SQL and bindings
available for the original task; no extra heap envelope or binding copy is used.
Regression tests verify allocation-free sink copying before injecting the frame
failure, all three entry points, intact bound values, no early database work,
discarded-task silence and subsequent successful traced execution. Existing
disabled allocation-parity tests pass. Windows Release ORM/allocation tests and
the HTTP/SSL-disabled ORM build/test pass. This supersedes the preceding frame
allocation limitation, but does not prove overall network performance parity or
cross-platform lifetime correctness for every protocol.

Arch Clang 22.1.8 Release validation now uses the independent build directory
/tmp/cnetmod-orm-observation-validation with HTTP, Redis, MySQL, ORM and OpenAI
enabled, SSL and mimalloc disabled. Fresh compilation exposed missing direct
tracing imports in telemetry and its allocation test, and recovery_policy imports
in service integration interfaces; these are now explicit. OpenAI moderation
guardrails now initialize the input vector with one string rather than assigning
a string to the vector. The three requested test executables build successfully.
Application and ORM tests pass on Linux. The allocation suite aborts with
std::bad_alloc in redis_observation_preserves_allocation_exceptions_as_errors,
after the preceding Redis disabled-path and sampling tests pass. This remains
under investigation; the full allocation suite and newer ORM frame-fault case
have NOT yet passed on Linux. No debugger is installed at /usr/bin/gdb.

The Linux allocation abort was traced to epoll_context::add allocating underneath
a noexcept I/O await_suspend, not to the Redis span completion callback. The map
also published an empty unique_ptr slot before allocation completed. Registration
now allocates ownership before publication and translates bad_alloc to the exact
not_enough_memory error code. A direct regression injects registration allocation
failures and retries the same descriptor to verify no empty slot survives. The
MySQL startup test accepts this precise system_error code as well as bad_alloc.
The temporary terminate/backtrace diagnostic has been removed. All 38 Linux
allocation tests now pass, including Redis error propagation and ORM frame-fault
ownership. The previous /tmp build disappeared between WSL sessions; the current
persistent verification directory is /root/cnetmod-orm-observation-validation.
Real valid-descriptor readiness/cancellation stress remains outside this new
invalid-descriptor allocation regression.

The epoll registration recovery test now also uses a real readable eventfd.
Each allocation position is followed by registration/re-registration and polling:
the task remains lazy until readiness, resumes exactly once, is not repeated on
the next poll, and registration cleanup succeeds. All 39 Linux allocation tests
pass for 20 consecutive runs in the persistent Arch build. The Windows allocation
target also builds and passes with the Linux-specific test excluded. This closes
the valid-descriptor readiness gap noted above, but not concurrent directional
cancellation, descriptor reuse stress or end-to-end network performance acceptance.

MySQL's single connection_pool maintenance entry now uses an atomic active-run
guard held through connection-worker joining. A duplicate run fails with
operation_in_progress before it can mutate failure state or stop the owning run.
The guard releases on normal completion and exception. A regression rejects three
duplicate runs, confirms the owner remains active, then verifies stop/join and a
subsequent stopped no-op run. Linux ORM and allocation suites pass. This is a
prerequisite safety invariant, not stopped-pool recovery: replacing a pool with
outstanding borrowed leases and the analogous sharded entry guard still need work.

The sharded MySQL maintenance entry now rejects duplicate runs before allocating
worker state or dispatching any shard. Single and sharded pools share an internal
maintenance_run_guard, preserving ownership through joined completion. The new
three-shard regression rejects repeated dispatches both before and after queued
workers execute, verifies the original task remains active, and verifies final
stop/join and guard release. Linux ORM/allocation tests and Windows ORM build/test
pass. This closes the missing sharded entry guard, not pool generation replacement
or recovery with outstanding borrowed connections.

MySQL lease return now wakes the connection worker through its existing embedded
task_completion node, matching pool cancellation, instead of allocating a new
post node. Both the immediate and mutex-contended return implementations exchange
the waiting handle before enqueueing. Linux ORM/allocation suites and Windows
ORM build/test pass. These existing suites are not a direct real-server borrowed
lease return fault test. The contended path still launches return_connection_async
without joined ownership; that and pool destruction while leases remain borrowed
are explicit unresolved lifecycle hazards before safe generation replacement.

MySQL's contended return coroutine now uses the same start_worker registration as
connection maintenance. The shared implementation owns a completion ticket,
reports asynchronous failures through maintenance_failure_, requests stop, and
participates in the existing worker join. Connection startup still rolls back
its published node when worker setup fails. Linux and Windows ORM/allocation
suites build and pass, exercising the shared startup registration fault paths.
A dedicated real-lease contention test is still missing. This does not close
returns registered after the maintenance join has completed, borrowed-handle
lifetime across pool destruction, or allocation failure during lease destruction;
those require a shared pool-state ownership design rather than another counter.

### Live MySQL validation on the 114 host (2026-09-12)

The BT Panel API created and verified the isolated `cnetmod_otel_test` database
and its dedicated `cnetmod_otel_t` account, restricted to loopback access. The
Windows Release `test_application_mysql_live` executable connected through an
SSH loopback tunnel, passed once, then passed ten consecutive CTest runs
(`--repeat until-fail:10`, 30-second per-test timeout; approximately 0.20 seconds
per run). Credentials were passed through process environment variables and
were not written into the repository or test output.

The test covers authentication, three PING health probes, maintenance join while
a connection lease remains borrowed, stopped-pool acquisition rejection, lease
return, zero remaining acquisition waiters, and stopped-service health. Existing
business databases were not modified. The test database is retained for further
validation. This does not prove outage recovery, contended lease destruction,
cross-platform live integration, OTLP export, or disabled-mode throughput parity.

With ORM enabled, the live MySQL test additionally compares a successful SELECT
and a server-rejected SELECT through the raw session, empty span sink, recording
span sink, and throwing span sink. It verifies identical result rows, error code,
SQLSTATE, and error message, plus parent/child span identity and default exclusion
of query text and returned content. The rebuilt Windows Release target passed ten
consecutive runs on the same isolated 114 database (approximately 0.35 seconds
per run). This uses SDK-neutral completed-span callbacks, not a live collector;
it is behavioral evidence, not a throughput or tail-latency benchmark. The ORM
test branch is explicitly conditional so MySQL/HTTP-only builds do not import ORM.

The live parity assertions now also require the successful baseline to return
exactly the expected scalar and the rejected baseline to report MySQL 1054 /
SQLSTATE 42S22. Recorded spans must distinguish success from failure, preventing
an equally broken set of paths from passing the comparison. The strengthened
Windows test passed against the isolated database; clang-format validation passed.
The strengthened target also compiled and linked with Arch Clang 22.1.8 and
libc++ in the ORM-enabled Linux build. This is build evidence only; the live
database execution above remains Windows-only.

The disabled-hub allocation test now exercises 32 repetitions of span/metric
adapter acquisition, server tracing options, local counters, exporter-statistic
refresh, and lazy metric/log submission with an endpoint configured but all
three signals disabled. Both measured allocation counts and bytes remain zero;
an armed allocation-failure sentinel remains untouched, ruling out attempted
allocations hidden by exception isolation. No payload factory executes and no
adapter or submission is enabled. The allocation suite passed on Windows and
Arch Clang 22.1.8. This covers steady-state producer paths, not hub construction,
protocol throughput, or whole-application tail latency.

ORM spans now identify `db.system.name` as `mysql` or `postgresql` according to
the configured session dialect, instead of the non-specific `sql` value. This
matches the [database span conventions](https://opentelemetry.io/docs/specs/semconv/db/database-spans/).
The attribute remains inside sampled, failure-isolated annotation construction;
disabled dispatch is unchanged. MySQL query and PostgreSQL bound-execution
assertions, ORM tests, and allocation regressions passed on Windows and Arch.
This does not yet establish complete database semantic-convention coverage.

Failed ORM result spans now annotate `db.response.status_code` and `error.type`
with the numeric MySQL error or a five-character uppercase alphanumeric
PostgreSQL SQLSTATE, following the respective database conventions. Missing or
malformed codes are omitted; diagnostic messages and SQL remain excluded by
default. Annotation allocation failures remain contained by operation_scope and
the original result is returned unchanged. Unit coverage verifies both dialects,
malformed-code exclusion and preservation of private diagnostics in the caller's
result only. Windows and Arch ORM and allocation suites passed. Actual collector
verification of these fields remains outstanding.

The local HTTP OTLP receiver fixture now includes actual ORM-generated MySQL
and PostgreSQL failed spans from a controlled database-client substitute. The
Windows exporter test passes through batching, HTTP 503 retry, acknowledgement
and JSON parsing, verifying unique attribute keys, database identity, response
code/error.type, parentSpanId, CLIENT kind and ERROR status, with private query
and diagnostic strings absent. ORM imports are enabled only for ORM builds.
This closes wire-serialization coverage for these fields on Windows, not official
Collector interoperability or a combined real-database-to-Collector scenario.

A dedicated SQL error-annotation fault test now arms allocation failure only
after the controlled database operation constructs its result. For both dialects,
the injected telemetry allocation failure leaves the original error code,
SQLSTATE and diagnostic unchanged, executes the database once and still completes
one failed span. The Windows allocation suite and formatting check passed.

Arch execution exposed an EOF classification defect in strict HTTP response
handling: truncated Content-Length bodies and incomplete chunk trailers surfaced
as cnetmod end_of_file rather than a zero-byte read. They were incorrectly
retried by OTLP and exhausted the flush deadline. The bounded-response branches
now classify either EOF representation as invalid framing while retaining other
transport errors and the unbounded compatibility behavior. The raw receiver
fixture also drains its request before closing to avoid unread-data TCP resets.
All 17 OTLP tests now pass on Arch (about 0.10 s) and Windows (about 0.32 s),
including ORM wire spans and all 12 response-boundary cases. The SQL annotation
allocation fault test also passed on Arch. This does not prove all HTTP framing
or all-platform coverage; remaining strict chunk-body EOF branches need review.

Strict chunk size-line and chunk-data reads now also normalize zero-byte and
end_of_file completion to invalid_chunk. Four added receiver cases cover a
partial size line, short data, truncated data delimiter and missing final chunk.
The expanded 16-case response matrix and allocation suite passed on Windows and
Arch; ordinary unbounded HTTP response behavior remains unchanged. These tests
verify malformed-response rejection without OTLP retries, not transport-reset
recovery or successful delivery after an unavailable collector returns.

The collector recovery fixture holds a loopback port without listening, waits
for the exporter to register an actual retry, then starts its HTTP receiver.
It verifies delivery of the queued log and a later batch, two acknowledged
records, no drops/failed batches and successful flushes. Windows initially failed
because HTTP connect omitted client_options::connect_timeout when calling Happy
Eyeballs. Passing that existing option fixes the configured timeout without
adding another timer layer. The full 18-test OTLP suite passed five consecutive
runs on both Windows and Arch. This covers a local connection failure/recovery,
not DNS cancellation, every signal's outage behavior or official Collector use.

The recovery fixture now covers all three signal queues: one span, gauge and log
before recovery and a second set after the first flush. The receiver checks each
endpoint's record count and exact before/after payload or gauge value; exporter
statistics confirm six accepted/exported records with no drops or failed batches.
The expanded OTLP suite and formatting checks pass on Windows and Arch. This
remains a local HTTP receiver test rather than official Collector interoperability.

OTLP delivery now explicitly supplies a per-attempt deadline to the HTTP client.
A silent loopback receiver consumes the request but sends no response and keeps
the connection open. With a 100 ms request timeout and one attempt, the batch
finishes as failed (not an invalid acknowledgement), flush drains, and the peer
observes connection closure. The complete OTLP suite passes on Windows and Arch.
This does not establish bounded DNS resolution or cancellation of all in-flight
delivery when a shorter overall flush budget expires.

The delayed-acknowledgement regression distinguishes flush waiting from shutdown:
a 20 ms flush times out, acceptance is closed, and a second flush still receives
the acknowledgement delayed by 100 ms without retrying or losing the record.
The public API comments now explicitly document this distinction. Source review
confirms Application Host currently stops its event loop after flush timeout
without joining the exporter worker; explicit cancellation and worker settlement
remain necessary before this shutdown path meets the lifecycle goal.

The exporter now offers an explicit execution-thread-only abort operation,
separate from non-destructive flush. It closes acceptance, cancels the active
HTTP operation or retry timer, and drains queued records into per-signal dropped
counters. A silent-receiver regression waits 20 ms, queues another log, aborts
twice, and verifies settlement within a 500 ms flush budget despite a 2 s request
timeout: one failed batch, one dropped log, zero retries, and peer closure.
Windows and Arch OTLP tests pass. Application integration, retry-wait cancellation
regression, all-signal discard coverage, and bounded DNS remain outstanding.

Abort regressions now cover a 503 acknowledgement with Retry-After: 30 and
verify cancellation settles within 500 ms while the retry timer is active.
Both active-request and retry-wait cases enqueue one record of each signal,
verify all three discard counters, reject a post-abort submission, and ensure
the peer sees closure while the exporter object is still alive. This exposed
an idle-connection lifetime gap: abort drain now explicitly closes the client
after cancellation settles. The expanded OTLP suite passes on Windows and Arch.
Application shutdown integration and DNS cancellation are still not established.

An exporter shutdown operation now composes close, graceful flush, abort, and
cancellation settlement with two explicit budgets. Its successful result means
the worker is idle and the connection closed, not guaranteed collector acceptance.
Tests exercise both delayed successful acknowledgement and a forced silent-peer
shutdown, observing closure before exporter destruction. Windows and Arch pass.
Source inspection confirms getaddrinfo still executes synchronously on the CPU
pool and posts back to the context afterwards; cancelling that wait safely must
address resolver ownership before Application can destroy the context on timeout.

Happy Eyeballs now uses a cancellable DNS waiter for uncached hostnames. System
resolution runs in an owned shared result state on the CPU pool without an
io_context reference or completion post; the waiter observes completion through
a cancellable timer. Literal and cache-hit paths retain their fast paths, and
the existing standalone non-cancellable resolver retains its original route.
OTLP shutdown tests now use localhost for delayed acknowledgement and abort
cases and pass on Windows and Arch. This verifies hostname delivery, not a
deterministically stalled getaddrinfo. Late resolver completion after context
destruction, cancellation allocation failures, bounded outstanding lookups and
global pool destruction remain to be tested/addressed before full acceptance.

Cancellable uncached DNS lookups now have a configurable admission limit of 64
owned result states. Ownership releases capacity only when both the worker and
waiter have relinquished the state, including scheduling/allocation exceptions;
caller cancellation alone does not release a still-running lookup's slot.
DNS metrics expose pending and rejected counts. A zero-capacity test disables
the cache, rejects localhost before system lookup launch, and checks counters.
This bounds this cancellable path, not standalone async_resolve or the global
pool's process-exit wait. Concurrent saturation and delayed-worker fault tests
remain outstanding.

A Linux-only linker-wrapped getaddrinfo regression now blocks one designated
hostname deterministically. It cancels the caller, verifies another hostname is
rejected at capacity one, destroys the io_context while lookup remains blocked,
then releases the system call and observes pending capacity return to zero.
Ten consecutive Arch runs pass. This is direct late-completion and saturation
evidence for that backend, not a Windows fault-injection or sanitizer proof.
The test wrapper is confined to its executable; production resolver hooks and
system DNS settings are unchanged. Process exit with a permanently blocked
system resolver and Application shutdown integration remain open.

Telemetry Hub now forwards explicit shutdown while closing its producer gate.
Application normal shutdown invokes this operation, reserving one fifth of the
existing telemetry budget (at least 1 ms) for cancellation settlement rather
than extending the configured budget. Telemetry failure remains a warning and
does not overwrite a business result. Existing Application and OTLP regressions
pass on Windows and Arch. A cancellation-settlement timeout still reaches the
existing finish path, so this is not proof of safe shutdown under every failure;
dedicated Host stalled-collector and settlement-failure tests remain required.

A Host-level silent-collector regression now runs an independent raw TCP peer
that reads telemetry and never responds. With a 5 s request timeout and 100 ms
telemetry shutdown budget, Host returns successfully within the test's 2 s
overall ceiling, and the peer observes closure before Host destruction. Failed
delivery remains visible in statistics without changing the business result.
Windows passes and Arch passes five consecutive Application suite runs. This
does not prove exact 100 ms wall-clock completion or the settlement-timeout path.

Idle abort now discards queued records and closes the client synchronously on
the execution thread, without allocating or scheduling another drain worker.
Flush no longer starts a worker when there is no work. A fault-injection test
fails submission scheduling, arms the next allocation to fail again, then aborts
twice: no allocation is attempted and the stranded metric is counted as dropped.
Allocation, OTLP and Application suites pass on Windows and Arch. Windows also
reported QuarkCloudDrive locking build-state files; a build retry succeeded
without terminating the user's process. General active-worker settlement timeout
and exception handling remain outstanding.

Exporter shutdown now catches graceful-flush exceptions so cancellation still
runs, skips a second wait when idle cleanup has settled, and translates errors
from the cancellation wait into error codes. The idle nested-flush allocation
failure is injected and recovered on MSVC; Clang elides the tested allocation,
so its result establishes an allocation-free idle path, not that exception case.
Allocation, OTLP and Application suites pass on Windows and Arch. Failure to
allocate the outer shutdown frame, Hub/Host exception boundaries and active
settlement timeout remain outside this evidence.

Hub and Host now catch exceptions around their nested shutdown calls and request
allocation-free abort before reporting an error. Host also isolates failure of
the shutdown warning itself, preserving the business result under memory pressure.
A Hub idle-abort test verifies repeated calls allocate nothing and close producer
acceptance. Application and allocation suites pass on Windows and Arch. These
checks do not yet inject each outer-frame failure with active I/O; the active
worker settlement-timeout finish path is still not proven safe.

Flush now settles an aborted, already-idle worker's remaining queue directly
before testing the waiting deadline. This covers abort followed by worker-start
allocation failure: a zero-budget flush discards the queued metric without a
new allocation or scheduling attempt. The fault-injection regression passes on
Windows and Arch, together with Application and OTLP suites. This closes an
idle-after-failure case; it does not resolve genuinely active I/O exceeding its
cancellation-settlement deadline.

Source inspection confirms epoll/IOCP stop only ends dispatch, not an I/O join.
It also found three startup exits bypassing telemetry shutdown. Startup service
failure, business bind failure and management bind failure now await the same
telemetry finishing routine as normal stop. A bind-conflict regression queues
telemetry before run and verifies the original startup failure is returned,
Host stops within its test ceiling, and accepted work is accounted as failed or
dropped rather than stranded. Windows and Arch Application suites pass.
Active cancellation deadline exhaustion and allocation of the finishing frame
remain outside that proof.

Host now allocates its single finishing task during construction and consumes
that task from every terminal lifecycle branch, avoiding creation of this frame
under shutdown memory pressure. Host construction translates bad_alloc and
system_error into build errors. Inspection also found the managed-service health
registration condition inverted; successful preparation now populates health.
A new build-only test verifies one registered custom service produces one health
snapshot without starting the service. Windows and Arch Application suites pass.
Construction-failure injection and active-I/O settlement exhaustion remain open.

A running Host regression now changes a fake required dependency from available
to unavailable and back using an atomic control flag. It observes ready, a DOWN
snapshot with readiness withdrawn, recovered readiness, and readiness withdrawn
again after stop, with at least three actual probes. Windows passes; Arch passes
five consecutive Application runs. This covers the real Host health loop with
a fake dependency, not broker/container outage recovery. Inspection also notes
health refresh currently ignores task-group scheduling/join errors, which needs
failure-injection coverage to rule out stale UP status on scheduling failure.

Health failure-path review exposed task_group registering a completion slot
before an unguarded child spawn. Startup allocation failure could strand join.
Task-group dispatch now reports startup failures and returns the completion slot;
sibling cancellation traverses shared tokens without allocating a copied vector.
A ten-position startup allocation sweep verifies join always finishes. Allocation,
task-group and Application suites pass on Windows and Arch. Health refresh still
needs to translate scheduling/join failures into non-UP cached reports rather
than ignoring them; this change fixes its lower-level joining prerequisite.

Health probes now use the identity captured at registration instead of calling
the service's virtual key() again during telemetry annotation and cache updates.
The three-mode health test rejects all post-registration key reads and verifies
the probe still updates readiness with tracing disabled, zero-sampled, or enabled.
Windows and Arch Application tests pass. Reports are published before metric accounting so
that accounting cannot prevent an already completed probe from updating health.
This does not yet resolve task-group scheduling/join failures or prove allocation
failure safety of the entire refresh operation.

Refresh now records per-probe completion, catches dispatch failures, observes
join errors, and invalidates only incomplete reports after children settle.
Task groups expose an allocation-free settlement awaitable for emergency cleanup
after cancellation. A 24-position allocation sweep starts with cached UP and
checks that a normally returning refresh cannot leave an unexecuted probe UP.
Windows and Arch allocation, Application, and task-group suites pass.
The sweep uses an unlimited deadline and one service. Preparation allocations
that throw before dispatch, multi-service mixed completion, and deadline-watcher
allocation failures still need explicit coverage; the current proof is not a
complete refresh lifetime or bounded-cancellation proof.

Preparation of the health service snapshot and task group is now an error-code
boundary. Before any child is dispatched, preparation failure invalidates cached
reports in place without allocating, preserving failure hysteresis. The allocation
sweep now covers 40 positions and two services, requires refresh to return normally
at every position, and explicitly observes mixed completion: unprobed services are
non-UP while completed successful probes remain UP. Windows and Arch Application
and allocation suites pass. The refresh coroutine frame itself is created before
injection; finite-deadline watcher faults and actively suspended child cleanup are
not established by this immediate-probe test.

Task-group deadline setup now converts allocation errors and timer failures into
group failure, cancels children, and joins them before returning. A 20-position
join-initialization allocation sweep starts with an actually suspended five-second
child timer and verifies cancellation settlement within a two-second ceiling.
Windows and Arch targeted suites pass with injection scoped to join initialization.
Keeping injection armed across event-loop dispatch exposed an uncaught bad_alloc
on Arch; that broader failure is NOT fixed. The test retains an opt-in reproducer
via CNETMOD_TEST_EVENT_LOOP_ALLOCATION_FAILURE=1. No debugger is installed in Arch,
and the abort's precise call site remains to be identified. This is an open defect,
not evidence of complete memory-pressure or shutdown safety.

GDB is now installed in the local Arch validation environment. Its abort stack
identified epoll_cancel_fn: the noexcept cancellation callback called allocating
io_context::post. The epoll cancellable awaiter now owns its cancellation post
node and queues it through post_node_raw, preserving execution-thread dispatch
without allocation at cancellation. Both join-only and event-loop-wide fault
injection now run by default (20 positions each) and pass on Windows and Arch,
along with Application and task-group suites. The opt-in failing reproducer above
has therefore been promoted to a default regression, not removed. This adds inline
node storage to cancellable epoll frames; throughput/frame-size impact is not yet
benchmarked. Inspection found the analogous allocating kqueue cancellation path
still present, requiring a platform-specific fix and macOS validation. These tests
also do not establish cross-thread readiness/cancellation race safety.

The kqueue cancellation callback now uses an awaiter-owned post node too; its
token context points to the awaiter rather than directly to the event loop.
This branch has only source review, not macOS compilation or execution evidence.
Review also found allocating post() inside the generic noexcept post_awaitable.
That handoff now queues frame-owned storage, and a regression arms failure on
the next allocation while suspending, verifies no allocation attempt, then verifies
two queued continuations complete. Windows and Arch rebuilds and Application,
task-group, and allocation suites pass. Non-cancellable direct post() behavior is
unchanged. Cross-thread cancellation races, frame-size/latency measurements, and
actual kqueue execution remain open.

Readiness/cancellation review found an additional same-thread epoll batch hazard:
one callback could erase a registration still referenced by a later event in the
already fetched batch. Removed registrations now remain on an intrusive retired
chain until batch dispatch finishes; retirement needs no extra allocation and
prevents both stale dereference and address reuse during that batch. A two-event
regression removes and re-registers the second descriptor from the first callback,
requiring its replacement to run only on a subsequent poll. The Arch allocation
suite passes ten consecutive runs; Application and task-group suites also pass.
Windows allocation regression passes (the epoll-specific case is Linux-only).
This is a same-thread batch lifetime fix, not a cross-thread cancellation fix:
cancel_token still directly invokes platform callbacks, and concurrent registry
access, callback ownership, and duplicate resume prevention need further work.

A new opt-in test, CNETMOD_TEST_CANCEL_RACE=1, races a ready one-millisecond
timer against cancellation from a separate thread (up to 2000 iterations).
GDB reproduced SIGSEGV in epoll_context::retire_registration called from
run_one_impl. Inspection confirms cancellation currently mutates registrations_
directly from the caller thread while the owner loop also mutates it. The earlier
batch lifetime repair does not synchronize these concurrent mutations. This test
is retained but not enabled in the default suite until repaired; default green
results do not cover it.

The required repair must serialize registration removal onto the owning loop,
arbitrate readiness versus cancellation as one completion, and keep callback/node
storage alive until that arbitration finishes. It must also cover cancellation
during await_suspend publication and token reuse after completion. Merely locking
the registry or swallowing callback errors does not satisfy those requirements.

Epoll now registers a readiness callback rather than exposing its continuation
directly. A cancel_token registration arbitrates normal completion versus queued
cancellation under a short project-owned metadata latch. The cancellation notifier
only enqueues frame-owned storage; removal and continuation execution occur on the
owner loop. await_resume synchronizes with notifier publication before releasing
the frame. The former cross-thread crash test passed three 2000-iteration runs
and is now enabled by default; a final default run also passes. New registration
tests cover pre-cancel, normal completion, idempotent cancellation, allocation-free
notification, reset/reuse, and 128 completion/cancellation races. Windows and Arch
Application, task-group and allocation suites pass.

This migration currently covers epoll cancellable readiness, not kqueue, IOCP,
io_uring, or custom legacy callbacks. Legacy callback invocation remains outside
the registration latch because it may resume inline. Switching a token between
new registrations and legacy fields requires reset and still needs integration
audit. Publication races before registration, sanitizer coverage, token/frame-size
overhead, and disabled-mode throughput/p99 remain explicit verification gaps.

A standalone Release HTTP observation benchmark now exercises successful real
HTTP/1.1 traffic against a loopback-only Python peer, with warmup, eight alternating
paired rounds, body/status validation, and per-round throughput/P50/P99. The first
Arch run completed 16000 measured requests; raw/disabled median throughput was
15343.18/15459.70 requests/s and median P50 was 58.9590/58.9795 microseconds.
Raw data and reproduction instructions are in testing/bench/http-observation*.
This is initial measurement infrastructure and scoped evidence, not a zero-cost
proof: runs were short, sequential and unpinned, and both paths link the current
framework. Windows, concurrent load, pre-instrumentation baseline, all protocols,
and statistical equivalence remain unverified. The temporary peer was stopped.

The successful-HTTP comparison now also builds and runs on Windows/MSVC Release
with IOCP and the system allocator. All 16000 responses matched. Eight-round raw
and disabled median throughput was 16997.76/16988.23 requests/s, P50 53.55/53.30
microseconds, and per-round P99 medians 154.50/141.85 microseconds. The roughly
0.06% throughput difference is observational, not an equivalence confidence bound.
The peer was stopped; an unavailable-peer run returned exit code 1 as required.
Benchmark task exceptions now stop the loop instead of potentially stranding it.
Raw Windows results and limitations are saved alongside the Arch measurements.
Longer/concurrent trials and a pre-instrumentation baseline remain outstanding.

Cancellation registration now keeps pending true after notification is queued,
until the owner acknowledges completion with finish_callback. Normal completion
releases callback mode immediately, so a subsequent legacy platform operation is
not silently masked. Tests assert pending remains set during cancelled ownership,
clears after acknowledgement, and a normally completed registration permits the
next platform cancellation without an intervening reset. Windows and Arch
Application, task-group and allocation suites pass, including the default epoll
cross-thread race. Calling reset before an operation actually finishes remains
outside the documented precondition; legacy platform races are not fixed by this
registration-state correction.

The default epoll timer race now exercises two schedules: cancellation competing
with already-ready I/O and cancellation released immediately before coroutine
startup. Each schedule reuses one token across 2000 completed operations, resetting
only after joining the cancelling thread and draining completion. Every iteration
requires exactly one resume, a completed task, and pending=false. Five consecutive
Arch allocation-suite runs passed (20000 race iterations total); Application and
task-group regressions also passed. This adds publication/reuse stress evidence,
not an exhaustive interleaving proof or sanitizer result. Other readiness backends
and actual network socket cancellation still require equivalent coverage.

Default epoll regression now also races actual AF_UNIX stream socket reads against
cross-thread cancellation. Each run creates 1000 socket pairs, requires exactly
one initial completion, checks cancellation error identity, and retries cancelled
reads with the reset token to verify the already-sent byte was not consumed or
corrupted. Half the cases join cancellation before polling, guaranteeing at least
500 cancelled-read/retry cases; the rest permit readiness/cancellation competition.
Three Arch suite runs pass (3000 socket pairs, plus existing timer races). This is
kernel socket evidence but not TCP, TLS, half-close, sustained backpressure, or
cross-platform socket cancellation coverage, and is not a sanitizer proof.

AddressSanitizer validation on Arch Clang 22.1.8 (RelWithDebInfo, system
allocator, epoll, SSL disabled) completed the five targeted builds. The allocation
and cancellation suite, task-group suite, and DNS cancellation suite pass with
leak detection enabled. Application fails LeakSanitizer with 2,733,274 bytes in
96 allocations; OTLP exporter fails with 97,992 bytes in 73 allocations. These
are failed gates, not suppressed findings or production-readiness evidence.
Symbolized OTLP stacks include HTTP server handle_connection (http_server.cpp:821)
and server run (line 623); several collector fixtures stop the event loop directly
after collector.stop(), without awaiting connection/accept completion. Application
allocation stacks include the enabled telemetry instance in the exact-rollback
test, whose coroutine stops the loop without shutting down telemetry. These are
concrete lifetime investigations; correcting fixtures alone will not prove bounded
production Host shutdown or exporter settlement. Re-run both failing suites after
establishing explicit completion ownership, without disabling leak detection.

Follow-up: HTTP stop now explicitly cancels its pending accept, retaining the
listener until the cancellation completes. The owner-loop contract requires
awaiting run() before loop/server destruction and does not claim connection
drain. A new owned-task stop regression passes ASAN. The lifecycle rollback
fixture now awaits telemetry.shutdown and asserts settlement before stopping I/O;
its filtered ASAN run passes. Rebuilt full Application ASAN now reports 19,840
bytes in 11 allocations (previously 2,733,274/96), while full OTLP still fails
with 98,472 bytes in 73 allocations. Neither suite is green. HTTP connection
completion and collector fixture ownership remain open, as do Windows/macOS
verification and accept-path performance measurement. Changed C++ files pass
clang-format validation; HTTP skill and generated AGENTS are synchronized.

The remaining Application leak is reproduced by the filtered management-scrape
test alone (19,840 bytes/11 allocations). That fixture uses the business HTTP
server as its collector, so exporter client closure must be followed by server
connection completion before Host stops I/O. This production ordering remains
unfixed. In the permanent-collector-failure OTLP fixture, explicit exporter
shutdown, owned accept-task completion, and bounded waiting for zero active
connections preserve retry/error assertions and pass ASAN without suppression.
The full OTLP suite now reports 77,992 bytes/59 allocations, down from
98,472/73. Other collector fixtures and Host connection ownership still require
the same lifetime guarantees; the bounded polling in this test is not a proposed
production shutdown abstraction and does not prove forced-drain safety.

Host now keeps its event loop alive after telemetry shutdown while either HTTP
server still reports active connections, bounded by http_drain_timeout. This
allows the self-collector EOF completion to run before Host destruction. Idle
shutdown skips the timer loop; timeout/timer/allocation errors preserve an earlier
run error and do not escape the cleanup coroutine. Three rebuilt full Application
ASAN runs pass with leak detection enabled. This is an incremental ordering fix:
it does not yet force-cancel connections that outlive the budget, replace detached
HTTP ownership with a joinable completion API, or enforce one absolute deadline
across every shutdown phase. The extra final drain budget and polling must be
folded into that final lifecycle design; passing these fixtures is not evidence
that arbitrary long-lived HTTP handlers can be destroyed safely.

All four HTTP Collector fixtures now share bounded settlement of exporter,
owned accept task, and active connections. Existing wire, retry, partial-success,
and recovery assertions are retained. Full OTLP ASAN leakage is reduced to 6,072
bytes in 18 allocations, but the suite still fails. Symbolized stacks point to
async_connect_happy_eyeballs/connect_attempt during the response-budget scenarios.
Source inspection finds that fallback delays use uncancellable timers before
their tokens are registered, and the race returns a winner without joining
detached losing attempts. The next required fix is ownership and cancellation of
every attempt, including delayed attempts, before returning the race result;
adding a test-only delay would conceal this production lifetime defect.

Happy Eyeballs now pre-registers immutable per-attempt cancellation tokens,
including fallback-delay timers, supervises attempt exceptions/startup failures,
and awaits child completion before returning the winning/error result. Its parent
cancellation relay uses the ownership-aware callback registration instead of raw
legacy callback fields; waiter delivery uses state-owned post storage and does
not allocate. Winner/error cancellation no longer copies a token vector. Rebuilt
OTLP, Application, and allocation/cancellation suites pass Arch ASAN with leak
detection enabled. A deterministic resolver fixture returns two loopback addresses
and delays the fallback by ten seconds: the first address wins, only one connect
is attempted, pending clears, and the context can be destroyed in under two
seconds. Three full DNS cancellation runs pass (0.07-0.12 seconds each), including
the existing blocked-system-resolver test. Formatting passes. This does not yet
prove all-backend cancellation races, allocation-failure coverage of every new
race setup step, or unchanged cold-connect throughput/tail latency; those checks
remain required alongside the still-open HTTP forced-drain design.

Windows/MSVC Release core and the Application, task-group, allocation/disabled
HTTP, and OTLP test executables rebuild and all four CTest suites pass after the
HTTP accept and Happy Eyeballs ownership changes (3.23 seconds total). This is
IOCP runtime coverage, not a Windows sanitizer proof. A second deterministic
Linux resolver test holds an unused local port, waits for the first refusal,
then cancels while the second address is delayed ten seconds. It requires the
original operation_aborted error, cleared pending state, and completion under
two seconds. Three full DNS ASAN runs pass with this additional case. macOS,
io_uring, cross-thread race stress, setup allocation injection and connect-path
performance measurements remain unverified for these changes.

Allocation injection now probes 64 positions across a literal-address Happy
Eyeballs setup and event-loop completion, requiring settled tasks, cleared parent
pending state, successful recovery positions, and not_enough_memory on failures.
It exposed two Linux cases where with_deadline misclassified timer registration
failure as expiry. The watchdog now returns its actual error and cancels the
operation on infrastructure failure; normal timer expiry still marks deadline
exceeded. The operation wrapper cancels its watchdog even when the operation
throws. A ten-second-watchdog regression preserves permission_denied from a
throwing operation and returns in under two seconds. Rebuilt Windows four-suite
regressions and Arch ASAN allocation, OTLP, Application, task-group and DNS suites
all pass. Multi-address allocation injection and simultaneous external-cancel /
watchdog-failure precedence are not covered by this new test.

HTTP now exposes terminal, idempotent abort_connections(): an intrusive,
non-allocating registration list applies socket shutdown without destroying
pending coroutine frames. Registrations unlink before socket parameter
destruction. A movable admission guard increments the existing connection count
before dispatch, so queued work is included and dispatch allocation unwinding
releases its count. No request-level observation hooks were added. A real idle
TCP peer regression stops admission, aborts twice, and requires both accept
completion and zero active connections before loop destruction. Three full OTLP
ASAN runs and the Application ASAN suite pass. HTTP skill/AGENTS and formatting
checks are synchronized. This primitive is not yet wired into Host timeout
handling; IOCP/TLS/multicore abort tests, admission-rejection write cancellation,
handler-level cooperative cancellation, and connection-rate overhead measurement
remain required. Socket shutdown alone cannot cancel arbitrary non-socket work.

Host final HTTP drain now invokes abort_connections on both listeners when the
graceful connection budget expires, then allows service_stop_timeout for socket
completion. The original timed_out run result is preserved. A real peer first
completes GET /ready on a keep-alive connection, then submits an incomplete POST
body and requests shutdown. The test requires EOF before Host destruction and
verifies the incomplete-body handler never runs (the current parser buffers the
body before dispatch). Explicit end_of_file and zero-byte success are accepted
as the two platform EOF forms, not arbitrary read failures. Three full Application
ASAN runs pass; Windows Application and OTLP suites rebuild and pass, including
the idle abort primitive. This still uses separate phase budgets rather than one
absolute shutdown deadline. Exhausted forced-drain budgets, non-socket handler
work, TLS/multicore abort and cold-connection performance remain open.

The lifecycle shutdown API now accepts an absolute caller deadline and constrains
its configured limit to that deadline. Normal host shutdown passes the deadline
created before HTTP draining, so service shutdown does not restart that budget.
Only successfully stopped services are removed from lifecycle ownership; expired
budgets retain unclosed services for a subsequent cleanup attempt. The new expired
parent deadline regression verifies timeout, retained ownership, and successful
subsequent cleanup. The Arch Clang 22 ASAN Application suite passed three complete
runs after this change (1.30, 1.39, and 1.31 seconds). Windows has not yet been
rebuilt for this change. Supervisor joining, telemetry flushing, and final HTTP
connection settlement still require a unified deadline and safe ownership
handling; this change does not establish a hard end-to-end shutdown bound.

Shutdown failure reporting now retains a thrown `system_error` code (and maps
allocation failure separately), records the structured shutdown failure, and
keeps the health report in `stopping` with that error until cleanup succeeds.
Explicit `stopping` and `stopped` reports bypass probe hysteresis, which previously
could relabel them as `degraded`. The regression verifies failure, retained
ownership, and a successful retry clearing the health error. Exception outcomes
also reach lifecycle telemetry rather than bypassing its result recording.
Windows Release core and Application test rebuilds passed; its full Application
suite passed in 2.03 seconds. Arch ASAN rebuilt and passed three full Application
runs (1.30, 1.31, 1.62 seconds). These results include the parent-deadline test
from the preceding change, but do not prove end-to-end shutdown boundedness.

Service shutdown now wraps each stop operation in `with_deadline` with an
independent token. Expiry requests cancellation and joins both the operation and
watchdog before returning; the next service does not inherit cancellation from
its predecessor. A regression uses a ten-second cancellable operation with a
20 ms service limit, verifies cancellation registration settlement and retained
ownership, checks that an independent service still stops, and retries cleanup.
This remains cooperative: an implementation ignoring cancellation can still
delay shutdown indefinitely and must not be forcibly destroyed.
Full-suite repetition exposed a fake-service recovery flaw: startup succeeded
while its simulated dependency was unavailable, making `down` transient. The
fixture now rejects startup until that dependency is restored. After this fix,
Windows Release Application passed three runs (1.50/1.55/1.52 seconds), and Arch
ASAN Application passed three runs (1.38/1.35/1.34 seconds). This does not replace
real dependency outage testing or the remaining end-to-end budget work.

The disabled producer allocation regression now also constructs, annotates,
completes, and destroys an `operation_scope` using the disabled hub's span sink
inside its measured loop. It asserts zero allocation attempts, zero allocated
bytes, no metadata factory calls, no active context, and no accepted telemetry.
The complete allocation/fault-injection suite was rebuilt against the current
lifecycle changes and passed on Windows Release (0.48 seconds) and Arch ASAN
(3.11 seconds). Those suite durations are not throughput benchmarks. Existing
allocation comparisons cover selected HTTP, Redis, OpenAI, and ORM paths, not
all protocols or a pre-instrumentation framework baseline. The unpinned loopback
benchmark remains insufficient to establish the requested full performance gate.

Reverse shutdown now retains dependencies of services whose stop failed or was
skipped, while allowing independent services to close. Dependency metadata is
prepared before issuing stops, and successful stops are removed from the pending
set. A three-level dependency regression verifies that a failed worker keeps its
repository and database alive, an independent service closes, and retry closes
only the remaining chain in reverse order. Arch ASAN Application passed three
runs (1.35/1.32/1.35 seconds); Windows Release rebuilt and passed three runs
(1.51 seconds each). Application skill documentation now states the cooperative
cancellation and remaining global-budget limits explicitly; generated AGENTS.md
and formatting checks passed. Host final destruction after unsuccessful cleanup
still needs safe ownership handling; retaining lifecycle records alone does not
prove resource safety at that boundary.

Rollback now preserves the initiating lifecycle failure and exposes a separate
`last_rollback_failure()` for a component stop failure, labeled as `rollback`.
A regression starts a database, fails its dependent worker startup with
connection-refused, fails database rollback with permission-denied, verifies
both identities and original codes, then successfully cleans up the database.
Windows Release and Arch ASAN core/test rebuilds passed, and their full
Application suites passed in 1.51 and 1.31 seconds respectively. Allocation
failure during rollback bookkeeping and host-level exposure/cleanup remain
separate unverified boundaries.

Recovery-exhaustion notification now snapshots a shared immutable handler rather
than copying its callable on the failure path. Registration prepares storage
before acquiring the latch, and replacement releases the old callable after the
latch is released. The host requests stop before formatting the failure log.
A callable configured to throw on any post-registration copy still receives
exactly one exhaustion notification; joining retains the original task error.
Current Windows Release and Arch ASAN Application rebuilds and full suites pass
(1.51 and 1.33 seconds). This verifies callback-copy isolation, not arbitrary
callback progress or a hard shutdown bound.

Recovery startup success no longer contributes a synthetic successful health
probe. It publishes an awaiting-confirmation state; a regression verifies zero
successes immediately after reconnect, readiness still false after one probe,
and readiness true only after the configured second successful probe. Arch ASAN
Application passed in 1.34 seconds. Windows Release rebuilt after a transient
LNK1104 retry and its full Application suite passed in 1.53 seconds. Formatting
and generated skill documentation checks pass. Recovery-budget continuity across
reconnect-success/probe-failure cycles remains unverified and is not established
by this readiness regression.

Each supervised recovery attempt now probes after successful startup. A non-up
probe keeps that attempt failed, preserving the supervisor's recovery budget
instead of completing a reconnect-only task. Successfully started resources are
registered before probing so failed confirmation does not lose cleanup ownership.
A regression verifies continuously successful reconnects with failing probes
reach one required exhaustion notification and retain resources until cleanup.
Windows Release and Arch ASAN full Application suites passed after rebuilding
(1.54/1.36 seconds); the consecutive-health-confirmation regression also passes.
The direct recovery probe still requires enforced cancellation/deadline coverage,
and budget continuity when an initial probe succeeds but later confirmation
fails is not yet proven by this change.

A pending recovery-probe cancellation regression now waits for a ten-second
cancellable probe to enter, requests supervisor stop, and verifies join returns
within two seconds only after cancellation registration is settled. It also
verifies the successfully started service remains owned until lifecycle cleanup.
Windows Release and Arch ASAN rebuilt and passed their full Application suites
(1.56/1.33 seconds). This verifies explicit cooperative stop propagation, not
automatic recovery deadline enforcement. A recovery timeout must use a distinct
attempt token: cancelling the supervisor's shared stop token would otherwise
end retries instead of consuming the recovery budget.

Recovery startup and its initial probe now share an enforced per-attempt deadline
and an independent cancellation token. A scoped parent registration forwards
supervisor stop without allowing attempt timeout to poison subsequent retries.
The deadline wrapper settles the operation and watchdog before the registration
is released. The timeout variant verifies repeated timed-out probes, exhausted
required recovery, settled cancellation, and cleanup ownership. A 15 ms test
recovery budget was too short for reliable Windows retry-count evidence and was
raised to 150 ms without changing assertions. Windows Release Application passed
three runs (1.86/1.87/1.87 seconds), and Arch ASAN passed three runs
(1.64/1.67/1.64 seconds), including explicit stop during a pending probe.
Cross-thread link races and allocation-failure coverage remain to be added;
cooperative timeout is not a bound on an unresponsive service coroutine.

The recovery cancellation regression now runs 32 iterations: one owner-loop
stop and 31 foreign-thread stops synchronized with probe entry, alternating
immediate cancellation and a short delay. Each verifies probe cancellation and
registration settlement, bounded join, retained service ownership, and cleanup.
Windows Release full Application suites passed three runs (2.35/2.34/2.35 seconds)
and Arch ASAN passed three runs (1.67/1.66/1.68 seconds); formatting passed.
This exercises but does not exhaustively prove cross-thread interleavings and
does not substitute for ThreadSanitizer or other platform backend tests.
Inspection also found that `task_supervisor::request_stop()` is noexcept yet
invokes arbitrary stop-request callbacks without an exception boundary; a
throwing callback can terminate the process and prevent later cancellations.
That separate shutdown failure path still needs implementation and regression.

Supervisor stop-request callbacks now have an exception boundary. System errors
retain their code, allocation failures map to not-enough-memory, and other
exceptions map to I/O error; cancellation continues for the remaining entries.
Stop errors survive subsequent coroutine completion rather than becoming
`stopped`, while an already-terminal task failure retains precedence. A regression
has two throwing callbacks, repeats the stop request, and verifies both tasks
settle, each callback runs once, and required join/state/error queries expose the
failure. Windows Release and Arch ASAN full Application suites passed after
rebuilding (2.35/1.69 seconds); formatting passed. Concurrent join versus an
in-progress foreign-thread stop callback still needs completion-accounting tests.

Stop callback dispatch now participates in supervisor completion accounting;
registration and join synchronize through the membership latch. A controlled
foreign-thread callback regression verifies join stays pending until callback
release, sees its error, and resumes the awaiting coroutine on the I/O thread.
An initial unconditional I/O post broke empty-group synchronous join and exposed
a queued-frame use-after-free in existing tests. The final implementation posts
only when wait completion switched threads, preserving the synchronous path.
After the correction, Windows Release and Arch ASAN full Application suites
passed (2.38/1.69 seconds). Non-returning stop callbacks and shutdown-budget
enforcement remain open; join does not forcefully interrupt user callbacks.

The allocation/fault-injection suite was relinked against the current supervisor
completion-accounting changes. Its existing 32-callback stop/reentrant-query
check still observes zero allocations. A new bad-allocation callback regression
verifies both callbacks execute, stop handling makes no operator-new attempts or
allocated bytes, and join returns not-enough-memory after tasks settle. Windows
Release and Arch ASAN full allocation suites pass (0.48/3.04 seconds), and format
checks pass. The allocation counters cover overridden new/delete, not every C++
exception-runtime allocation mechanism or end-to-end throughput.

Startup now catches service invocation exceptions at the component boundary,
preserving system_error codes and mapping bad_alloc separately before health,
telemetry, and required/optional handling. The rollback provenance regression
now covers both returned errors and thrown startup errors, retaining the worker
startup cause separately from the database rollback cause. Windows Release and
Arch ASAN rebuilt and passed full Application suites (2.39/1.72 seconds), with
formatting checks passing. This does not establish failure safety for allocations
in post-start registration bookkeeping or every optional exception path.

Recovery invocation exceptions now enter the same result path as returned
errors, including component failure metadata and terminal telemetry. The recovery
budget regression additionally injects a throwing probe and checks the original
code, component identity, health-recovery phase, exhaustion, and retained cleanup
ownership. Non-timeout variants use a normal deadline so incidental scheduling
cannot replace their intended error. Arch ASAN full Application passed after
rebuild (1.86 seconds); Windows core/test rebuild succeeded after one transient
LNK1104 retry. Recovery exception telemetry receiver assertions and comprehensive
allocation-failure bookkeeping tests remain outstanding.

Lifecycle ownership now uses pre-created per-service active flags rather than
allocating set nodes after successful start. Startup prepares the dependency
layers and flags before dispatch, and recovery prepares its flag before
registration. Successful start/stop changes only existing flags. The redundant
post-start completed-key vector and layer append were removed. A fault injection
service arms failure immediately before returning startup success and verifies
the resource is either already rolled back or still owned for explicit cleanup.
Arch ASAN full Application and allocation suites passed (2.00/3.04 seconds).
Windows Release core and both test targets rebuilt successfully. Preparation
failure handling and every allocation position in rollback/health reporting
still need broader verification; this change does not establish the full
resource-leak gate.

Startup graph and ownership preparation now catches allocation/system failures
and returns error codes before dispatch. New layers and active flags are built
in temporaries and swapped only after successful preparation. The allocation
regression first injects preparation failure, verifies not-enough-memory and
zero service starts, then reuses the same lifecycle for the post-start ownership
test and cleanup. Windows Release Application/allocation suites pass
(2.54/0.47 seconds), as do Arch ASAN suites (1.86/3.05 seconds), after rebuilds;
formatting passes. This covers the first preparation allocation failure, not
every allocation position or failures during later layer dispatch.

Startup layer construction, dispatch, and join exceptions now settle already
dispatched children before rollback. The post-start allocation regression no
longer swallows exceptions: successful startup must retain registered ownership;
failed startup must return `not_enough_memory` and complete rollback before
returning. An armed allocation failure need not fire if the remaining startup
path allocates nothing. This is not a sweep of every dispatch allocation site.
Current Windows Release verification passed Application (2.54 s) and the revised
allocation suite (0.48 s); Arch Clang ASAN passed Application (1.90 s) and the
revised allocation suite (3.04 s). These checks do not establish universal
zero-overhead behavior or the remaining end-to-end shutdown guarantees.

Service recovery now shares an absolute deadline from scheduling through the
first attempt, later attempts, and retry delays. Each start/probe attempt uses
the earlier of this limit and the per-service startup timeout. General supervised
background tasks retain first-failure recovery timing unless explicitly given
a deadline. The supervisor bounds retry scheduling; lifecycle operations enforce
the same deadline through cooperative I/O cancellation and settlement.
A regression uses a 150 ms recovery budget, 30 s per-attempt timeout, and a
10 s pending probe: it verifies one probe, cancellation and settlement, terminal
timeout propagation, and completion within a 2 s scheduling tolerance. Current
Windows Release Application/allocation suites passed (2.67/0.50 s); Arch ASAN
passed (2.00/3.01 s). This does not close the separate budget-reset gap across
reconnection followed by failed health confirmation, nor bound uncooperative code.

Recovery deadlines now persist by service across completed reconnect tasks until
confirmed cached health is up and the recovery task is terminal. Host health
refresh reconciles every snapshot, including expired starting/degraded states.
Repeated refreshes do not redispatch an exhausted required episode or repeat its
notification. Optional exhausted tasks may start another recovery cycle on a
later refresh so background recovery continues. Confirmation expiry detection is
refresh-driven, not an independent hard-deadline watchdog.
The new required-service regression checks successful reconnection followed by
unconfirmed expiry, no extra probe after expiry, one exhaustion notification,
and renewed budget only after two healthy probes and a subsequent outage.
Arch ASAN Application/allocation suites passed (2.07/3.05 s). Dedicated optional
exhaustion-cycle and concurrent stale-snapshot tests remain to be added; this
change is not evidence of complete lifecycle or cross-protocol closure.

The optional-service multi-cycle regression now runs with all three telemetry
signals disabled and enabled. Two exhausted cycles must each perform fresh
attempts while keeping liveness, withholding readiness, owning no successfully
started resource, and never notifying required-service shutdown. After dependency
restoration, the next cycle starts successfully, two cached health confirmations
restore readiness, and shutdown closes the service exactly once. Both variants
passed in the Application suite on Windows Release (2.88 s) and Arch ASAN
(2.19 s). This is controlled-service lifecycle parity, not real-broker recovery,
export delivery verification, or a throughput comparison. Concurrent stale-health
snapshot reconciliation and unified shutdown deadlines remain open.

Cached health snapshots now carry a revision advanced by report updates and
probe-preparation failures. Lifecycle reconciliation checks this revision under
its episode latch before changing episode ownership. A deterministic delayed-up
regression retains a confirmed snapshot from one episode, starts a new episode,
then delivers the old up after expiry: it must not reset the budget, run another
probe, or suppress the new terminal timeout. Windows Release Application and
allocation suites passed (2.98/0.49 s); Arch ASAN passed (2.46/3.07 s).
The registry revision check is instantaneous, not a cross-component transaction;
this proves rejection of already-stale snapshots, not every concurrent update
interleaving or stale asynchronous probe completion. Those cases remain open.

Asynchronous health refresh now captures each source revision and conditionally
commits the report under the registry latch. An intervening update discards the
old result without advancing hysteresis or revision; undispatched-probe failure
reports use the same condition. Stopping registries suppress probe commits and
new refreshes, while stopping/stopped components are skipped by preparation and
preparation-failure invalidation. A controlled delayed probe verifies that newer
down, stopping, and stopped reports survive, and that later refreshes do not
probe closing/closed components. Windows Release Application/allocation passed
(3.11/0.49 s); Arch ASAN passed (2.37/3.06 s). This does not establish serialization
of overlapping external service probes or complete cross-component reconciliation.

The current worktree was reconfigured and rebuilt with every protocol, SSL, and
ORM disabled on Windows MSVC Release (`cmake-build-protocol-free-verify`) and a
fresh Arch Clang 22.1.8/libc++ Release epoll build
(`/root/cnetmod-protocol-free-validation`). Instrumentation, metric aggregation,
deadline, task-group, and task test executables all passed on both platforms.
Generated Windows compile items and Linux compile_commands each contain zero
Application units, zero Observability units, and eleven protocol-neutral
Instrumentation units. This confirms core feature isolation for these targets,
not runtime zero overhead or the all-on/macOS matrix. Existing compiler warnings
remain (including unused Clang scan flags/helper and MSVC discarded nodiscard
results); neither build is asserted warning-free. Linux auxiliary LevelDB/LZ4
options retain their defaults, unlike the Windows cache, so this is a protocol
selection comparison rather than identical optional-dependency configurations.

Health probe exception translation now preserves `system_error::code()`, maps
`bad_alloc` to `not_enough_memory`, and uses `io_error` only for unknown
exceptions. Catch paths clear the message without copying exception text or
constructing a replacement diagnostic string. A regression executes all three
categories with telemetry signals disabled and enabled, checks cached error
codes and one hysteresis failure, and rejects a sentinel exception detail in
health JSON. Windows Release Application/allocation passed (3.15/0.49 s), and
Arch ASAN passed (2.36/3.09 s). This verifies the health boundary, not every
integration's exception translation or end-to-end exported error attributes.

Normal host shutdown now carries its absolute deadline into finish and clamps
telemetry's delivery/cancellation split to the remaining budget instead of
granting a fresh configured flush window. Delivery is clamped at zero; a minimum
1 ms cancellation opportunity remains when the budget is already exhausted.
The silent real TCP collector regression now configures a 100 ms total shutdown
budget against a 5 s telemetry flush window and verifies cancellation, peer EOF
before host destruction, and completion within its 2 s scheduling tolerance.
Arch ASAN Application passed (2.35 s). This does not prove a 100 ms process bound:
supervisor settlement, final HTTP connection cleanup, cancellation completion,
and startup rollback still need a unified ownership/deadline solution.

Business/management HTTP bind-failure cleanup now establishes one host shutdown
deadline before stopping started services and passes it through telemetry finish.
The real occupied-port regression adds a 30 ms service stop, a 100 ms total
cleanup budget, and a 5 s configured flush window. It verifies one settled
service close, pending telemetry failure/drop accounting, and completion within
2 s. The initiating error is compared with a direct HTTP listen attempt using
the same occupied endpoint: Windows returns permission_denied here while Linux
returns address_in_use, and both originals survive cleanup. Application suites
passed on Windows Release (3.12 s) and Arch ASAN (2.36 s). Service-start failure
rollback and final HTTP cleanup remain separate budget/ownership gaps; the
management-port branch was compiled but not separately fault-injected this turn.

Internal service-start rollback now records its absolute cleanup deadline before
logging/stopping and passes that same deadline to stop(). Host startup failure
inherits it for telemetry finish; a startup with no rollback gets a fresh cleanup
limit. Each start resets the previously recorded rollback deadline. The occupied
collector regression now covers both bind failure and dependent-service startup
exception with a 30 ms dependency stop, 100 ms cleanup budget, and 5 s flush
configuration. Original errors survive, successful services close once, and the
failed-start service is never stopped. Windows Release Application/allocation
passed (3.25/0.47 s); Arch ASAN passed (2.47/3.16 s). Remaining shutdown gaps include
unbounded cooperative settlement, final HTTP cleanup, and allocation/exception
failures inside rollback itself; these results are not a process-level hard bound.

Rollback now moves failure ownership instead of allocating string copies, contains
diagnostic logging exceptions, catches stop preparation/execution exceptions,
and restores the initiating startup failure. `last_rollback_error()` records
errors even when no component-specific shutdown report exists. A fake dependency
enumerator throws bad_alloc only after successful startup; the dependent startup
permission error must survive, the parent stays registered, and a repaired stop
cleans it exactly once. Windows Release Application/allocation passed (3.27/0.49 s);
Arch ASAN passed (2.47/3.03 s). This proves lifecycle-level retryable ownership,
not automatic host retry of failed cleanup, a full allocation-site sweep, or
safe destruction of arbitrary externally active resources after host failure.

Host finish now retries retained service ownership within its existing deadline,
with 1-to-20 ms backoff, before telemetry flush. Allocation-free active-service
counting avoids copying ownership snapshots on each iteration. Completion reports
cleanup_failed when services or HTTP connections remain instead of always
reporting stopped. A transient rollback stop failure is retried successfully;
a persistent fake stop failure consumes an explicit 80 ms test budget, retains
the original required-worker error, and reports cleanup_failed with bounded
attempts. Windows Release Application/allocation passed (3.36/0.50 s); Arch ASAN
passed (2.60/3.13 s). An initial suite run hit its 30 s test timeout because the
persistent-failure fixture still used the default 30 s cleanup budget; the fixture
now explicitly tests the shorter budget and new failure state. This does not make
destruction of residual real resources safe, supervise every root coroutine, or
bound uncooperative service operations; those remain substantive completion gates.

The application orchestration coroutine now contains exceptions from lifecycle
execution, preserves an existing primary error (or translates the exception),
revokes readiness, stops listeners, requests supervised cancellation, and enters
the preallocated finish task once. The generic spawn contract is unchanged.
The required-worker test now also injects a dependency-enumeration bad_alloc
during normal cleanup, proving this no longer escapes into detached termination:
the host returns the original worker error and cleanup_failed within its short
test budget. Windows Release Application/allocation passed (3.56/0.48 s); Arch
ASAN passed (2.78/3.03 s). Root dispatch allocation/post failures and independently
spawned HTTP accept-loop exceptions remain outside this boundary; emergency
cleanup after finish itself fails is not proof of residual-resource safety.

Host now precreates and owns its root orchestration task alongside finish. Root
dispatch uses post_awaitable's frame-owned queue node instead of detached spawn;
run checks root completion after the event loop returns. Exception fallback also
checks that finish is not already done before awaiting it again (task's rvalue
co_await alone does not transfer the stored handle). A one-shot first-allocation
failure armed after build now returns not_enough_memory with stopped state on
both Windows Release and Arch ASAN. Application/allocation suites passed on
Windows (3.55/0.49 s) and Arch (2.72/3.04 s). This covers the selected first runtime
failure, not all allocations, event-loop backend exceptions, queued-task discard,
or ownership of the independently spawned HTTP accept/connection tasks.

The host now registers business and management accept loops with the task
supervisor instead of independently dispatching them. Listener stop remains on
the accept I/O thread; a listener canceled before dispatch does not start. The
final cleanup path joins supervised tasks even when no managed services remain.
Health-task registration errors now enter the same orchestration failure boundary
instead of being ignored. A new regression reserves each of the management,
business, and health task names in turn, verifying the original `file_exists`
error, canceled worker settlement, exactly one service stop, withdrawn readiness,
and a stopped host. The updated Application suite passes on Windows Release
(3.54s) and Arch epoll ASAN (2.77s). The Windows disabled-allocation suite was
relinked and passes (0.49s). Initial compilation of the new regression exposed a
test-macro limitation for scoped enums; the assertion was corrected before these
successful runs. This does not prove accepted-handler ownership, arbitrary
handler cancellation, persistent accept-error handling, or a hard shutdown bound.
The Application skill and generated AGENTS.md document this distinction.

Admission rejection writes now use the accept-loop cancellation token, allowing
server stop to interrupt that write without changing the normal admitted-request
path. The HTTP observation test now owns its listener and client tasks, waits for
listener completion and zero active connections, and observes promise exceptions
before stopping I/O. Before extending admission coverage, all three Windows
Application/HTTP-observation/disabled-allocation suites passed. A new real HTTP
client assertion for connection-limit rejection now fails on Windows with
`cnetmod:3` (`connection_aborted`), while the same response assertion passes on
Arch epoll ASAN. This is an open regression, not waived: immediate close with
unread request data is a suspected cause, not yet a proven one. A bounded,
cancellation-aware rejection close and a deterministically pending-write test
remain required. The working tree intentionally retains this failing assertion;
the current Windows HTTP observation suite must not be reported green.

The Windows admission-response failure above is now fixed by a half-close and
bounded drain after sending 429. The write and drain share a one-second deadline
and the accept cancellation token; stop cancels either operation, and token reuse
occurs only after the deadline wrapper joins I/O and its timer. The real Windows
client now receives 429 with the close/retry headers. A new test keeps a rejected
peer's send half open: expiration still allows the next client to receive 429,
and stop settles the listener in under 500ms without waiting for the one-second
budget. These tests pass on Windows (HTTP observation suite 1.19s). The rejection
path still occupies the accept loop during drain; overload concurrency, TLS
rejection semantics and a deterministically backpressured write require further
work. Normal admitted traffic does not invoke the new deadline/drain helper.

Accepted HTTP connections now use the existing guarded dispatch rather than
the terminating detached dispatch in both single-worker and server-context paths.
The failure observer preserves system-error category/code, maps allocation
failure to not_enough_memory and other exceptions to io_error, and excludes
exception text from diagnostics. Observer failures are contained by the existing
guarded wrapper. A real HTTP test now throws from an unprotected route, verifies
the failed request, then successfully uses the server again and waits for zero
active connections. Windows Application, HTTP observation and disabled-allocation
suites pass (3.52s, 1.19s, 0.49s). This is exception containment, not full structured
connection ownership: dispatch-wrapper allocation may still propagate to the
listener, and backend discard, worker teardown, arbitrary handler cancellation,
and old-versus-new dispatch frame/throughput costs remain verification gates.

A new direct dispatch allocation comparison found a gap not covered by the
existing OTEL on/off tests: on Windows, guarded dispatch allocated 200 bytes
versus 152 for plain dispatch, with equal allocation counts (excluding the common
inner task frame). Moving the nested diagnostic exception boundary to a
synchronous helper reduced guarded dispatch to 168 bytes. Using a stateless
callback in HTTP did not remove the remaining 16-byte difference on MSVC. The
strict equality regression is retained and currently fails; the allocation suite
must not be reported entirely green. Windows Application and HTTP observation
still pass (3.55s, 1.20s). This is partial optimization, not proof of performance
parity or of end-to-end throughput. Remaining dispatch storage must be addressed
without dropping error isolation or weakening the equality assertion.

The remaining Windows fixed-observer dispatch allocation difference is eliminated
with `spawn_guarded<OnError>(ctx, task)`. HTTP uses this compile-time binding;
stateful callbacks retain the original three-argument overload and its storage.
The strict test still compares allocation calls and bytes for equality, excluding
the common inner task frame, and now passes on Windows. A separate regression
verifies the static observer receives the original system_error once and that an
exception thrown by the observer does not escape. Windows Application, HTTP
observation, and the full disabled-allocation suite pass after relinking. This
does not establish throughput parity or zero cost for arbitrary stateful
observers. The initial build encountered transient object/IFC permission errors;
retry completed successfully without changing user processes or deleting output.

Guarded dispatch resource ownership now has a fault-injection regression for both
runtime and compile-time observers. It injects failure at five dispatch allocation
positions, then either runs the queue or destroys the context with queued work.
Every case verifies exactly one destruction of the resource held by the inner
task, no duplicate report or simultaneous caller/observer failure, and execution
only for successful, non-discarded dispatch. The Windows full allocation suite
passes (0.49s). This covers undispatched heap-owned callback queue cleanup, not
destruction of active socket/timer operations or concurrent worker teardown.

The host no longer ignores the boolean HTTP request-drain result. A timeout now
preserves timed_out unless an earlier error exists and immediately interrupts
business/management socket I/O. A real slow-handler test requests shutdown from
inside the handler, exceeds a 200ms drain budget, eventually settles, and verifies
that the host returns timed_out in stopped state rather than success. Windows
Application and allocation suites pass (3.84s, 0.48s). This does not fix the separate
final connection deadline renewal, nor prove services cannot close while an
arbitrary non-socket handler still references them; both remain explicit gates.

A real slow-handler test with a managed dependency reproduced premature service
stop: the stop event preceded the handler-settled event. The host now waits for
tracked handlers after socket interruption using the existing overall shutdown
deadline, before canceling background tasks and stopping services. Normal service
stop and cleanup retries are gated on zero tracked handlers; unresolved handlers
at budget expiry leave service registration intact. The regression now verifies
handler settlement before service stop while preserving the earlier timed_out.
Windows Application/allocation suites pass (4.13s/0.49s). This does not solve final
destruction of retained resources after budget expiry, or untracked application
work; no hard process-exit or universal safe-destruction claim is made.

The request-drain poll no longer always schedules 50ms. It schedules the minimum
of 50ms and the precise remaining steady-clock duration. A new test keeps tracked
work pending, verifies zero budget schedules no sleep and a 5ms budget never
requests a sleep above that budget, then cancels and joins the tracked work.
Windows Application passes (4.13s). This constrains requested timer duration, not
OS scheduling latency or the still-open final connection deadline renewal.

Request shutdown now reuses request_context's existing cancellation token.
shutdown_handler tracks tokens through intrusive middleware-frame registrations,
removes them on normal/exceptional unwinding, and cancel_requests notifies tracked
tokens after drain timeout. Counter completion uses release/acquire publication.
The drain test now cancels a pending ten-second timer through this registry and
waits for the tracked task and count to settle. Windows Application/allocation
suites pass (4.13s/0.49s). The registry adds no separate node allocation, but frame
size and latch throughput require measurement. It is not yet linked to the fresh
tokens produced by request.with_deadline, and callback arbitration across workers,
exceptional shutdown paths and residual resource destruction remain open.

Request cancellation now fans out to independent with_deadline child tokens via
request-owned intrusive registrations. The factory is owned by the wrapper frame,
including move-only temporary factories. The drain regression joins two child
timers and a direct timer after cancellation. A second regression completes and
destroys one child before repeated cancellation, then starts a late child through
a temporary owning factory and verifies cancellation and factory resource release.
Windows Application passes (4.14s); the preceding Windows and Arch ASAN Application
and disabled-overhead suites also pass. These checks do not measure the additional
request/wrapper frame cost or establish arbitrary inline-callback reentrancy safety.
Direct cancellation_token().cancel() does not fan out; callers use
cancel_pending_operations() for request-level cancellation. Exceptional host
cleanup, cross-worker races, unified final deadlines and safe residual destruction
remain open; passing allocation tests is not end-to-end throughput evidence.

The generic with_deadline factory overload also had a deferred-lifetime defect:
its forwarding-reference parameter retained a reference to an already destroyed
temporary closure. A new regression checks captured ownership before starting
the task and failed on Windows against the previous implementation. After the
fix, Windows Application/deadline suites pass (4.13s/0.01s), and Arch ASAN's
deadline suite passes (0.09s). The overload
now stores the factory by value in its existing coroutine frame; move-only
factories are supported and callers may explicitly use std::ref for externally
owned factories. This is lifetime correctness, not a measured performance claim.
Cancellation review also confirmed QUIC channel notification may resume inline;
the request registry's lock-held dispatch remains a real reentrancy risk, not
merely an untested hypothetical. It must be resolved before closure is claimed.

Cancelled request child completion and tracked middleware completion now post
back to their I/O context before RAII unregistration, including captured exception
paths. Non-cancelled completion does not take that post. A regression exercises
legacy inline-resume cancellation for both direct and child operations, with
normal and exceptional completion, checking request release and original error
propagation. This addresses completion-unlink reentrancy, not arbitrary user code
reentering registration during a cancellation callback. The extra result/error
storage and conditional post node need fresh frame-cost measurement. Windows
Application/disabled-overhead pass (4.20s/0.50s), as do Arch ASAN equivalents
(3.40s/3.05s); formatting and generated instructions checks pass. Full callback
reentrancy, cross-worker races and residual destruction remain open.

A new warmed allocation probe measures constructing and synchronously completing
an unlimited-deadline operation, excluding request/context setup and result
assertions. Before removing duplicate result storage, Windows measured direct
4000/request 4176 bytes with three allocations each; Arch ASAN measured direct
960/request 1056 bytes with two each. The request object is 704 bytes on both.
Request with_deadline now waits for completion without extracting the promise,
performs the cancellation-only post, then reads the existing promise result or
exception. Windows request allocation drops to 4080 bytes (96 fewer), still 80
above the direct case; Application and disabled-overhead suites pass (4.20s/0.51s).
This measurement is not elapsed-time throughput, timed/streaming behavior, or a
pre-instrumentation baseline. It does not prove zero cost or finish the objective.
The same optimization regressed Arch ASAN to THREE allocations (1000 bytes),
failing the new equal-allocation-count assertion despite lower byte volume.
The promise-retaining optimization was therefore reverted on all platforms;
the new allocation regression remains, as does cancellation-safe completion.
Current implementation retains the original result/error storage pending a
cross-platform improvement that does not add allocation calls. After reverting,
Windows Application/disabled-overhead pass again (4.21s/0.49s). A transient
Windows library-write failure cleared on incremental link retry without deleting
build artifacts or terminating external processes.

Redis pool waiter cancellation no longer tries the pool mutex and allocates a
post/helper detached coroutine. It claims pending completion and posts the
waiter's frame-owned node; the original waiter reacquires the coroutine mutex
and unlinks itself. Pool-stop and successful lease delivery use the same node
and existing completion claim. A fault-injection test arms the next allocation
to fail after registration and verifies caller cancellation and pool-stop plus
repeated cancellation schedule with zero allocations, settle the task, preserve
operation_canceled and leave zero waiters. Windows Application/allocation suites
pass (4.18s/0.49s). This is scoped to waiter notifications; connection maintenance,
return paths, live-service races and total acquisition frame cost need further
validation. No whole-Redis zero-allocation or universal safe-destruction claim.

A 128-iteration Redis waiter regression now races a foreign-thread token cancel
against owner-thread pool shutdown. Atomic start coordination and varied yields
exercise competing completion claims; owner polling may run before the cancelling
thread joins, and a final poll checks no second completion. Each iteration asserts
one acquisition completion, operation_canceled, a settled stop task, and zero
waiters. Windows allocation suite passes (0.49s). This is stress evidence, not a
proof of all interleavings or a ThreadSanitizer result; live lease delivery and
Redis outage/recovery are not exercised by this empty-pool fixture.

The Application Redis TCP-peer fixture now queues a borrower behind a live lease
and tests both deterministic claim orders. Returning the lease before cancellation
must deliver an open connection; cancellation before return must report
operation_canceled and preserve the reusable idle connection. Both paths check
zero queued waiters and one idle connection after RAII release. Windows Redis
and Application Redis suites pass (1.07s/0.27s); Arch ASAN Redis passes (1.02s).
These suites exercise real local sockets against a protocol simulator, not a
redis-server process, and do not substitute for the real-service recovery gate.

An opt-in test_application_redis_live target now accepts an explicit loopback
port, negotiates RESP3, probes PING health three times, joins supervised shutdown
and checks drained waiters/maintenance. It makes no key writes. CTest records
return 77 as skipped when integration is not enabled; missing/invalid ports fail.
Windows and Arch ASAN compile the entry point; Windows skip/invalid-port checks
pass. No server execution is claimed: Arch has no Redis/Valkey binary and package
download failed with mirror TLS EOF. The attempted download installed/upgraded
no packages and launched no service. Its temporary cache directory is
/tmp/cnetmod-redis-live.sZPZN1SJ. This is test infrastructure, not completion of
real Redis health/recovery, authentication, telemetry or outage acceptance.

The Linux Release/Debug workflow now provisions a dedicated redis:7.4 container
for the live lifecycle executable. It binds a dynamically allocated loopback
port, uses tmpfs for data, disables snapshot/AOF persistence, bounds readiness
polls and test runtime, and installs exit cleanup preserving the test status.
The test runs directly so an unexpected skip exit fails CI. YAML parsing and
bash syntax checking pass locally; GitHub Actions has NOT been dispatched and
no real Redis process result is available. Windows downloads failed with the
same TLS EOF as WSL; the official mirror-list query also failed. These network
failures do not prevent continued local implementation and are not a full-goal
blocker. Container launch/cleanup and service behavior still require execution.

Host orchestration-exception cleanup now cancels tracked requests and aborts
business/management connection I/O immediately after stopping listeners, before
requesting supervisor stop and entering finish. Previously only normal drain
timeout issued those notifications, so exceptional cleanup could wait on an
uncancelled downstream request. Existing primary error preservation is unchanged.
Windows Application regression passes (4.24s), and formatting passes. A dedicated
orchestration-exception injection with a live handler is still missing; existing
tests do not establish that whole path, arbitrary callback reentrancy, or safe
destruction of uncooperative residual work.

A real HTTP request now drives an orchestration allocation-failure regression.
Its controlled token-aware wait arms the next allocation failure only after the
handler is suspended; the fault is thread-local to the host, not the HTTP client.
The test verifies the fault was consumed, the handler receives cancellation,
run returns not_enough_memory, and host state reaches stopped. Windows targeted
test passes (0.14s), and its complete allocation suite passes (0.62s). This fixture
has no managed business dependency, so exceptional dependency-stop ordering and
uncooperative handler destruction remain distinct missing coverage.

The active-handler orchestration-fault fixture now registers a required managed
dependency. It verifies one start and one stop, and the stop callback observes
that the cancelled handler finished its business code; the original
not_enough_memory and final stopped assertions remain. Windows complete allocation
suite passes (0.61s). This extends the prior no-dependency fixture, not proof of
arbitrary dependency graphs, background-task cancellation order or uncooperative
coroutine destruction.

Adding a supervised dependency worker and a five-millisecond asynchronous handler
cleanup reproduced premature supervisor cancellation on Windows: the worker stop
callback ran before the handler finished. Exceptional entry no longer stops the
supervisor immediately; finish first settles tracked handlers, then requests and
joins supervisor stop before service cleanup retries. Emergency fallback still
requests task stop. The strengthened test now passes, preserving the original
allocation error and checking worker completion, stop notification ordering and
single service stop. Windows Application/allocation suites pass (4.23s/0.63s).
Expired request budgets and failed emergency cleanup still need residual-lifetime
handling; this does not establish bounded safe destruction for arbitrary work.

Request cancellation now publishes an atomic terminal flag before invoking any
token callback. Repeated request-level cancellation returns without taking the
registry latch. Late child registration skips the list and receives an already
cancelled token, with a second terminal check under the latch for concurrent
registration. A nullable owner replaces the former owner reference, so an
unregistered node needs no extra membership field. The inline-completion test
now reenters request cancellation and creates a nested child before normal or
exceptional completion. This does not yet address recursive shutdown_handler
cancellation, arbitrary custom callbacks, or all cross-thread lifetimes.

The request-level terminal-state change passes the complete Application and
disabled-overhead suites on Windows Release (4.22s / 0.63s) and Arch ASAN
(3.34s / 3.22s). The resumed Arch build also completed successfully. The isolated
deadline allocation diagnostic remains unchanged: Windows direct/request use
3 allocations and 4000/4176 total bytes; Arch uses 2 allocations and 960/1056
total bytes. request_context remains 704 bytes on both configurations. Thus this
change adds no measured allocation cost over the preceding implementation,
but the request wrapper still has a 176-byte/96-byte allocation-size premium
over the direct helper. This diagnostic is not a throughput benchmark and does
not prove zero overhead for every disabled telemetry path. Recursive
shutdown_handler cancellation and bounded residual-resource destruction remain
open safety gates.

Shutdown cancellation now publishes the existing admission-stop signal and an
atomic broadcast terminal state before taking the request registry latch.
Recursive or concurrent repeated broadcasts return without entering that latch;
this is idempotent cancellation, not a join of the first caller. A registration
that raced cancellation dispatches its catch-up cancellation after releasing
the registry latch. Normal admission reuses its existing stop-signal read.
The inline-completion fixture now recursively cancels the shutdown handler and
submits a late request from that callback, checking 503, no business dispatch,
and unchanged in-flight count, for direct/child and normal/exceptional paths.
Windows Release Application/disabled-overhead suites pass (4.19s / 0.63s), as do
Arch ASAN suites (3.23s / 3.23s). Both incremental builds completed; generated
instructions match their sources. Broadcast still holds the registry latch
while dispatching existing tokens, so arbitrary reentrant event-loop polling,
request destruction and all cross-thread lifetime interleavings are not proven
safe by these tests. The earlier broader safety and performance gates remain.

Disabled HTTP execution allocation parity now uses a nonempty root identity
and a 247-byte tracestate instead of only an empty parent. Both cancellable
and non-cancellable entry points preserve the raw transport error and match
its allocation count and total bytes. The separate dispatch-only test retains
the empty-parent case. Windows Release and Arch ASAN disabled-overhead suites
pass (0.63s / 3.20s), and format validation passes. This strengthens detection
of accidental parent snapshots on the disabled branch; it still tests a local
invalid-URL error path, not successful network throughput or every protocol.

Source inspection also found an unclosed messaging propagation gate: all four
messaging inject adapters replace traceparent but leave an existing tracestate
when the new context has an empty tracestate. Reused carriers can therefore
retain stale vendor state. This needs an explicit replacement regression and
verification with Kafka/MQTT/AMQP enabled; the current Windows/Arch allocation
build configurations disable these protocols and cannot establish that gate.

The stale messaging tracestate gate now has an implementation and Windows
regression evidence. Kafka/MQTT injection erases matching state entries even
when the replacement is empty; AMQP 0-9-1/1.0 erase the corresponding map key.
The carrier reuse test injects a previous vendor state followed by a new root
without state, checking the new trace identity, empty extracted state and
absence of the old metadata entry. All four protocols are enabled in
cmake-build-mvsc-release. Its core and test_otlp_exporter build completed, the
focused messaging test passed, and the complete exporter suite passed (1.19s).
Formatting and generated-instruction checks pass. This is an in-memory carrier
regression, not broker delivery, Linux/macOS evidence or transactional behavior
under metadata allocation failure; those gates remain open.

Message injection now uses a shared prepare/swap transaction over metadata
only. Each public inject overload is noexcept; failed preparation leaves the
original metadata intact, and a static assertion requires a nonthrowing swap.
The business payload is not copied. A fault-injection regression tries 64
allocation positions for each of Kafka, MQTT, AMQP 0-9-1 and AMQP 1.0, requiring
both failure and success cases and checking that trace ID, span ID and state
are entirely old or entirely new. Windows with all four protocols enabled
passes disabled-overhead and OTLP suites (0.63s / 1.20s). Arch ASAN with these
four protocols disabled also builds and passes those suites (3.17s / 0.57s),
which verifies conditional imports but not the four transaction implementations
on Linux. Formatting and instruction-generation checks pass.

This transaction copies all carrier metadata on the explicitly enabled path;
its cost needs measurement and possible targeted-update optimization. The
regression does not yet compare arbitrary business header values or actual
broker delivery. Inspection of application/integration/kafka.cpp also confirms
that Kafka service construction currently exposes a raw client without wiring
message instrumentation; lifecycle telemetry and standalone propagation helpers
must not be represented as completed automatic producer/consumer tracing.
Automatic message-path integration remains a required implementation gate.

The four-protocol injection fault fixture now includes non-trace business
metadata, checking its value and entry count after every failed/successful
allocation position. Kafka uses a binary business header, MQTT a user property,
and both AMQP variants application/header entries. Kafka and AMQP messages also
carry a 4096-byte binary body whose contents must remain unchanged. The Windows
all-four-protocol build and complete disabled-overhead suite pass (0.62s), with
format validation passing. This adds business-data preservation evidence for
these representative carriers; it is not a no-copy assertion, exhaustive
metadata-type coverage or broker-level delivery validation.

Kafka extraction now ignores unrelated header values before constructing a
view, and borrows matching trace header bytes until the parser produces the
owned context. It no longer copies every binary business header into a string.
A 64-KiB unrelated binary header plus an empty header produces no context,
performs zero measured allocations and leaves a fail-first allocation probe
unconsumed. Windows all-message-protocol core/test builds and complete
disabled-overhead/OTLP suites pass (0.63s / 1.21s); format validation passes.
This proves the no-trace-header path, not allocation-free parsing of a valid
trace context.

Kafka producer inspection locates the actual send boundary in
producer/kafka_producer.cpp: public send owns topic/record and awaits impl send,
which enqueues and awaits its pending record outcome. Future automatic tracing
must preserve that terminal result rather than ending when the record is
enqueued, and must validate ownership on observed-frame allocation failure.
No producer/consumer instrumentation adapter has been installed by this change.

Kafka injection no longer uses the full-metadata copy/swap helper. It prepares
the trace entries, reserves sufficient vector capacity, then removes old trace
keys and moves prepared entries into place. Compile-time checks require header
move construction and assignment to be nonthrowing. Allocation failure before
the commit preserves original metadata; the existing 64-position injection
regression and business-data checks still pass. A new storage test compares a
16-byte business header with a 1-MiB header: its byte-buffer pointer remains
unchanged, and measured injection allocation count and total bytes are equal.
Windows all-message-protocol builds and disabled-overhead/OTLP suites pass
(0.63s / 1.21s) after transient library/object file-lock errors cleared on
incremental retries. Formatting and generated instructions validate. MQTT and
AMQP still use copy/swap; no broad throughput or Linux Kafka claim follows.

MQTT injection now prepares trace properties and reserves capacity before a
nonthrowing erase/move commit, guarded by compile-time property move checks.
It no longer copies the complete property list. A new test compares 256-byte
and 1-MiB business user-property strings: the original character-buffer pointer
is retained and injection allocation count/bytes do not grow with that string.
Existing four-protocol allocation-failure and business-data preservation cases
still pass. MQTT extraction now borrows matching strings until parsing instead
of first copying them. Windows all-message-protocol core/test builds and
disabled-overhead/OTLP suites pass (0.64s / 1.21s). Generated instructions match
their source. AMQP copy/swap cost, automatic producer/consumer integration and
real-broker/platform/performance gates remain open.

Both AMQP injectors now share a trace-node preparation/merge helper instead of
copying the complete metadata map. The helper constructs trace-only nodes with
the destination allocator, checks the string-key/nonthrowing comparator contract,
then erases prior trace entries and transfers prepared nodes without allocating.
Business map nodes are not removed or replaced. New tests retain the mapped
string object and buffer addresses across injection and compare allocation
count/bytes for 256-byte and 1-MiB business values. Existing fault-injection,
business payload preservation and stale-state replacement tests pass. Windows
all-message-protocol build and disabled-overhead/OTLP suites pass (0.64s / 1.21s)
after a transient library lock cleared on retry. This does not remove the
remaining automatic message-path wiring, broker, platform and throughput gates.

The Arch ASAN configuration now enables Kafka, MQTT, WebSocket, AMQP 0-9-1
and AMQP 1.0. Clang 22.1.8/libc++ built the core and both observation test
executables successfully, including the nonthrowing map-comparator assertions.
Complete disabled-overhead and OTLP suites pass (3.24s / 0.61s). Focused runs
explicitly execute all three protocol storage-retention tests and the shared
four-protocol allocation-failure test, so this is no longer merely a disabled
conditional-import check on Linux. The cache retains address-sanitizer flags,
epoll and the system allocator. This updates the existing /root/cnetmod-otel-asan
configuration; future callers must not assume its messaging protocols are off.
Real-broker interoperability, automated producer/consumer observation and full
end-to-end performance remain separate unproven gates.

Kafka service start now forwards the service cancellation token to connect
and wraps the adapted operation in the supplied absolute deadline. The adapter
checks cancellation before executing Kafka work and maps Kafka cancellation and
request timeout to corresponding generic errors; other failures retain the
previous connection_refused mapping, so detailed error-category preservation
is still open. Started-service probes now request metadata with that same
cancellation/deadline contract rather than reporting the started flag as up.
A new test starts an unconfigured service with cancelled/expired contexts,
checks the expected error, down health and successful stop. Windows and Arch
ASAN core/integration-test builds pass; complete application integration suites
pass (0.01s / 0.08s). Formatting and generated instructions validate. These early
exit tests do not prove in-flight network timeout, healthy metadata exchange,
recovery after disconnect, or cleanup of partially established connections.

A new local TCP stalled-Kafka fixture reproduced a 15-second Windows test
timeout despite a 500-ms application deadline. broker_connection accepted a
token but omitted it from Happy Eyeballs and its handshake/read/write helpers.
The token now reaches those cancellable overloads, including response prefix
and body reads and send-only writes. The application adapter recognizes an
active cancellation when normalizing a failed Kafka result. The fixture accepts
TCP, observes request bytes, withholds a reply, and tests deadline expiry plus
explicit cancellation after request arrival. It verifies terminal error,
completed owned tasks, service stop and peer disconnect. Complete Application
integration suites now pass on Windows (0.52s) and Arch ASAN (0.58s); both core
and test builds succeed. Formatting and generated instructions validate.
This is real local socket I/O, not a real Kafka broker, TLS handshake test,
successful metadata exchange or proof of cancellable request-lock waiting.
Detailed Kafka error-code preservation and all broader goals remain open.

Kafka connection notification now contains each observer exception separately.
The local stalled-peer regression registers a throwing observer followed by a
recording observer, checks one connected and one disconnected notification for
both, and retains cancellation/deadline outcomes, socket disconnect and service
stop assertions. Windows and Arch ASAN Application integration suites pass
(0.52s / 0.57s), with both builds, formatting and generated instructions passing.
This fixes exception propagation that could suppress later observers or skip
transport cleanup. Observer-list mutation during dispatch, callback-driven
connection reentry and cross-thread registration remain unproven; this change
does not install automatic message tracing or finish the overall goal.

Kafka observer dispatch fixes same-thread registration during callbacks: it
captures the initial count, takes a strong reference before each callback, and
does not retain vector references across callbacks. Expired entries are removed
after delivery. A loopback connection test appends 64 registrations from the
first connection callback, checks no delivery to them in that event, reconnects
and checks all 64 deliveries while existing observers receive each event once.
Windows and Arch ASAN core/test builds and integration suites pass (0.53s /
0.58s); formatting and generated instructions validate. No vector snapshot was
introduced. This covers append-driven reallocation, not nested connection-event
dispatch, connection destruction during callbacks or concurrent registration.
The full Application/OTEL objective remains unproven.

Kafka lifecycle failures other than cancellation/timeouts now retain their
numeric Kafka code in a cnetmod.kafka error category instead of collapsing to
connection_refused. Default conditions map configuration, transport, malformed
response and authorization categories to generic conditions without replacing
the stored number. Message text is generated from a fixed prefix and number,
not broker-provided diagnostics. A real facade failure with no bootstrap
configuration verifies code 1003, category identity, invalid_argument condition
and sanitized text. Windows and Arch ASAN integration suites pass (0.52s /
0.57s); builds, formatting and generated instructions pass. The test directly
covers configuration errors; broker authorization and other mappings still need
wire-level fixtures. All broader lifecycle and observation gates remain open.

### Kafka malformed-response disconnect notifications

The local TCP Application fixture now covers invalid response lengths and mismatched
correlation IDs, alongside deadline expiry and explicit cancellation. Before the fix,
both malformed-response cases closed the socket but omitted both registered observers'
disconnect callbacks. The exchange path now reports its original error consistently
before closing, including response-body read failures. The fixture verifies socket
closure, successful service stop, exception isolation between observers, exactly one
disconnect notification, and preserved Kafka malformed-response identity and protocol
error condition. Windows Release passed in 0.53 seconds and Arch Clang 22 ASAN passed
in 0.58 seconds. This is controlled local TCP
evidence, not real-broker or complete producer/consumer instrumentation validation.

### Kafka nested observer dispatch safety

A local TCP regression deliberately reconnects from a connection observer while an
expired registration separates two live observers. The unmodified dispatcher failed
with an ASAN container-overflow: nested dispatch compacted the vector while the outer
dispatch retained its original indices. Compaction is now deferred until the outermost
dispatch exits. Empty registration lists return immediately; dispatch neither copies
the registration list nor allocates a snapshot. Windows Release passed in 0.53 seconds.
This validates nested notification traversal, not connection destruction from callbacks,
cross-thread sharing, or disabled-OTEL throughput parity.

### Kafka partial-body lifecycle regression

The failed-exchange fixture now also sends a valid length prefix followed by only
half of the declared body. Closing the peer preserves the Kafka transport error and
generic I/O error condition; leaving the peer open exits at the Application deadline
with timed_out. Both cases verify exactly one notification to each observer (including
a throwing observer), completion of owned coroutines, and successful service stop.
Peer behavior uses named enum cases instead of numeric modes. The complete integration
suite passed on Windows Release in 1.03 seconds and Arch Clang 22 ASAN in 1.07 seconds.
The preceding nested-observer regression also passed under Arch ASAN in 0.57 seconds.
These checks do not establish real-broker recovery or throughput parity.

### Kafka metadata observer exception isolation

A new regression confirms that a throwing metadata observer previously escaped from
update after the cache was committed and prevented a later observer from running.
Callbacks are now individually exception-isolated outside the cache latch. The test
checks committed controller identity and delivery to both observers. Notification
snapshot allocation failures and the unnecessary snapshot copy with no observers remain
separate open performance/failure-isolation work; this change does not claim those solved.

### Kafka metadata update without observers

Metadata updates now skip the notification snapshot when no live observer remains,
including an expired registration. A single-invocation allocation window with the next
allocation forced to fail verifies zero allocation calls/bytes and retention of a 64 KiB
cluster ID and controller identity. The initial test incorrectly reused a 256-iteration
measurement helper with moved input; that fixture was corrected to measure one update.
Windows allocation and integration suites passed (0.63 s / 1.03 s); Arch ASAN allocation
and integration suites passed (3.18 s / 1.06 s). Snapshot preparation failures when live
observers exist remain open. This is allocation evidence, not throughput parity.

### Kafka metadata notification preparation failure isolation

Fault injection reproduced bad_alloc escaping from both observer-list allocation and
metadata snapshot copying. Notification preparation now catches these failures after
cache commit, skips that notification, and retains observer registrations. Expired
registrations are removed with a nonthrowing predicate before list construction. Eight
allocation positions cover injected failures and successful notification, verify the
64 KiB cluster identity remains intact, and verify a later update notifies again.
Callbacks remain outside the cache latch. This best-effort contract is documented;
required business work must not depend on these optional notifications.

### Kafka nested metadata snapshot contract

The integration regression now performs a metadata update from inside an observer,
registering another observer before the nested update. It verifies that the outer
snapshot remains unchanged, the cache retains the nested/latest state, and a newly
registered observer receives only the nested event. Existing later observers receive
the nested event before the outer event completes; event delivery is therefore not a
monotonic version stream and must not be used to overwrite authoritative cache state.
Windows Release integration passed in 1.04 seconds. No production change was needed.

### Kafka automatic instrumentation boundary audit

Current source inspection confirms that application/integration/kafka.cpp returns the
raw client_facade, whose make_producer/make_consumer construct protocol clients without
operation instrumentation. observability/messaging.cppm only provides explicit carrier
injection/extraction. Connection and metadata observers cannot establish per-message
producer/consumer spans or automatic parent propagation. These are still missing,
regardless of the transport and fault-injection tests above.

The next implementation must cover per-record send completion (not merely send_batch),
retain one parent per record when batching unrelated requests, and distinguish receive
from user processing/commit. A polling span must not become the parent of every message
in a mixed-trace batch. Protocol modules must depend only on neutral instrumentation
contracts; OTEL export and wire-context adapters belong in observability. Application
must install the adapter only when observation is enabled, while the disabled factory
returns the original implementation without a wrapper coroutine or carrier mutation.
Tests must use controllable producer/consumer backends to verify parent identities,
exact terminal outcomes, observer failure isolation, unchanged Kafka results, and raw
versus disabled allocation counts before real-broker validation. The existing coroutine-
owned operation_scope is reusable, but an automatic Kafka adapter has not been added.

### Initial Kafka per-record send adapter

observability/kafka_producer.cppm and .cpp now provide send_kafka_record with an explicit
parent and optional span sink. The disabled branch returns producer.send directly; the
enabled path owns an operation_scope, injects its context before send, and records a
producer terminal span without payload, topic, or broker diagnostic attributes. Arch
ASAN integration verifies a closed-producer error is preserved and the span retains
the caller's trace and parent identity. This is an initial adapter, not automatic
Application integration: consumer processing, metrics, live success/cancellation,
enabled-wrapper allocation failure, Windows validation, and factory wiring remain.

### Kafka send adapter Windows and throwing-sink validation

Windows CMake was explicitly regenerated so the new kafka_producer observation module
was actually compiled (building the old project with project references disabled did
not discover the added source). Release allocation and integration suites passed in
0.64 s and 1.04 s. The disabled closed-producer path matches raw allocation calls/bytes
on Windows as well as Arch. A new regression exercises a throwing exporter with and
without a cancellation-token argument, preserving the Kafka error code and message
and invoking the exporter once per operation. Arch ASAN integration passed in 1.17 s.
This does not validate successful wire sends, actual cancellation, wrapper allocation
failure, Application wiring, or consumer observation.

### Kafka successful send backend contract

A controllable producer_backend now captures the records delivered by the real producer
implementation. Enabled observation preserves the caller trace, injects the completed
producer span identity into the record, and preserves the backend's offset and topic.
Disabled observation exports nothing and adds no tracing headers. Both modes preserve
binary message payload and the business header key. Windows integration passed in
1.03 seconds. This is a backend contract test, not a real Kafka wire/broker test;
multi-record batching, consumer processing and automatic Application wiring remain open.

### Kafka send terminal outcome matrix

The controllable backend exercises success, cancelled, request_timed_out and transport
results with observation enabled and disabled. Assertions preserve Kafka code, message
and retriable flag; enabled spans carry the corresponding operation_status and never
copy backend diagnostics into attributes. Cancellation deliberately retains the shared
operation_scope contract (cancelled is not failed); the initial test assumption that
every non-success was failed was corrected after inspecting the implementation.
Windows integration passed in 1.03 seconds. This tests returned cancellation errors,
not cancellation races or interruptibility of real network sends.

### Kafka backend exception terminal classification

The backend matrix now includes thrown runtime_error with observation enabled and
disabled. It reproduced enabled spans incorrectly reporting abandoned. observed_send
now completes an error outcome before rethrowing the original exception; the test
checks its original type/message, one completed span, and unchanged payload/context.
Windows Release integration passed in 1.03 s and Arch ASAN in 1.09 s. Destruction of an
uncompleted task still uses the separate abandonment contract. Producer internal
reusability after an exception and enabled wrapper allocation failure remain unverified.

### Kafka producer exceptional batch cleanup

A follow-up send after backend exception reproduced a stalled producer in both raw
and observed modes: batch.flushing remained set. Exceptional batch cleanup now detaches
queued records, clears inflight state, and publishes the original exception to unfinished
records before resuming waiters. Non-idempotent sequential reuse passes on Windows
Release (1.03 s integration) and Arch ASAN (1.09 s). Idempotent/transactional producers
are closed on an unknown exceptional outcome rather than risking sequence reuse.
Concurrent waiter settlement, other partitions during fatal close, allocation faults,
and idempotent fatal-state behavior still need direct tests. The new exception_ptr in
pending-record state has a storage cost that must be measured/optimized before claiming
the full no-regression performance requirement; this is not performance acceptance.

### Kafka pending-result storage consolidation

The separate exception_ptr added for exceptional batch cleanup has been removed.
Pending completion now stores monostate, Kafka result, or exception_ptr in one variant.
A production static_assert proves its size does not exceed the previous optional Kafka
result slot on both MSVC and Clang/libc++. This avoids simultaneous storage for mutually
exclusive outcomes. Windows integration/allocation suites passed (1.03 s / 0.62 s), as
did Arch ASAN (1.07 s / 3.19 s). This proves the terminal storage bound and existing
regressions, not throughput parity or concurrent exceptional batch settlement.

### Kafka queued send exception settlement

The producer backend regression now reentrantly submits a second raw send to the same
partition while the first send is inside send_batch. It verifies the queued coroutine
is suspended before the backend throws, then completes with the original exception
without requiring close. Subsequent non-idempotent sending still succeeds. The scenario
runs with the initiating send observed and unobserved; Windows integration passed in
1.03 seconds. This covers one queued send, not simultaneous flush waiters, other
partitions, idempotent shutdown, or task destruction races.

### Kafka concurrent send and flush waiters

Adding flush while a queued send awaited the same pending record reproduced waiter
handle overwrite and an unfinished task. Pending records now hold a frame-owned waiter
list instead of one handle. Notification pops each node before resuming, pins the state,
and allocates no notification nodes. Awaiter destruction unlinks its node; flush reads
the stored result rather than moving it. The backend-exception regression settles both
send and flush on Windows Release (1.02 s) and Arch ASAN (1.06 s). Successful concurrent
flush ordering, waiter destruction races and coroutine-frame overhead remain unverified.

### Kafka successful and returned-error concurrent flush

The queued-send plus flush scenario now runs for every backend outcome, not only
exceptions. It checks that the queued send retains its topic and offset on success,
and both send and flush preserve the original Kafka error code and diagnostic on
returned failures. The backend captures the first record separately so a later queued
batch cannot invalidate payload/trace assertions. Windows integration passed in 1.04 s.
This verifies the tested registration order; arbitrary reentry, multiple flush waiters,
destruction and cross-partition fatal cleanup still need coverage.

### Kafka multiple flush waiters and withdrawal

The controlled concurrent-send regression now registers three flush tasks, destroys
the middle suspended task, and retains two others. Both surviving flushes and the queued
send complete with their expected success, returned error, or original exception.
Windows integration passed in 1.03 seconds. This exercises frame-owned waiter unlinking
from the middle of the list without allocating notification nodes. Cross-thread
destruction, arbitrary continuation reentry, and whole-producer destruction remain
outside this evidence.

### Kafka idempotent exceptional-outcome policy

The backend/send/flush matrix now also enables producer idempotence. After a thrown
backend exception, pending send/flush tasks settle and a subsequent send promptly
returns the closed-producer configuration error, rather than reusing an uncertain
sequence. Non-idempotent reuse still succeeds. Both observed and raw paths run this
matrix; Windows integration passed in 1.03 seconds. This validates the local fatal
policy, not exactly-once delivery against a broker or cross-partition cleanup.

### Initial Kafka per-message processing scope

observability/kafka_consumer.cppm and .cpp add start_kafka_processing. It lazily extracts
the individual record's parent and returns a coroutine-owned CONSUMER operation_scope;
the caller explicitly completes processing separately from offset commit. Two distinct
parent traces completed in reverse order retain their own identities and outcomes.
The disabled path skips extraction and context creation, verified with allocation fault
injection and zero allocation calls/bytes on Windows. Windows integration/allocation
suites passed (1.03 s / 0.63 s). This remains an explicit scope, not automatic polling
or Application wiring; metrics, remote-parent metadata, malformed header cases and
processing/commit workflow integration still require implementation or verification.

### Kafka processing parent validation and sampling

Consumer-scope regressions verify absent and all-zero traceparent values start a valid
new trace with no parent span ID, without changing header count. A valid unsampled
parent retains its trace identity and sampled flag, invokes neither the root sampler
nor the completion sink. Windows integration passed in 1.03 seconds. This is not an
exhaustive header parser fuzz test; duplicate-header policy and remote-parent export
metadata remain to be reviewed.

### Kafka processing allocation-failure isolation

Thirty-two allocation positions around start_kafka_processing exercise injected
allocation failures and successful scope creation. The test verifies completion export
matches scope activation, original trace/span/tracestate headers survive, and the 4 KiB
payload retains its storage address and size. Windows allocation suite passed in
0.65 seconds. This covers processing-scope creation, not enabled producer wrapper-frame
allocation or automatic consumer task supervision.

### Kafka producer scope-failure fallback and cancellation-path allocation parity

An inactive producer operation scope now returns the original send task directly,
instead of allocating an empty observation wrapper. An exporter whose copy constructor
throws verifies that both cancellation-token and token-free calls retain the original
closed-producer error. Windows and Arch ASAN integration suites passed in 1.03 s and
1.09 s. Disabled-send allocation parity now checks both overload paths over 256 calls
each; allocation counts and bytes match direct sends on both platforms. Allocation
suites passed in 0.63 s and 3.19 s respectively. These allocation assertions cover a
closed producer, not successful broker traffic or throughput. Enabled wrapper-frame
allocation isolation and automatic Application messaging instrumentation remain open.
Formatting checks passed for the three relevant C++ files; AGENTS.md is current.

### Kafka observation-frame allocation failure

The enabled producer wrapper now transfers request ownership during the coroutine
parameter move, after frame allocation succeeds. Its initial parameter only borrows
the caller-owned request. A bad_alloc before that transfer therefore falls back to
the original send without losing the topic or record; no additional heap allocation
is introduced for this transfer. A root sampler arms a one-shot allocation failure
after scope setup, targeting the observation frame. Both token and token-free tests
retain the closed-producer error instead of exposing the instrumentation bad_alloc.
Windows integration/allocation suites passed in 1.04 s / 0.63 s; Arch ASAN suites
passed in 1.08 s / 3.22 s. Formatting checks passed. Existing successful-send fixture
tests also exercise the new owning transfer. Fault-injected successful broker delivery,
automatic Application integration and end-to-end performance remain unverified.

### Successful Kafka fallback and disabled allocation parity

The observation-frame allocation regression now uses a successful producer backend
instead of a closed producer. After the injected frame failure it verifies the exact
128-byte topic, offset 17, 4 KiB binary value and original business header. No tracing
header reaches that fallback backend. Both cancellation-token paths are covered.
The same warmed backend compares 256 successful direct sends against 256 disabled
observation sends with identical messages: allocation counts and bytes match on
Windows and Arch ASAN. Allocation suites passed in 0.63 s and 3.22 s; formatting and
AGENTS.md synchronization checks passed. This is an in-process backend fixture, not
a real broker or a throughput benchmark; network delivery and full Application
instrumentation remain separate open requirements.

### Application Kafka producer observation factory

Added an owning instrumented_kafka_producer decorator with explicit per-call parent
context. Transaction operations, flush, identity queries and close delegate to the raw
producer. kafka_service::make_producer binds the sink supplied by auto-configuration;
a sink-copy failure degrades to an empty sink while preserving a created producer.
Raw client access remains available. Windows integration/allocation suites passed in
1.03 s / 0.64 s; Arch ASAN suites passed in 1.07 s / 3.32 s. The owning decorator's
closed-producer disabled path matches raw allocation counts and bytes for both token
variants, and enabled error spans retain parent identity. Documentation and generated
AGENTS.md are synchronized. Successful auto-configured factory delivery, sink-copy
failure at the factory, transactional forwarding and producer lifetime supervision
still need direct tests; this factory does not itself manage task shutdown or flush.

### Kafka producer decorator transaction regression

A shared in-process producer backend now verifies eight combinations of enabled or
disabled observation, commit or abort, and successful or failed transaction completion.
The decorator preserves the consumer group, offset 18, cancellation token identity,
completion decision, backend error code/message and ready/fatal terminal state. Explicit
flush does not finish the transaction. Exactly one send span is exported when enabled;
transaction forwarding does not create duplicate send spans. Windows integration suite
passed in 1.03 s; Arch ASAN passed in 1.12 s. Formatting and AGENTS.md checks passed.
This validates decorator delegation, not broker transaction durability,
transaction operation spans, concurrent close or automatic producer lifetime management.

### Closed Kafka producer transaction admission

A new regression reproduced transaction requests reaching the backend after close:
the Windows baseline failed with 12 assertions, including successful begin, offset
submission and commit on a closed producer. The protocol producer now rejects closed
transaction admission and rechecks closure after initialization and before submitting
transaction completion after flush. Tests cover tasks created before close but executed
afterward, both previously active and inactive transactions, plus synchronous closure
during identity initialization. Windows integration passed in 1.03 s and Arch ASAN
passed in 1.09 s; formatting and
AGENTS.md checks passed. This is not cancellation of an already submitted backend
request, nor proof of safe concurrent producer destruction or a global shutdown bound.

### Kafka close diagnostic allocation isolation

close() previously allocated its long diagnostic unconditionally inside noexcept and
copied it into each pending result. It now detaches each pending vector before waking
waiters and prepares each diagnostic within a failure boundary; an unavailable message
buffer retains the configuration error code. Already completed entries are skipped.
A fault-injection fixture queues a second send during backend execution, fails the
close diagnostic allocation and verifies that the queued send settles with its error.
Windows integration/allocation suites passed in 1.04 s / 0.63 s; Arch ASAN passed in
1.08 s / 3.27 s. This does not settle an arbitrary already submitted backend request
or prove cross-thread close/destruction safety. Producer supervision remains open.

### Final Application connection cleanup shares the shutdown budget

The final connection-wait phase previously granted a fresh HTTP drain interval and
then a fresh service stop interval after abort. Both phase deadlines now constrain
the existing shutdown_deadline. Timer waits use its native duration instead of
truncating positive sub-millisecond time to an early expiry. Existing Application
regressions passed on Windows (4.17 s) and Arch ASAN (3.24 s). Dedicated exhausted-budget
connection-cleanup coverage remains needed; these results do not establish safe
destruction of arbitrary uncooperative tasks or a hard process exit deadline.
Supervisor join and the minimum telemetry cancellation allowance remain open gates.

### Cancellation reserve for final HTTP connection cleanup

The incomplete-body regression now uses a 50 ms global shutdown budget and 200 ms
phase limits, with a 180 ms elapsed-time assertion allowing scheduling overhead while
rejecting a renewed full phase. It exposed 19,016 bytes in 11 leaked allocations under
Arch ASAN when socket abort occurred only at the global deadline. Final connection
waiting now reserves 20% of its remaining global budget for cancellation completions.
The same regression and Application suite pass on Windows (4.19 s) and Arch ASAN
(3.28 s), with no sanitizer leak report. This reserve does not solve the case where
earlier cleanup has already exhausted all budget before entering the connection phase;
that case and arbitrary uncooperative handlers remain open requirements.

### Preserve a connection reserve before dependency cleanup

Normal shutdown now constrains request drain, handler settlement, lifecycle stop and
stop retries plus telemetry delivery to a cleanup deadline reserving 20% of the total
configured budget. The reserve is fixed relative to that total rather than renewed per
phase. The incomplete-body fixture now includes a dependency with a 10-second stop
wait that responds to cancellation. Under a 50 ms total budget it verifies cancelled
and settled service stop, retained cleanup_failed state, closed peer, timeout identity
and elapsed time below 180 ms. Windows Application passed in 4.13 s and Arch ASAN in
3.19 s without a leak report. Uncooperative supervisor join, startup rollback and
exceptional cleanup remain outside this demonstrated budget guarantee.

### Listener startup failure uses reserved cleanup budget

Business and management listener failures now pass shutdown_cleanup_deadline() to
lifecycle stop rather than consuming the entire final shutdown budget. A real occupied
loopback port fixture compares the returned error with a direct HTTP server listen
failure, avoiding assumptions about the framework's network error category. Its slow
dependency stop is cancelled and settled under a 50 ms global budget; cleanup_failed
retains service ownership, and elapsed time remains below the 180 ms test threshold.
Windows Application passed in 4.18 s and Arch ASAN in 3.25 s. Formatting and generated
instructions checks passed. Lifecycle-internal startup rollback and exceptional failure
during finish itself still require separate budget and ownership validation.

### Startup rollback explicitly reserves caller cleanup time

service_lifecycle::start now accepts a nonnegative rollback reserve, defaulting to
zero for standalone callers. Application supplies 20% of its total stop budget.
Internal rollback constrains stop to the earlier deadline while rollback_deadline()
continues to expose the original absolute overall deadline. A regression verifies a
slow rollback responds to cancellation, preserves the initiating connection-refused
error, retains service ownership and leaves time for a successful subsequent stop.
Negative reserve rejection is checked before service invocation. Windows Application
passed in 4.24 s and Arch ASAN in 3.26 s. Documentation and AGENTS.md are synchronized.
This does not bound uncooperative service operations or resolve exceptional finish
failure and detached connection ownership in every shutdown scenario.

### Joint regression after lifecycle interface and shutdown-budget changes

Rebuilt and ran test_instrumentation, test_metric_aggregation,
test_http_observability, test_http_disabled_overhead, test_otlp_exporter and
test_application_integrations against the current core on Windows and Arch ASAN.
All six targets passed on each platform. Windows aggregate runtime was 4.11 s;
Arch individual runtimes were 0.09, 0.07, 1.19, 3.24, 0.60 and 1.09 s respectively.
This refreshes evidence for disabled allocation paths, independent HTTP signal
switches, scope failure containment, bounded metrics aggregation, HTTP OTLP delivery
and acknowledgement/retry handling, plus local Kafka protocol/observer fixtures.
These results are not real-broker integration, macOS verification, end-to-end
throughput evidence or proof that all background work has managed ownership.

### Kafka consumer maintenance ownership audit

Current source inspection confirms a remaining critical ownership gap in
src/protocol/kafka/client/kafka_client_facade.cpp. make_consumer starts
facade_consumer_backend::start_background_maintenance, which directly uses spawn.
The static maintenance loop waits on an uncancellable steady_timer and passes nullptr
to membership maintenance and automatic commit. Its weak backend reference avoids
holding the backend during timer sleep but does not cancel or join the timer task.
facade_consumer_backend::close only marks closed_ before acquiring the membership lock;
it does not await maintenance completion. client_facade::close closes transport sockets
without joining consumer maintenance. Thus neither factory construction nor close is
evidence of supervised heartbeat/auto-commit ownership. This is a source-level finding,
not yet a reproduced runtime leak. The required next implementation is an explicitly
owned maintenance task with cancellation propagated through timer and protocol work,
an Application supervisor binding, and cancellation/join before backend teardown.
Tests must include closing during timer sleep and during membership/commit I/O, failed
task registration, maintenance failure propagation, and raw-vs-disabled observation
behavior. Preserve consumer heartbeat semantics during migration; simply deleting the
background spawn without a replacement execution owner would be incorrect.

### Consumer maintenance cancellation and join implementation

facade_consumer_backend now owns a task_group for maintenance. Timer waits, membership
maintenance and auto-commit receive its child cancellation token. close() pins backend
lifetime, cancels and joins maintenance before acquiring membership and releasing fetch
sessions; task-group failure is reported after successful protocol cleanup. Backend
destruction requests cancellation as a fallback, not a substitute for async close.
make_consumer constructs its owning consumer before task dispatch and checks task
registration failure. The existing retry/heartbeat loop remains active; it was not
removed to avoid supervision work. Windows and Arch compile and existing integration
regressions pass (1.03 s / 1.31 s), with formatting corrected afterward. These tests do
not yet exercise actual consumer maintenance, so timer/heartbeat/commit cancellation,
registration failure and close/destruction interleavings need dedicated protocol tests.
Application supervisor recovery/health notification and raw client close joining all
consumers are still missing. task_group ownership alone does not establish that closure.

### Direct consumer maintenance timer cancellation verification

The local protocol fixture kafka_consumer_close_joins_sleeping_maintenance performs
ApiVersions and Metadata exchanges through client_facade, creates a consumer with a
five-second heartbeat interval, lets maintenance dispatch, then awaits close(). The
test requires close to finish within 150 ms, closes the client transport and checks
that both explicitly owned peer/exercise coroutines have completed. This directly
exercises cancellation and joining during the maintenance timer wait, rather than
only compiling the consumer factory. Incremental builds are current; Windows Release
test_application_integrations passed in 1.07 s and Arch ASAN in 1.12 s, each with a
15-second CTest timeout. No sanitizer failure was reported. The fixture uses a local
protocol peer, not a real Kafka broker. Membership/commit I/O cancellation, failed
registration, Application supervisor notification and joining consumers from raw
client close remain unverified or incomplete as described above.

### Consumer public operations remain sealed after close

Extending the local maintenance fixture reproduced assign({}) returning success
after consumer close (Windows: 18/19 integration cases passed, one failed). The
consumer facade now seals new subscribe/assign/poll/commit/seek operations when
the close coroutine starts, before delegating cleanup. Backend cleanup can still
commit offsets and leave the group, and callers can retry failed close. Guards
execute when operation tasks run, covering a task constructed before closure but
awaited afterward. The fixture checks all five operations plus a repeated close.
Windows Release passed all 19 integration cases (1.05 s); Arch ASAN passed the
same target (1.60 s). This state check is independent of telemetry enablement;
it is a protocol lifecycle correction, not an instrumentation fast-path branch.
No throughput parity claim is made from these functional tests. Concurrent use
from multiple executors and cancellation of already-running user operations are
not provided by this guard. The ownership contract and generated AGENTS.md are
updated; core API documentation uses an English multiline documentation comment.

### Failed consumer cleanup and disabled-allocation regression

kafka_consumer_lifecycle_cases.inc isolates a controllable backend fixture from the
main integration source. It verifies both a retriable timeout and a thrown cleanup
exception: original diagnostics/token identity survive, all five public operations
stay sealed without backend calls, a preconstructed poll is rejected when executed,
and a second close can successfully finish cleanup without reopening operations.
Creating a lazy close task alone does not seal the consumer; execution does.
Windows integration and disabled-overhead targets passed (1.05 s / 0.63 s); rebuilt
Arch ASAN counterparts passed (1.09 s / 3.21 s). The latter target refreshes existing
raw-vs-disabled allocation and result checks after the lifecycle change; it does not
establish throughput parity for every protocol. New test files pass clang-format.
Already-running operation cancellation, concurrent close interleavings and automatic
Application supervision remain separate outstanding requirements.

### Concurrent consumer cleanup is serialized and successful cleanup is idempotent

A suspended-backend regression reproduced two simultaneous close tasks invoking
cleanup twice and the second task completing before the first (three Windows
assertion failures). consumer::impl now owns a close-only async_mutex and records
successful cleanup. A queued close waits for the owner and skips backend work after
success; errors/exceptions release the lock and leave cleanup retryable. The lock
is not acquired by normal subscribe/assign/poll/commit/seek operations and is not
conditional on telemetry. Rebuilt integration targets passed on Windows (1.06 s)
and Arch ASAN (1.12 s), including the existing failed-cleanup retry cases. The API
and skill explicitly require callers to retain and await pending close tasks:
async_mutex waiters cannot currently be safely abandoned by destroying their frame.
This does not yet provide caller-token cancellation while waiting for that mutex,
settle in-flight user operations, or establish Application supervisor ownership.

### Application Kafka shutdown dependency audit

Source inspection after consumer close serialization identifies why that fix alone
cannot satisfy Application shutdown. kafka_service::stop calls client_.close and
returns success immediately. client_facade::close only closes bootstrap/connections;
runtime state has no consumer inventory, shutdown gate or completion join. Consumers
capture shared runtime ownership through lookup, coordinator factory and refresh
closures. Coordinator connections are separately owned by the group backend, outside
the client's connection map. ensure_metadata retries transport failure by resetting
bootstrap and calling connect; resolve can create more broker connections. Thus merely
closing the current transport map is neither a consumer completion barrier nor a
barrier against later reconnect. These are source-level findings, not a live-broker
shutdown reproduction.

The next ownership change must account for all those entry points together: an async
client shutdown operation must seal factories and reconnect, cancel/join all owned
consumer maintenance, await group/fetch cleanup, and only then release transports.
Application stop must await it using its existing deadline/cancellation contract and
retain ownership when cleanup fails. Lifecycle restart must create a fresh runtime
generation, not reopen a stopped generation still referenced by old handles. Child
registration must be transactional before maintenance dispatch, use weak inventory
references to avoid the existing consumer-to-runtime ownership creating a cycle,
and expose maintenance failures to the supervisor independently of telemetry enablement.
Required regression evidence includes held consumer handles across stop/restart,
coordinator I/O in progress, no post-stop reconnect, startup registration failure and
disabled instrumentation allocation parity. No client shutdown API was added in this
audit; the current stop implementation remains incomplete.

### Kafka stopped runtime generation gates

client_facade::close now seals its runtime before transport callbacks execute.
Factories, metadata refresh (including after mutex acquisition and awaited requests),
runtime connect, broker lookup/resolve and coordinator creation reject that generation.
An explicit client_facade::connect creates a new runtime with copied configuration
and weak observer registrations; old handles retain the old shared runtime instead
of being rebound. Public connect/refresh coroutines pin runtime ownership across
suspension rather than awaiting through replaceable impl storage. The local fixture
checks rejected post-close factories/refresh and separate metadata/API-version state
on a cancelled explicit restart. Windows integration passed in 1.06 s and Arch ASAN
in 1.51 s. Successful broker restart with old handles and mid-exchange closure still
need direct tests. This is a prerequisite, not completed async shutdown: consumers
can still own coordinator connections and maintenance tasks, and those must be
cancelled and joined before Application stop can report successful cleanup.

### Client async consumer shutdown is connected to Application stop

client_facade now registers each consumer's weak backend and independently shared
maintenance task_group before dispatch. async_close seals factories, requests all
maintenance cancellations, joins every registration (including expired backends),
awaits surviving backend cleanup and closes transports after successful cleanup.
Failure retains registrations for retry. Backend public operations reject client-
initiated closure; cleanup uses an internal commit path and serializes against
explicit consumer close. Successful cleanup invalidates the independently owned
coordinator connection. Runtime connect/metadata reject the closing generation.
kafka_service::stop awaits this operation under its lifecycle deadline; empty
registrations retain synchronous transport cleanup, including already-cancelled
startup-failure contexts. Initial tests exposed five regressions in those empty
cleanup contexts before that fast path was restored.

The local protocol fixture now starts/stops an actual kafka_service, keeps one
consumer handle alive and releases another during maintenance timer sleep. Stop
must join within 150 ms, clear registrations and reject polling on the retained
handle; repeated async_close succeeds. Windows integration passed in 1.06 s and
Arch ASAN in 1.09 s. Formatting and generated documentation checks pass. Windows
required one incremental retry after transient LNK1104, with no artifact deletion.

Remaining limitations are explicit: registration entries accumulate until shutdown;
already-running user operations are not tracked/cancelled by this registry; mutex
waits cannot be cancelled/abandoned; maintenance errors are surfaced at shutdown,
not yet bound to Application recovery/health; real coordinator I/O, successful
stop/restart generations and registration allocation failures need direct tests.
Raw synchronous close requests cancellation but does not join. These changes do
not establish all-protocol performance parity or complete the overall goal.

### Successful consumer registrations retire during service operation

task_group::completion_result provides a nonallocating completion/error snapshot;
it does not seal future registration. Kafka uses it only for its single-dispatch
maintenance groups. Consumer creation and requires_async_close retire registrations
whose maintenance succeeded and whose backend is expired or successfully cleaned.
Running/failed groups remain, and closing disables retirement so async shutdown's
inventory stays stable across suspension. No periodic task or disk write is added.
Tests cover pending/success/error snapshots, preserved original task error and
retiring a successfully closed consumer while its handle remains alive. Windows
integration passed in 1.06 s and Arch ASAN in 1.09 s; formatting and AGENTS checks
pass. This prevents successful historical registrations accumulating until shutdown;
failed registrations intentionally remain and still require recovery policy work.
High-churn memory/throughput measurements and allocation-failure registration tests
remain necessary; functional snapshot tests do not replace those gates.

### Successful Kafka service restart keeps previous consumers sealed

The loopback fixture now serves two independent connections, each completing
ApiVersions and Metadata exchanges and then observing transport closure. After
the first service stop, it retains an old consumer across an actual successful
service restart. A new consumer accepts an empty assignment while the old one
rejects assign/poll. Metadata objects differ across generations, API versions are
renegotiated, and the second service stop clears maintenance registrations. This
extends the earlier cancelled-restart check; it is still not a real-broker test.
Windows integration passed in 1.06 s and Arch ASAN in 1.09 s. Rebuilt disabled-
overhead regressions against the current core passed on Windows (0.63 s) and
Arch ASAN (3.22 s). These establish the covered allocation/result invariants, not
all-protocol throughput equivalence or cancellation of outstanding user I/O.

### Terminal Kafka maintenance errors participate in health reporting

client_facade::background_error reads the first failed maintenance completion
without allocation or consumption. kafka_service::probe checks it before and after
the metadata await and reports down with the original std::error_code and a fixed,
non-sensitive diagnostic. Metadata success no longer takes precedence over a
terminal maintenance failure. The existing lifecycle reconciliation schedules
budgeted recovery from down, but kafka_service::start still returns immediately
when started_: restarting a failed maintenance group remains to be implemented.
Protocol errors still retried inside the loop are not terminal completion errors.
Windows/Arch integration passed (1.06 s / 1.11 s), including healthy/background-
cancelled queries; the independent task-group snapshot test covers original error
retention. Fault injection through an actual consumer and an assertion that probe
reports down remains missing, so these tests are not full failure-path evidence.

### Failed maintenance restart entry is wired to lifecycle recovery

restart_failed_maintenance replaces only completed-failed groups on open consumer
backends. Running/successful groups are unchanged; closing runtimes are rejected.
Replacement registration precedes dispatch, and allocation exceptions restore the
previous failed registration. kafka_service::start uses this entry when already
started, so the existing lifecycle supervisor owns retry pacing and its recovery
budget instead of adding another retry loop. Closed/expired failed backends retain
their failure rather than being resurrected. Windows/Arch integration regressions
passed (1.06 s / 1.10 s), covering no-op recovery of running maintenance and rejection
after shutdown. Formatting and generated documentation are current. This does not
yet prove actual failed-consumer restart: inject a failure during maintenance task
execution, observe probe down, exercise recovery and allocation failures, then verify
health confirmation and final shutdown. These direct failure-path tests remain open.

### Actual consumer maintenance allocation failure and recovery are exercised

kafka_runtime_lifecycle_cases.inc now shares the local protocol lifecycle fixture
between integration tests and the existing allocation-fault executable. Only the
latter defines the fault test macro; production code gains no test hook. After
creating a consumer, its queued maintenance invocation encounters a one-shot
allocation failure. The error is contained, background_error becomes nonzero and
service.probe returns down with the same error. A failed replacement allocation
returns not_enough_memory and retains the original failure. service.start then
restarts maintenance; probe performs a fresh Metadata exchange and returns up.
Subsequent consumer/service cleanup and successful generation restart still finish.
Windows integration/fault targets passed (1.07 s / 0.67 s); Arch ASAN passed
(1.09 s / 3.21 s). Existing disabled allocation tests also remain green. Formatting
and generated documentation are synchronized. This verifies actual facade consumer
failure, not just a task-group fake. It invokes recovery directly through service
start, so health hysteresis, automatic supervisor backoff/budget exhaustion and a
real broker interruption are still separate outstanding combined-test requirements.

### Task failure no longer traps successfully cleaned resources in shutdown

Injecting another maintenance allocation failure immediately before service stop
reproduced three Windows assertions: registrations remained and repeated stop still
failed despite completed protocol cleanup. Backend cleanup_complete is now recorded
after resource cleanup independently of the maintenance result. Client async_close
retains inventory only when backend cleanup is actually incomplete; otherwise it
closes transports, clears registrations and still returns the first task failure.
A following stop succeeds idempotently. The local fixture's fault variant verifies
first-stop failure, cleared inventory, subsequent success and peer disconnection.
Windows integration/fault targets passed (1.06 s / 0.67 s); Arch ASAN passed
(1.10 s / 3.24 s). Formatting and generated skill documentation are current. Real
group-cleanup failure retention, arbitrary in-flight user I/O and the full automatic
recovery/readiness state machine still need their respective integration evidence.

### Lifecycle-supervised consumer recovery waits for health confirmation

The shared runtime fixture now registers an actual kafka_service in service_registry
and health_registry and starts it through service_lifecycle. Its allocation-fault
variant commits the real failed probe, invokes reconcile_health and joins the real
task_supervisor recovery operation. Recovery performs a Metadata exchange but leaves
readiness false and consecutive successes zero. Two health.refresh calls perform
further Metadata exchanges: the first remains not-ready, the second confirms ready.
Final lifecycle.stop releases all started service ownership. Windows integration/
fault targets passed (1.08 s / 0.67 s); Arch ASAN passed (1.10 s / 3.23 s). An explicit
recovery_policy module import was added after both compilers rejected transitive-only
visibility. Formatting and generated documentation checks pass. The test drives
health reconciliation directly, not the host's periodic scheduler, and does not yet
exercise repeated supervisor backoff, recovery budget exhaustion or a real broker.

### Kafka recovery retries after both metadata candidates fail

The fault fixture now returns malformed Metadata replies from both the bootstrap
connection and the discovered broker candidate on the first supervised recovery
attempt. It then serves the next attempt normally. A deterministic 10 ms initial
delay (zero jitter, 20 ms cap, two-second budget) is configured for this test; the
peer checks at least 8 ms between the final rejection and the next attempt, and
readiness remains false throughout. Recovery then requires two successful health
confirmations as before. This distinguishes lifecycle backoff from merely trying
another broker inside one metadata refresh. Windows integration/fault targets pass
(1.08 s / 0.68 s), as do Arch ASAN targets (1.10 s / 3.24 s); formatting passes.
This covers one supervised retry, not exponential growth to the cap, budget
exhaustion or the host's periodic health scheduler against an actual Kafka broker.

### Mandatory CI gates cannot disappear silently

The Linux messaging workflow now selects the three actual CTest registrations
(`python_amqp091_interoperability`, `python_amqp10_interoperability`, and
`python_kafka_interoperability`) instead of the obsolete combined name. Container
mode is explicit and `CNETMOD_MESSAGING_REQUIRED=1` turns missing infrastructure,
Python dependencies, or driver preflight failures into failures rather than skips.
Local optional execution retains exit code 77. Security scenarios that require
separately configured endpoints can still skip; this is not full security coverage.

Windows, Linux and macOS workflows run each of seven Application/OTEL CTest targets
individually with `--no-tests=error`, so another target cannot mask a missing target.
Windows explicitly preserves each native command's nonzero exit status. The gate
includes instrumentation, metric aggregation, HTTP observation and disabled-path
allocation checks as well as lifecycle and OTLP tests.

Local verification: three dependency-free runner regression tests pass, all seven
targets from the existing Windows Release build pass (8.42 seconds total), Python
and workflow YAML syntax checks pass, and generated AGENTS.md is current. This
verification did not rebuild C++ binaries, execute real brokers, or run hosted CI;
it does not prove cross-platform builds or zero overhead for every protocol.

### Broker preflight resource cleanup and current Arch build

Both Docker preflight paths now close a created client even when ping raises.
Fixture diagnostics no longer interpolate arbitrary Docker exception text, and
direct fixture execution respects required mode when an external broker endpoint
is missing. Five dependency-free runner tests pass, including failed client
construction and cleanup after successful/failed ping. Actual container teardown
and fault-injection thread termination remain separate verification concerns.

The seven Application/OTEL targets were built incrementally from the current
source in the Arch ASAN configuration and each passed individually: application
3.28 s, integrations 1.10 s, OTLP 0.59 s, instrumentation 0.06 s, aggregation
0.06 s, HTTP observation 1.23 s, and disabled-path checks 3.26 s. This configuration
has SSL disabled and does not establish full protocol/platform coverage.

### PostgreSQL and MongoDB startup admission

Both managed services reject an already-cancelled token or expired deadline before
warm-up or maintenance registration, with cancellation taking precedence when both
apply. A regression test uses zero minimum pool size and no running event loop to
check the original error code, an empty pool, down health and safe stop. Windows
Release has both protocols enabled; the core and integration target rebuilt and
the integration suite passed (1.07 s). Formatting checks pass. Arch's existing ASAN
configuration disables both protocols, so its successful build is not evidence for
these branches. Mid-operation cancellation, real PostgreSQL health queries and
MongoDB/ PostgreSQL end-to-end recovery remain incomplete; startup admission alone
does not provide those guarantees. The change adds no per-query observation work.

### PostgreSQL authentication failure retains its transport error

Authentication cleanup previously called disconnect with a default error after
read/write had already recorded the transport failure, clearing last_error(). It
now passes the stored error through cleanup. A loopback peer receives startup and
closes before authentication; the test checks failed connect, closed client,
nonzero transport error, and cleared backend identity. Both owned coroutine tasks
are awaited to completion; CTest bounds the fixture to 30 seconds. Windows Release
core and integration builds pass and the suite passes in 1.06 seconds. One initial
library-open linker failure disappeared on incremental retry. The fixture does not
prove mid-operation cancellation, TLS interruption or a specific OS error mapping.

### PostgreSQL overlapping reconnect preserves the active transport

Reconnect previously disconnected before connect acquired the operation guard.
Removing that premature disconnect leaves teardown under the existing ownership
check. A loopback fixture withholds authentication, invokes reconnect from the
owning executor, checks rejection, then sends AuthenticationOk/ReadyForQuery and
verifies the original connection succeeds. Windows core and integration builds
pass after an incremental retry of the library-open linker failure; the integration
suite passes in 1.07 seconds. No query-path lock or observation work is added.
Cross-thread calls and mid-operation cancellation are not covered by this fixture.

### PostgreSQL cancellable query transport

Added query(sql, cancel_token&) with a token binding installed only after acquiring
operation ownership and retired before releasing it. Plain and TLS read/write
calls use their cancellable overloads while bound. Pre-cancelled queries do not
send SQL or close the session; cancellation during I/O follows transport failure
cleanup and discards it. A competing query cannot overwrite the token binding.
The existing query entry retains its direct coroutine, but shared I/O helpers now
have a token-pointer branch; zero performance regression remains unproven.

A Windows loopback fixture authenticates, rejects a pre-cancelled query, receives
the next query, waits five milliseconds, rejects an overlapping query, and cancels
the original pending response. It verifies closed session, nonzero error, peer EOF,
and completion within one second. Core/test builds and the integration suite pass
(1.07 s). TLS overloads compile but real TLS cancellation, large writes, reconnect
after cancellation, connect/auth cancellation and Application probe wiring still
need verification or implementation. CTest remains the outer hang bound.

### PostgreSQL reconnect after a cancelled query

The cancellation fixture now accepts a second TCP connection after observing EOF
on the discarded session, completes a fresh handshake and responds to a normal
query. The original token remains cancelled throughout. Successful reconnect and
query, cleared last_error and the returned command tag verify that the token
binding did not leak into the replacement session. Windows integration rebuild
and tests pass (1.07 s). This is a controlled plaintext peer, not a real database
or a full Application recovery test; TCP fragmentation coverage is still limited.

### Compile-time PostgreSQL cancellation paths

Superseding the temporary shared token binding above, private read/write/message
and result collection templates now receive either std::nullptr_t or cancel_token*.
Ordinary callers retain direct task entry points; if constexpr selects the original
transport overload without a runtime cancellation-pointer check. Cancellable query
passes its own token explicitly, removing the client token member and scope binding.
Template definitions remain in the implementation translation unit because only
that unit instantiates these private helpers. This is structural evidence, not a
latency/allocation benchmark proving identical generated code or total overhead.

Windows core and integration builds pass; cancellation, reconnect and legacy-query
fixtures pass in the integration suite (1.07 s). The TLS branch is compiled, not
tested against a TLS peer. PostgreSQL is still disabled in the existing Arch ASAN
configuration, so it supplies no evidence for these new template instantiations.

### PostgreSQL cancellable connection and authentication

Added connect(options, token), selecting a private connect_impl specialization.
The same token reaches Happy Eyeballs, retry waits, SSL negotiation, TLS handshake,
startup and all authentication exchanges. The original entry returns the null-token
specialization directly, without another coroutine frame or shared cancellation
state. Authentication is templated in the implementation translation unit.

A loopback authentication peer withholds its reply, cancels after five milliseconds
and observes EOF. The client reports failure with retained error and cleared backend
identity within one second. The original disconnected-authentication case still
passes. Windows builds and integration tests pass (1.10 s); one library-open linker
failure required incremental retry. Real DNS/TLS and retry-wait cancellation are
not established by this plaintext fixture. Pool and Application wiring remain open.

### PostgreSQL acquire propagates cancellation into authentication

Pool acquire(token) now rejects already-cancelled admission before reserving or
leasing a slot and passes the caller token to a newly created connection. Failure
returns last_error when available instead of replacing it with connection_refused.
The authentication fixture now also exercises a one-slot pool: cancelled admission
leaves size zero, cancelled authentication releases checked-out ownership and leaves
no waiter, and close completes with zero size within one second. Windows builds and
integration tests pass (1.10 s), after retrying the known library-open linker error.

This does not establish concurrent waiter cancellation correctness, exception-safe
slot reservation or bounded close with outstanding leases. The existing discarded
slot reconnect uses detached spawn and is not yet fully supervised/cancellable;
warm-up and Application probe wiring remain incomplete as well.

### PostgreSQL Application probe performs a cancellable query

The managed service no longer equates a nonempty pool with availability. Probe
uses one deadline across acquire(token) and SELECT 1, returns fixed diagnostic
text with the error code, and discards the leased connection on failure. No server
SQL diagnostics enter health output. A local fixture starts the service, withholds
the first query reply until the 50 ms deadline, observes EOF, then accepts a new
connection and completes the next probe. The reports are down/timed_out then up,
checked-out count returns to zero and service stop completes. Windows core/test
builds and integration tests pass (1.16 s).

This is a controlled plaintext component test, not a real PostgreSQL or full Host
readiness/recovery-budget test. Startup warm-up, outstanding lease shutdown and
detached background reconnect still require lifecycle work; no performance parity
claim follows from the probe test.

### Cancellable PostgreSQL warm-up is wired into Application startup

Added warm_up(token) returning expected<void, error_code>. It retains acquired
leases until minimum capacity is reached, then returns them to the pool; partial
success remains pool-owned for caller rollback. Application start uses this overload
under its existing deadline and preserves the returned code. The no-argument
warm-up API remains unchanged. A one-slot fixture cancels during authentication,
checks released reservation and no remaining waiter, then closes the pool. Existing
Application start/probe/recovery tests also pass. Windows rebuild and integration
suite pass (1.18 s), following an incremental retry of the library-open error.

Partial multi-connection rollback, concurrent warm-up/close, zero-minimum closed-pool
admission and failure-injected reservation cleanup are not proved by this fixture.
Background reconnect ownership and shutdown with borrowed leases remain open.

### PostgreSQL background reconnect has a joinable owner

Discarded-slot reconnect now runs inside a lazily allocated pool-owned task_group,
with one cancellation token per child. Close seals admission via closing_, cancels
the group and settles it before retiring slots. A completed group can be replaced
on later reconnect to avoid accumulating completed tokens indefinitely. Normal
acquire/release without discarded slots does not allocate a group.

The new one-slot fixture queues a second acquirer, discards the first lease, stalls
background reconnect during authentication, then closes the pool. It observes EOF,
rejected waiter, zero pool/waiter counts and close completion within one second.
Windows core/test builds and integration suite pass (1.22 s), after an incremental
retry of the library-open linker error. Caller must still await close before pool
destruction. Release/cancel mutex-contention helpers still use detached spawn;
background exception propagation, allocation-failure slot state, concurrent close
and outstanding lease bounds remain open, so this is not full pool supervision.

### PostgreSQL close callers share serialized cleanup

A separate coroutine mutex now covers the whole pool close operation, including
settling reconnect and terminating retired connections. It is not taken by query
or lease operations. The stalled-background-reconnect fixture launches two close
tasks, checks both return with zero pool/waiter counts, then closes again. Windows
builds and the integration suite pass (1.24 s). An initial assertion requiring caller
continuations in FIFO order was removed: mutex release can synchronously resume a
waiting coroutine before the releasing caller's continuation. Both caller identities
are instead checked once each with cleanup invariants, without that false ordering
requirement. This does not bound close with unreturned leases or prove TLS shutdown.

### PostgreSQL allocation failures do not strand acquire reservations

Allocation and connection exceptions after slot reservation now use the same
release path as failed connects, mapping bad_alloc to not_enough_memory. An eight-
position allocation sweep verifies no checked-out reservation or waiter remains,
a subsequent acquire can complete, and close leaves size zero. Invalid connection
configuration prevents network I/O in this fixture; it is not a network benchmark.

The sweep initially terminated the process at allocation index three: client was
declared noexcept despite owning standard containers that allocate during member
construction. Removing the invalid noexcept declaration lets the pool catch the
failure and release its slot. Restored full sweep and Windows integration/disabled-
path targets pass (1.23 s / 0.68 s). Before-reservation failures may still propagate,
but must leave no reservation. Background reconnect exceptions and contended
release allocation failures remain distinct open cases.

### PostgreSQL paths now build and run under Arch ASAN

The existing /root/cnetmod-otel-asan build was reconfigured with PostgreSQL enabled;
it uses system ICU uc/i18n, Clang 22.1.8, libc++, epoll, the system allocator and
-fsanitize=address -fno-omit-frame-pointer. Current protocol templates, pool and
Application sources compiled successfully. Integration and disabled/allocation
targets passed (1.20 s / 3.25 s), now including PostgreSQL branches rather than
compiling them out. This supersedes the earlier absence of Arch evidence for those
branches. SSL remains disabled and MongoDB is not added by this configuration
change. Existing unused-parameter/function warnings remain; this is not a clean
all-platform or all-protocol build, nor a performance benchmark or real DB run.

### Partial PostgreSQL startup has observable rollback ownership

A two-connection service fixture authenticates the first connection and withholds
authentication on the second. The shared 100 ms startup deadline expires, start
returns timed_out, checked-out count becomes zero and the first connection remains
idle and pool-owned. Probe reports down because start did not complete. Explicit
service stop empties the pool; the peer observes the first connection's Terminate
message and EOF and the second connection's cancellation EOF. Windows and Arch
ASAN integration rebuild/tests pass (1.34 s / 1.26 s). This establishes component
rollback ownership under a controlled plaintext failure, not automatic Host rollback
or bounded shutdown against unresponsive TLS transports or outstanding leases.

### PostgreSQL partial startup now cleans its own attempt

The cancellable pool warmup now closes the connections held by a failed attempt,
including reused idle connections, before returning its original error. Other
borrowers are not closed. Slots remain discarded and retryable; `size()` counts
slots, not live transports. An already closed pool rejects cancellable warmup
even with a zero minimum. The ordinary no-token warmup and query paths are unchanged.

The partial-startup regression now uses `service_lifecycle`, not a manual service
stop: one connection authenticates, the second stalls until the startup deadline,
both TCP peers observe closure, no successful service ownership remains, and the
failure retains its service identity, startup phase and timeout error. A second
start on the same service establishes both connections and normal lifecycle stop
closes them. This supersedes the earlier partial-warmup ownership limitation.

Windows Release passed the integration target (1.33s) and disabled-overhead target
(0.68s). Arch Clang 22 with AddressSanitizer passed the same targets (1.28s and
3.24s). These are loopback plaintext fixtures, not real PostgreSQL authentication
or TLS validation. They do not prove throughput parity, arbitrary contention
cleanup, or completion of the overall OTEL goal. The 114 SSH attempt still timed
out after 20 seconds; no remote database changes were made in this continuation.

### Cancellable warmup allocation admission is covered

Lease-vector reservation now belongs to the warmup error-conversion boundary.
A twelve-position allocation-failure sweep verifies error results, zero borrowed
leases and waiters, retry admission, and eventual pool closure. The test creates
the outer task before injecting failure, so caller-side coroutine-frame allocation
is outside its coverage. It rejects invalid connection options before network I/O;
allocation failure after a successful network connection is not covered here.
A separate regression verifies zero-minimum warmup succeeds before close and
returns `operation_canceled` afterwards instead of reporting startup success.

Windows Release integration/disabled-overhead targets passed in 1.34s/0.68s;
Arch Clang ASAN passed in 1.27s/3.27s. These checks do not establish throughput
equivalence or complete the remaining concurrency and real-dependency gates.

### Background PostgreSQL connection exceptions restore slot state

The discarded-slot reconnect body now catches client construction and awaited
connection exceptions, restores `connecting`/`in_use` under the pool state lock,
and returns the mapped exception error to its task group. Ordinary connection
error results retain their existing behavior. This does not cover failures before
the reconnect coroutine begins executing, notification dispatch failure, or
propagation from the private task group into Application health.

Windows Release integration/disabled-overhead regressions passed (1.34s/0.66s),
as did Arch Clang ASAN (1.28s/3.26s). The existing reconnect shutdown test exercises
cancellation and concurrent close, not targeted allocation failure inside this
background body; direct fault-injection evidence for this new branch is pending.

### PostgreSQL waiter admission no longer terminates on queue allocation

The queue awaiter's `await_suspend` is no longer incorrectly `noexcept` around
deque insertion. It publishes `queued` only after insertion succeeds; allocation
failure unwinds the still-owned state lock and is converted by `acquire` into
`not_enough_memory`. No cancellation callback is installed on that failure path.
This is separate from reconnect factory/frame allocation failure, which remains
open. A saturated-queue allocation fault-injection regression is still required;
existing general pool allocation tests do not exercise deque growth here.

### Saturated PostgreSQL waiter allocation regression

`postgresql_waiter_growth_failure_preserves_existing_waiters` now covers the
previously missing queue-growth fault. A loopback peer authenticates the sole
connection; the test retains its lease and queues at least four other callers.
Subsequent queue admission is run with the next allocation forced to fail, until
deque growth occurs (bounded at 1024 attempts, with an assertion that injection
actually happened). Coroutine frames and token storage are allocated outside the
fault window. The failed caller returns `not_enough_memory`, preserves the queue
count, and all previous waiters cancel and complete. Releasing and reacquiring
the original connection succeeds before final close.

Windows Release passed the complete disabled-overhead target in 0.68s; Arch
Clang ASAN passed in 3.28s. This supersedes the missing saturated-queue regression
noted above, but does not cover cross-thread cancellation or reconnect task
factory/frame allocation failure. No production instrumentation was added for
the test, and these results are not a throughput baseline.

### Refreshed seven-target regression gate after PostgreSQL changes

All seven targets were rebuilt and individually run with `--no-tests=error`,
anchored names, and a 30-second timeout. Both Windows Release and Arch Clang 22
ASAN passed:

| Target | Windows seconds | Arch ASAN seconds |
| --- | ---: | ---: |
| test_application | 4.24 | 3.28 |
| test_application_integrations | 1.34 | 1.31 |
| test_otlp_exporter | 1.19 | 0.60 |
| test_instrumentation | 0.01 | 0.07 |
| test_metric_aggregation | 0.01 | 0.07 |
| test_http_observability | 1.21 | 1.19 |
| test_http_disabled_overhead | 0.68 | 3.24 |

This refresh includes PostgreSQL-enabled builds and the queue growth fault test.
It does not certify a macOS build, real dependency integration, every feature
combination, historical throughput parity, or complete cleanup ownership. The
performance analyzer/runner has nine passing Python tests; its latest independent
Windows measurement remains inconclusive at zero allowed regression. No commit
or remote CI execution was performed.

### Protocol-free benchmark target isolation

Enabling benchmarks in the protocol-free configuration exposed an unconditional
HTTP dependency: `crosslang_cnetmod_server` was still selected with HTTP disabled.
The benchmark target list now excludes it alongside HTTP/gRPC targets in that
configuration. Existing conditional selection also excludes the observation and
raw baseline targets when HTTP is disabled.

The existing Windows `cmake-build-protocol-free-verify` cache was configured with
`CNETMOD_BUILD_BENCH=ON`; no `CNETMOD_ENABLE_*` options were ON. The core library
and all four selected benchmarks (`bench_coroutine`, `bench_channel`, `bench_sync`,
`bench_io`) built in Release. The generated solution contains none of the HTTP,
gRPC, WebSocket, MQTT, Raft or cross-language HTTP benchmark targets. This proves
that local build combination, not all-platform or runtime benchmark performance.

### PostgreSQL background completion reaches health probes

The pool exposes an allocation-free owning-executor `background_error()` snapshot
of its completed reconnect group. Application PostgreSQL probes return down with
a fixed message and the recorded error before issuing SQL when this snapshot is
failed. Running or absent groups still return a clear snapshot, not a connectivity
guarantee. Direct injection of the completed-group failure-to-health path is still
pending; current integration regression only covers the existing normal and
cancellation cases plus the initially clear snapshot. Dispatch failures before
group ownership and the recovery/clearing policy remain incomplete.

### PostgreSQL recovery re-enters warmup after completed background failure

An already started service no longer bypasses warmup when `background_error()`
reports failure. Under the pool state lock, cancellable warmup retires only a
completed failed task group and resets discarded, non-borrowed slots for retry,
including slots whose reconnect frame never began. Waiting borrowers can be
redispatched. Running groups and borrowed slots are not reset. Query probing and
the lifecycle's health confirmation still follow this attempt; scheduler reset
alone is not an up transition. Direct fault injection of this recovery path is
still required, and failure before group creation is not reported by the snapshot.

### Direct reconnect allocation failure, health reporting and recovery regression

`postgresql_reconnect_frame_failure_is_reported_and_warmup_recovers` now starts
an Application PostgreSQL service with telemetry disabled, leases its sole
connection, and queues another borrower. After discard schedules a reconnect,
the next allocation on the event loop is forced to fail before the retry starts.
The completed group reports failure; the service probe returns down with the same
error. Calling service start again re-enters warmup, the original waiting borrower
receives an authenticated replacement, the background error clears, and service
stop closes the pool. No production fault hook or normal-path allocation was added.

The full disabled-overhead target passed on Windows Release (0.71s) and Arch
Clang ASAN (3.26s). This supplies the previously missing direct failure-to-health
and retry evidence for this specific queued allocation point. It does not prove
all allocation points, cross-thread races, recovery budget exhaustion, real SQL
health confirmation, or complete Application host lifecycle recovery.

### Reconnect dispatch failure before task-group creation

The pool now records dispatch failures separately from task-group completion,
including allocation failure before a group exists. The allocation-free error
snapshot gives dispatch errors precedence. Cancellable warmup clears this state
only when the group is absent or completed, then retries discarded, unborrowed
slots. An active group is not destroyed by this recovery step.

`postgresql_reconnect_dispatch_failure_is_reported_and_warmup_recovers` forces
the first allocation during discard to fail, verifies `not_enough_memory` and
Application probe down, and verifies recovery delivers a replacement connection
to the original waiter before service shutdown. The queued-frame failure case
remains covered separately. The full disabled-overhead target passed on Windows
Release (79 cases, 0.73s) and Arch Clang ASAN (3.30s). These tests use a local
plaintext authentication peer, not a real PostgreSQL server. They supersede the
earlier dispatch-failure evidence gap, but do not prove complete host recovery,
cross-thread safety, bounded shutdown with outstanding leases, or the absence
of performance regression against a pre-instrumentation baseline.

### Ordinary PostgreSQL reconnect errors reach health and recovery

Previously only exceptions failed the reconnect task; a normal failed connect
result left a discarded slot but reported task success. Reconnect now preserves
the client's transport error, falling back to `io_error` when none exists, so
the existing health snapshot and recovery path can observe the failure.

`postgresql_reconnect_transport_failure_is_reported_and_warmup_recovers` uses
a peer that authenticates the initial connection, disconnects during the next
authentication, and accepts the recovery connection. It verifies background
failure, probe down with the same error, delivery to the original waiter after
restart, cleared error, and shutdown. The full disabled-overhead target passed
on Windows Release (0.75s) and Arch Clang ASAN (3.37s). This is plaintext transport
fault evidence, not a real-server or full-host recovery-budget test.

### Cross-build HTTP measurement orchestration

The process runner accepts a separate raw-only baseline executable and alternates
baseline/candidate execution order. It retains both hashes and full round rows,
requires matching per-round request counts, and analyzes baseline raw versus
candidate disabled throughput at the process-pair level. Runner tests verify
ordering, correct ratio selection, baseline shape/duplicate rejection, same-path
rejection and cross-build count mismatch, including equal-total unequal-round
counts. Historical compilation and configuration parity are still outstanding;
simulated process-output tests do not establish performance equivalence.

### First Windows historical HTTP build and measured comparison

Detached revision `85fbba254fd2b5c69e17c53676d3e9a971bd0881` now builds the shared
raw-only benchmark without modifications to tracked framework sources. A current
counterpart builds with matching HTTP/ORM-only, TLS-off, system-allocator MSVC
Release settings. Details and source hashes are in `testing/bench/http-observation.md`.
Eight actual alternating process pairs are retained in
`testing/bench/http-observation-windows-historical-20260913.json`.
Throughput ratio 1.06662 with bootstrap interval [0.96802, 1.20008] is inconclusive,
not a passing zero-regression gate. The old ORM-off configuration failed to build;
the successful comparison explicitly enables ORM on both sides. The historical
commit is pre-wrapper, not proven pre-instrumentation for every subsystem.

### Equal-workload historical HTTP sample

Added the compile-time disabled-only benchmark target, preserving the original
paired target. Runner validation accepts eight disabled-only rounds only when an
independent raw baseline is supplied. Fourteen Python tool tests pass, including
this rule. Both binaries rebuilt successfully, then completed 32 preselected
alternating process pairs with 100 warmup plus 8000 timed requests per process.
`http-observation-windows-historical-equal-20260913.json` records ratio 1.00340 and
interval [0.97777, 1.03625]: still inconclusive. This eliminates unequal per-process
request counts, but CPU affinity and broader workload validation remain open.

### Client event-loop affinity evidence

The benchmark reuses `set_current_thread_affinity` on Windows/Linux before warmup;
invalid input or binding failure rejects measurement. The runner requires an
exact acknowledgement from every process and records the limited binding scope.
Sixteen tool tests pass, and the real Windows binary rejects invalid and
unavailable CPU values. Rebuilt historical/current executables completed 32 pairs
bound to CPU 2, retained in `http-observation-windows-historical-cpu2-20260913.json`.
Ratio 1.02264, interval [0.99564, 1.05618] remains inconclusive. This improves client
scheduling control but does not isolate the peer, prove physical-core placement,
or establish zero performance regression. No production affinity API changed.

### Successful HTTP request allocation parity

`http_disabled_success_cases.inc` adds a real loopback success-path comparison to
the disabled-overhead suite. A separate peer thread avoids counting server-side
allocations in the client thread's allocation window. After warmup, both regular
and cancel-token requests return status 200 and the expected body with identical
allocation calls/bytes for raw and empty-sink decorated clients. A nonempty,
heap-backed parent context is supplied; received requests contain neither
traceparent nor tracestate. The peer and client tasks are owned and checked after
completion. Windows Release (0.76s) and Arch ASAN (3.29s) pass the full target.

This complements task-creation and invalid-URL error-path allocation tests. It
does not establish historical allocation parity, CPU/latency equivalence, TLS,
streaming, cancellation during I/O, or allocations on other client threads.

### Client timeout configuration rejects coercion before service creation

Shared integer-property validation now guards HTTP client connection/request
timeouts and OpenAI timeout seconds before JSON narrowing and service creation.
Missing values retain defaults; booleans, fractions, strings, null, nonpositive
values and oversized integers are rejected. The build-level regression exercises
these cases and accepts a positive boundary value. Application integration tests
pass on Windows Release (1.35s) and Arch ASAN (1.27s), after fixing an explicit
JSON module import in the test. This is not a complete numeric-schema audit or
proof that all deadline arithmetic is overflow-safe.

### Database and messaging connector port narrowing guards

All eight built-in connectors with standalone port properties (Redis, MySQL,
PostgreSQL, MongoDB, Kafka, MQTT, AMQP 0-9-1 and AMQP 1.0) now use integer range
validation before conversion. The build-level matrix rejects negative/zero,
65536/65537, fractions, booleans, null, strings and uint64-max, and accepts 1 and
65535. Defaults remain unchanged. Windows integration tests pass (1.37s), covering
all eight compiled integrations. This change affects configuration only; pool
sizes, endpoint strings and other numeric fields still require separate audits.

### Shared pool capacity configuration validation

Redis, MySQL, PostgreSQL and MongoDB auto-configuration now validate pool bounds
before conversion and service construction. Minimum permits zero; maximum must
be positive; omitted values use each protocol's existing defaults and the resolved
minimum must not exceed maximum. Invalid JSON scalar types and out-of-range
values are rejected rather than narrowed or normalized. The matrix tests invalid
scalars, maximum zero, inverted bounds, minimum above the default maximum, valid
zero minimum, equal bounds and unchanged defaults. Windows integration tests pass
(1.40s); Arch ASAN passes its compiled subset (1.30s, MongoDB disabled). These are
configuration checks, not resource-capacity or real database recovery evidence.

### Finite sampling and framework duration bounds

Validation now rejects nonfinite sampling ratios (NaN previously bypassed range
comparisons) and invalid explicitly configured HTTP request timeouts. Shared
positive-duration checks and HTTP client timeout parsing use the existing recovery
policy's half-steady-clock-range horizon. Tests cover builder-provided NaN and
infinities, finite sampling boundaries, absent/positive HTTP timeout, and negative,
zero and maximum-duration rejection. Windows Application and integration targets
pass (4.24s/1.40s), and Arch ASAN integration passes (1.30s). The public low-level
reload helper's direct-input validation and transactional exception safety remain
to be audited; host file reload already passes through configuration loading.

### Transactional configuration snapshot preparation

The low-level reload API now returns expected, validates direct candidates and
prepares a private snapshot/change list before committing via no-throw move.
Logger level application occurs only after preparation. Host and the existing
caller test handle the new result. A 64-position allocation sweep verifies
failed preparation preserves sampling, health and recovery values, successful
preparation applies reloadable fields, and restart-only HTTP port stays unchanged;
invalid NaN candidates are rejected. Windows Application/disabled-overhead tests
pass (4.23s/0.75s), as do Arch ASAN counterparts (3.30s/3.27s). Host post-commit
snapshot copying and propagation to runtime components remain separate potential
failure boundaries; this is not a full cross-component reload transaction.

### Host reload preparation and service identity isolation

Host now prepares service-key storage before configuration commit and extracts
only health/sampling scalar snapshots afterwards, eliminating the full post-commit
configuration copy. Recovery updates target only bindings whose type and instance
remain unchanged. The low-level helper also preserves the old policy when either
identity field changes; regression cases verify restart-required without applying
the replacement's budget to the old service. Windows and Arch ASAN disabled-overhead
targets pass; Arch required an explicit recovery-policy import, now supplied.
Lifecycle override-map allocation and concurrent runtime propagation remain open
transaction boundaries; no full Host reload atomicity claim is made.

### Atomic lifecycle policy batches

Replaced per-key policy updates with an expected-returning batch operation that
validates policies, prepares a copy of the override map under its state lock and
swaps only after the complete batch succeeds. A snapshot query supports direct
verification. A 32-position allocation sweep checks failure preserves an existing
override and does not install a new one; success updates both. Windows and Arch
ASAN disabled-overhead suites pass. Host now checks the batch result, but still
commits configuration before the policy batch, so cross-component atomicity
remains unfinished. This closes policy-table partial updates, not the full reload.

### Host reload allocation audit (2026-09-13)

- Extended the host file-reload fault sweep to check the actual health policy
  and telemetry sampling ratio, as well as the stored configuration snapshot.
- Windows core and Application builds succeeded; `test_application` passed
  in 4.25 seconds before the final redundant JSON initialization removal.
- The new host fault sweep terminates on both Windows and Arch ASAN. GDB
  identified allocation inside the former JSON backend destruction, initially reached
  by erasing the copied `recovery` object. Copying only integration properties
  removes that redundant copy/erase path, but a subsequent run still terminates
  during configuration loading. This is an unresolved failure, not a passing
  allocation-safety gate. The regression remains enabled.
- Host reload now prepares a staged configuration under its configuration lock,
  checks the atomic recovery-policy batch before committing runtime scalars,
  and commits the stored configuration last. The low-level configuration helper
  no longer changes process logging. Independent component readers do not
  provide a single cross-component snapshot.
- No commit or push was performed. Real database tests and historical disabled
  performance parity remain unproven; this audit does not close those gates.

### Temporary configuration document cleanup (2026-09-13)

- Added a private RAII cleanup guard for parsed configuration documents. It
  removes leaves iteratively, so the dependency only destroys empty containers
  and scalar values; no heap traversal stack or recursive calls are needed.
  Auxiliary storage is constant; worst-case traversal work is nodes times depth.
  This code does not run on the request/telemetry fast path.
- The previously terminating 256-position host reload sweep now passes on
  Windows and Arch ASAN. Added a 128-position invalid-root sweep containing
  nested arrays and objects; the fresh Arch suite passes in 3.29 seconds.
- This supersedes the reproduced temporary-document destruction failure above,
  not all possible JSON allocation failures. Parser-internal partial states,
  arbitrary retained integration properties, sustained OOM, and historical
  disabled-performance parity still require further evidence.

### Configuration loading error contract (2026-09-13)

- The public loader now translates propagating allocation exceptions to
  `not_enough_memory`; JSON parsing and field conversion preserve this category
  instead of treating it as malformed input. Other unclassified propagating
  exceptions become `io_error`.
- Added a 64-position default-load sweep that requires resource-error identity
  and successful loads. The invalid-root sweep no longer catches exceptions in
  the test. Its optional path argument is prepared before injection, because
  caller-side argument allocation is outside the loader's error boundary.
- Windows disabled/fault suite passes (0.85 seconds); Windows auto-configuration
  integration suite passes (1.40 seconds), as does the Arch ASAN compiled subset
  (1.29 seconds). These are local suites, not real external-service validation.

### Protocol-free dependency boundary revalidation (2026-09-13)

- Revalidated `cmake-build-protocol-free-verify`: all protocol switches, SSL,
  and ORM are OFF in its cache; its source root is the current checkout.
  The Windows Release `cnetmod_core` target builds successfully. Its generated
  project contains no HTTP, Application Host, or observability adapter source.
- Rebuilt and ran `test_instrumentation`, `test_metric_aggregation`,
  `test_deadline`, `test_task_group`, and `test_task` in that configuration:
  all five pass (0.11 seconds total). This does not cover the other registered
  test targets, some of which have not been built in this configuration.
- Inspected the raw HTTP client interface and implementation: neither imports
  the observability/instrumentation adapter. Empty-sink decorated send delegates
  directly to the raw task, for both cancellation overloads. This establishes
  the source dependency and dispatch boundary, not instruction-level or
  historical throughput equivalence. The decorator still executes sink checks.
- No new performance measurement is asserted here; the archived historical
  comparison remains inconclusive. Other protocols and full platform matrices
  still need their own direct evidence.

### OTLP receive and shutdown evidence refresh (2026-09-13)

- Rebuilt `test_otlp_exporter` against the current core on Windows and Arch
  ASAN; both pass (1.19 and 0.61 seconds). Inspected the 20-mode loopback
  fixture covering response body boundaries, malformed/truncated framing,
  silent peers, retry cancellation, and shutdown accounting.
- The current exporter selects HTTP/1.1 with a 64 KiB response-body budget,
  disables redirects/cookies, and does not retry invalid framing. This is not
  a bound on total transport memory or evidence for HTTP/2/3/TLS coverage.
- Corrected the observability skill's stale receive-limit claim and unsafe
  unconditional stop-after-flush example. The example now uses shutdown and
  stops the context only after successful settlement; delivery success still
  requires checking exporter statistics. Generated AGENTS.md is synchronized.

### Recovery logging failure isolation (2026-09-13)

- Reproduced a supervisor regression with targeted allocation injection: after
  the first business failure, retry-log formatting threw and the guarded task
  transitioned to failed instead of performing its second attempt.
- Retry logging now has a local exception boundary. Diagnostic failure does
  not terminate recovery; the original retry delay, cancellation, and budget
  logic remain unchanged. This catch is on the failure/retry path only.
- The regression requires injection to occur, exactly two attempts, successful
  join, and final stopped state. Windows Application and the full disabled/fault
  suite pass against the fix (4.22 and 0.83 seconds).
- This does not isolate every logging site or establish all runtime policy
  hot-update semantics; those remain separate audit items.

### Host diagnostic isolation follow-through (2026-09-13)

- Added local exception boundaries around Host recovery-exhaustion reporting,
  failed recovery scheduling diagnostics, and the startup listening message.
  Stop is requested before its diagnostic; logging cannot escape that callback,
  end a health loop, or initiate rollback after successful listener startup.
  Existing lifecycle and shutdown errors keep their original propagation paths.
- Rebuilt Windows and Arch ASAN core/Application/fault targets. Arch Application
  passes in 3.47 seconds and the disabled/fault suite in 3.30 seconds.
- These are regression-suite checks, not targeted injection at each of the
  three Host logging sites. Targeted failure reproduction remains established
  for supervisor retry logging only; do not conflate those coverage levels.

### Lifecycle trace outcome classification (2026-09-13)

- Lifecycle start/stop/recovery span completion now uses the existing neutral
  error classifier rather than mapping every nonzero error to generic failure.
  Health probes use the same classifier; down without an error still produces
  an error outcome. Service results, readiness rules, and counters are unchanged.
- Added completion coverage for standard/framework cancellation, timeout,
  ordinary failure, and success. Checks preserve the original error, terminal
  status, failed flag, and exactly-one exporter invocation.
- Windows instrumentation/Application suites pass (0.01/4.24 seconds); Arch
  ASAN Application passes (3.30 seconds). The new test checks the neutral
  completion path, not a collector's wire payload for each lifecycle phase.

### Health outcome HTTP wire coverage (2026-09-13)

- Added `application_health_wire_cases.inc`, exercising real health registry
  refreshes through Telemetry Hub and OTLP to a loopback HTTP collector.
  Six reports cover standard/framework cancellation, timeout, permission error,
  down-without-code, and healthy success. Collector assertions check INTERNAL
  kind, operation outcome, original numeric error, OTLP error status, and absence
  of the service's private diagnostic text. Exactly six spans must arrive.
- The fixture owns its exercise and accept tasks, settles telemetry shutdown,
  stops the collector, and checks accept completion and zero active connections.
  The listener uses fixed loopback port 19435; collision-free parallel execution
  and abnormal fixture cleanup are not independently established by this test.
- Windows complete exporter suite passes in 1.23 seconds. This upgrades health
  outcome evidence from neutral unit completion to actual HTTP wire delivery;
  lifecycle start/stop/recovery wire coverage remains separate work.

### Lifecycle three-signal collector integration (2026-09-13)

- Extended the health collector fixture with an actual frozen service registry
  and lifecycle coordinator: startup, supervised recovery with internal probe,
  and reverse shutdown execute before telemetry settlement.
- Checks exactly two starts, one stop, seven probes, no registered started
  service after stop, and nine received spans. The final three spans must be
  INTERNAL start/recover/stop successes with the correct service identity.
- Enabled all three signals in the same fixture. The collector must receive
  the lifecycle operation metric and exactly three INFO logs, each correlated
  to the corresponding lifecycle span's trace ID and span ID. Every received
  payload is checked for absence of the private probe diagnostic.
- Windows full exporter suite passes (1.25 seconds). The metric assertion proves
  presence, not exact cumulative values; failure outcomes for each lifecycle
  phase and real external service integrations still require further coverage.

### Exact lifecycle metrics and retryable stop failure (2026-09-13)

- The collector fixture now makes the first stop return `timed_out`, verifies
  the original error and retained started-service registration, then retries
  stop successfully and verifies the registration is empty.
- Requires ten spans and four correlated logs. Failed stop must carry timeout
  outcome, the original numeric error, OTLP ERROR status, and ERROR severity;
  successful retry remains a separate successful span/log.
- Replaced the metric-presence check with exact cumulative series checks:
  start/success, recover/success, stop/failure, stop/success each end at 1.
  Checks cumulative temporality, monotonic sums, service identity, duplicate
  label rejection, and nondecreasing repeated snapshots; snapshots are not
  incorrectly added together. Windows exporter suite passes (1.23 seconds).
- This is a service-reported timeout, not deadline-triggered cancellation.
  Recovery failure, startup rollback, real dependencies, and disabled historical
  performance remain separate unfinished acceptance items.

### Failed recovery followed by successful retry on the wire (2026-09-13)

- The collector fixture's second start (first recovery attempt) returns
  permission_denied; the supervisor retries with a deterministic 1ms backoff
  and the third start succeeds. Stop failure/retry remains in the same run.
- Assertions require three starts, two stops, seven probes, eleven spans and
  five trace-correlated logs. Recovery failure and success are separate INTERNAL
  spans, with permission_denied preserved on failure. Stop failure independently
  retains timed_out. All five operation/outcome cumulative series end at 1.
- Windows complete exporter suite passes (1.26 seconds). This verifies one
  recoverable failure, not repeated failures through budget exhaustion or
  recovery against an external database/broker.

### Deadline-driven stop cancellation through OTLP (2026-09-13)

- Replaced the collector fixture's manually returned stop timeout with a real
  cancellable 10-second timer and a 10ms lifecycle service-stop deadline.
  The service confirms cancellation and returns the timer result; lifecycle
  normalizes the elapsed deadline to timed_out, retains the service, and a
  second stop succeeds. This supersedes the manual-timeout limitation above.
- Existing wire assertions still require timeout/error classification, linked
  ERROR log, and separate exact stop/failure and stop/success counters.
  Windows full exporter suite passes (1.27 seconds).
- This proves cooperative timer cancellation, not forced termination of
  noncooperative services, arbitrary socket implementations, or a universal
  process-level shutdown latency bound.

### Enabled/disabled lifecycle behavioral parity (2026-09-13)

- Runs the same health, startup, failed recovery/retry, deadline-cancelled stop,
  and successful stop retry twice: all three telemetry signals enabled, then
  all disabled. Both modes require three starts, two stops, seven probes,
  preserved timeout error, observed cancellation, and empty final registration.
- The disabled mode retains a valid collector endpoint intentionally. Requires
  zero collector requests, zero accepted records, no received spans/logs/metric
  series, and no local application metrics. Enabled mode retains all exact
  wire assertions from the preceding sections.
- Windows full exporter suite passes (1.31 seconds). This is behavioral and
  side-effect parity for the exercised flow, not allocation/CPU equivalence or
  a historical pre-instrumentation performance comparison.

### Remote MySQL live validation (2026-09-13)

- Reused the isolated `cnetmod_otel_test` database on the user-designated 114
  server through an SSH loopback tunnel; no public database port or business
  data was changed. Credentials remained outside the repository and output.
- Windows Release `cnetmod_core` and `test_application_mysql_live` incremental
  builds passed. The live CTest passed in 0.42 seconds with ORM enabled.
- The test covers authentication, three healthy probes, supervised pool stop
  with a retained lease, rejection of acquisition after stop, and down status
  after stop. Read-only ORM queries verify successful rows and server error
  1054/42S22 remain unchanged with disabled, enabled, or throwing exporters.
  Captured spans preserve parent context and omit query text/results.
- This is not evidence of remote outage recovery, MySQL TLS, OTLP delivery of
  database spans, or cross-platform live-database acceptance.

### MySQL closed-session replacement (2026-09-13)

- Extended the real-server test with three cycles of setting a session-only
  marker, sending QUIT, returning the closed lease, and acquiring a replacement
  within three seconds. Every replacement must have a different CONNECTION_ID
  and no old session marker; pool size remains one and waiter count returns
  to zero. The existing ORM exporter-isolation and shutdown checks run afterward.
- Windows Release live CTest passed in 1.07 seconds against the isolated remote
  database. Arch Clang/ASAN compilation and clang-format validation passed;
  this run did not execute the remote test under Arch. Seven invalid-port
  cases and the unavailable-loopback watchdog check also passed on Windows.
- This verifies replacement after an explicitly closed session, not an abrupt
  network failure, server outage, readiness transition, or exhausted recovery
  budget. No persistent SQL data was written. The owned tunnel and temporary
  local credential copy were removed after testing.

### Abrupt MySQL session termination exposes tunnel EOF gap (2026-09-13)

- Added three server-side KILL CONNECTION cycles, using the same test account
  and only the numeric CONNECTION_ID of the retained test lease. The test
  requires a transport error rather than a deadline timeout, then checks
  replacement session identity and isolation as in the graceful-close cycles.
- The initial unbounded query hit CTest's 30-second watchdog. A cancellable
  ping with a three-second deadline now bounds each disconnect observation.
  Windows compilation passes; the live test fails in 11.07 seconds with exactly
  three assertions rejecting timed_out. Replacement and shutdown assertions
  do not fail, but this is not a passing disconnect test.
- Inspection of the SSH skill's `_forward_data` shows each forwarding thread
  simply exits on EOF without propagating a half-close, and the handler joins
  both directions before closing sockets. This is consistent with the client
  waiting for EOF until its own cancellation. Direct transport validation or
  a corrected forwarding harness is required before attributing this failure
  to cnetmod or claiming abrupt disconnect acceptance.

### Tunnel half-close correction and abrupt disconnect acceptance (2026-09-13)

- Two local socket-pair regressions reproduced missing EOF in both forwarding
  directions. The SSH skill now forwards normal EOF via SHUT_WR, preserving
  reverse-stream responses, and shuts both directions down on forwarding
  exceptions so its reader threads can join. Both regressions pass afterward.
- With that corrected tunnel, the unchanged Windows live test passes in
  2.03 seconds: three graceful-close cycles and three server-terminated sessions
  all reconnect with fresh identities and no retained session marker. Abrupt
  disconnect probes return errors without reaching their three-second deadline.
  The ORM isolation and supervised stop assertions also pass.
- The forwarding fix is in the local SSH skill, outside this repository.
  Its generic metadata validator rejects the skill's pre-existing version and
  keywords fields; these unrelated metadata fields were not changed.
- This resolves the prior forwarding-induced failures, not the outstanding
  full-server outage, readiness/recovery-budget, cross-platform live execution,
  or disabled-observability performance acceptance requirements.

### Required MySQL recovery budget with real pool maintenance (2026-09-13)

- Added `mysql_required_recovery_budget_stops_pool_maintenance` using the real
  mysql_service, lifecycle, and supervisor against a bound, non-listening
  loopback port. No database process is modified and no fake managed service
  substitutes for the MySQL adapter.
- The 60ms recovery episode must not terminate early, reports exactly one
  required-service exhaustion callback, preserves health_recovery/timed_out,
  and joins pool maintenance after the callback requests supervisor stop.
  Waiters return to zero, no service is marked successfully started, and a
  subsequent acquisition is rejected as canceled. Telemetry is disabled.
- Windows complete Application CTest passes in 4.32 seconds and Arch ASAN in
  3.38 seconds. This checks the
  adapter's recovery wiring, not the Host's readiness HTTP response or its
  automatic process exit during an outage of a previously healthy dependency.

### Host cleanup after failed MySQL automatic startup (2026-09-13)

- Added `mysql_host_failed_start_cleans_supervised_pool`: the builder explicitly
  auto-configures a required MySQL pool against a reserved non-listening port.
  The pool registers maintenance but cannot finish startup within 30ms.
- Host run must return timed_out within two seconds, reach stopped, withdraw
  readiness, and leave no pool waiters. Host destruction is exercised after
  those assertions, including under Arch ASAN. This covers partial startup
  ownership rather than assuming only successfully started services own work.
- Complete Application tests pass on Windows (4.34 seconds) and Arch ASAN
  (3.38 seconds). This does not establish all allocation-failure paths, optional
  service behavior, or the HTTP readiness transition during a running outage.

### Optional startup timeout no longer aborts the Host (2026-09-13)

- The real MySQL adapter exposed a lifecycle bug: a task-group timer used the
  per-service startup deadline, so an optional service timing out poisoned the
  entire layer despite its child converting the failure into degraded startup.
- The group now enforces the total startup deadline; each child wraps its own
  start in the per-service deadline. Required failures still propagate to the
  group, and an expired total budget still fails startup. This change is on
  lifecycle startup paths, not the normal protocol request path.
- `mysql_optional_outage_keeps_host_live_but_not_ready` failed before the fix
  because Host rolled back. Afterward three HTTP observations spaced by 80ms
  require live=200 and ready=503 with a 60ms recovery budget. Host stays running
  through those windows, then explicitly stops successfully with zero waiters.
- Complete Application suites pass on Windows (4.72 seconds) and Arch ASAN
  (3.70 seconds); Windows disabled-overhead/allocation regressions pass (0.86
  seconds). Application skill and generated AGENTS are synchronized. These
  results do not establish historical disabled CPU performance equivalence or
  recovery of a previously healthy dependency after an external outage.

### Startup budget and sibling isolation regression (2026-09-13)

- Extended failed-start Host coverage with an optional MySQL service whose
  200ms service limit exceeds the 30ms overall startup budget. Overall expiry
  must still return timed_out, finish shutdown, and clear pool waiters. The
  required/optional pair passed ten consecutive Windows runs.
- The optional-outage HTTP fixture now includes a same-layer required service.
  Its event log must contain exactly one start and one final stop, while live
  remains 200 and ready remains 503 across recovery windows. This guards against
  accidentally rolling back or restarting a healthy sibling when an optional
  service times out.
- Complete Application tests pass on Windows (4.71 seconds) and Arch ASAN
  (3.76 seconds); the changed fixture passes clang-format validation.

### Disabled ORM error-path allocation parity (2026-09-13)

- Expanded the ORM allocation fixture from successful empty results to both
  success and failure, including error 1054, SQLSTATE 42S22 and an allocating
  diagnostic string. Query, text execute and bound execute compare task creation
  and complete execution against the original non-observed entrypoints.
- With an empty exporter, allocation counts and allocated bytes must match in
  all six operation/outcome combinations; errors and diagnostic content remain
  unchanged even when query capture is requested in observation options.
- Complete disabled-overhead/fault-injection suites pass on Windows (0.85
  seconds) and Arch ASAN (3.31 seconds). Removed the fixture's unused parameter
  name afterward. These are same-build allocation comparisons with a controlled
  client, not CPU benchmarks or historical protocol implementation equivalence.

### Expanded protocol test build uncovered portability gaps (2026-09-13)

- Rebuilt and passed twelve Windows suites: Application integrations, ORM,
  MongoDB wire, Redis, OpenAI, gRPC, MQTT, OTLP, HTTP observation/tracing,
  metric aggregation and instrumentation (6.62 seconds total).
- Arch's current configuration disables gRPC and MongoDB. Expansion exposed
  an unconditional MongoDB test target and a missing direct circuit_breaker
  module import in the OpenAI test. Added the protocol gate and direct import.
- Nine applicable Arch ASAN suites passed, while ORM exposed a temporary
  coroutine-lambda stack-use-after-scope in its concurrent lazy-relation fixture.
  Replaced detached invocation with a retained callable and owned task; the
  complete ORM suite then passed ASAN (0.20 seconds). The other nine passed
  in the preceding batch; gRPC/MongoDB Linux coverage remains unverified.
- These are local build/test results, not evidence that the real-service CI,
  all-protocol Linux build, macOS build or disabled CPU performance gates passed.

### Complete Windows protocol-free test set (2026-09-13)

- Reconfigured the existing protocol-free Windows build with all protocol
  options disabled. Built the core and all eighteen applicable C++ test targets.
  A QuarkCloudDrive lock on the core build-state file interrupted one redundant
  reference build; remaining tests built against the already-built core.
- Found and reproduced an unconditional C API test: it was registered when
  cnetmod_c did not exist, losing its public include path, and directly imported
  HTTP despite HTTP being disabled. Registration now requires both the actual
  C API target and HTTP. Reconfiguring the HTTP/C-API-enabled Windows build
  confirms the test remains registered there.
- The complete protocol-free CTest set passes 18/18 in 0.49 seconds without
  exclusions. Circuit-breaker test compilation still emits nodiscard warnings;
  this is not a warning-clean or all-platform build claim.

### Late startup success retains rollback ownership (2026-09-13)

- A new regression reproduced lost ownership after the per-service deadline
  change: a service observes cancellation but returns success after acquiring
  resources; deadline normalization returns timed_out before mark_started, so
  rollback previously omitted stop (one start event, zero stop events).
- Startup now records the raw successful acquisition inside the deadline-owned
  operation, before timeout normalization. The caller still receives timed_out,
  while rollback sees ownership and closes the component exactly once. Recovery
  already records raw start success before its own deadline normalization.
- Complete Application and disabled-overhead/fault-injection tests pass on
  Windows (4.72/0.86 seconds) and Arch ASAN (3.91/3.41 seconds). Skill and AGENTS
  synchronization checks pass. This is not proof that every unsuccessful custom
  start self-cleans partially acquired resources or responds to cancellation.

### Late successful stop is not retried (2026-09-13)

- Reproduced the symmetric shutdown ownership bug: stop completes resource
  release after cancellation and returns success, but deadline normalization
  reports timed_out; the old code retained ownership and called stop again.
- Raw successful release now clears registration before normalization. The
  first caller still receives timed_out and telemetry retains that outcome,
  while the health state is stopped and a later cleanup does not repeat stop.
  Raw failures continue to retain ownership for retry.
- The new regression failed on retained state, health status and duplicate stop
  before the fix. Complete Application and disabled-overhead/fault suites now
  pass on Windows (4.76/0.86 seconds) and Arch ASAN (3.77/3.27 seconds).
  Application skill and generated AGENTS are synchronized.

### Late release unblocks dependency shutdown (2026-09-13)

- Extended the late-stop regression with a lower-level required dependency.
  Exact event order must be dependency start, dependent start, dependent stop,
  dependency stop. Both end stopped and subsequent cleanup produces no events,
  although the first cleanup still reports the dependent's timeout.
- Complete Application tests also retain the complementary regression where
  a real stop failure keeps transitive dependencies alive for a later retry.
  Application/OTLP suites pass on Windows (4.73/1.32 seconds) and Arch ASAN
  (3.77/0.64 seconds); fixture formatting validation passes.

### Running Host readiness-to-shutdown wiring (2026-09-13)

- Added a real HTTP management fixture around Host with a controllable required
  fake service. It first requires readiness=200, switches the dependency to
  unavailable, then requires readiness=503. The 200ms recovery budget must cause
  Host to reach stopped before the test sends any explicit stop request.
- After joining the Host thread, run must have failed, readiness remains false,
  and the service event log contains exactly one stop. A local scope guard
  requests cleanup if inspection throws. Telemetry is disabled in this fixture.
- Complete Application suites pass on Windows (5.08 seconds) and Arch ASAN
  (4.06 seconds). This verifies management HTTP and lifecycle wiring with a
  fake dependency, not a real database/broker outage or successful recovery.

### Recovered Host does not inherit a stale shutdown budget (2026-09-13)

- Extended `application_runtime_health_cases.inc` with a shared fixture and a
  separate successful-recovery test. Real HTTP readiness moves from 200 to 503
  and back to 200 after the atomic fake dependency availability is restored.
- After recovery, the fixture waits another 300 ms, beyond the original 200 ms
  recovery budget, and verifies both HTTP readiness and the running Host state.
  Explicit shutdown must then succeed and close the service exactly once.
- The unrecovered branch still verifies automatic failure shutdown. Both branches
  run with traces, metrics, and logs disabled; this is lifecycle evidence, not a
  disabled-path performance measurement or a real database outage test.
- Full Application tests passed on Windows Release (5.51 s) and Arch Clang ASAN
  (4.44 s). The changed fixture passes clang-format checking; AGENTS.md is current.

### Independent repeated-outage budget through the Host (2026-09-13)

- Added `application_second_required_outage_receives_fresh_bounded_recovery_budget`.
  After confirmed recovery and a 300 ms healthy interval, a second fake dependency
  outage must still expose live=200 and ready=503 after 100 ms, despite the first
  episode's 200 ms deadline having already passed. It must subsequently terminate
  automatically within the fixture's 2 s observation bound, return failure, and
  stop the managed service exactly once.
- The fixture uses named scenarios rather than interacting Boolean options, sharing
  startup and cleanup checks across initial exhaustion, recovery, and repeated outage.
- Full Application suites passed after rebuilding on Windows Release (6.18 s) and
  Arch ASAN (5.06 s). No production source was changed for this addition. These
  controlled dependency tests do not establish real broker/database recovery or
  disabled-OTEL CPU equivalence.

### Live MySQL normal-return isolation failure (2026-09-13)

- Added a normal lease-return check to `test_application_mysql_live`: set a
  test-only session variable, destroy the lease, reacquire from the one-slot pool,
  and require the variable to be NULL. This does not modify persistent table data.
- Rebuilt Windows Release and ran against the dedicated 114 MySQL test database
  over the SSH skill tunnel. The new assertion failed (actual `0`, expected `1`)
  in 1.96 s; the same invocation reported no other assertion failures.
- Inspection confirms `needs_reset` is only written in `mysql_pool_impl.cpp`;
  no pool path reads it or calls `reset_connection`. Normal return advertises the
  connection as idle before any session reset. The live suite is therefore
  currently failing, and the earlier successful reconnect tests do not establish
  normal-return isolation. Keep this regression active while implementing reset.
- Separately, the contended return path constructs `return_connection_async`
  and a `start_worker` completion ticket from a destructor/noexcept move path.
  Allocation failure there is an unresolved exception-safety risk; this inspection
  is not a fault-injection reproduction or a completed fix.
- Closed this run's tunnel and removed the local downloaded credential copy.
  The remote test database and protected credential source were retained.

### MySQL normal-return session reset implemented (2026-09-13)

- Normal return now publishes resetting rather than idle. The existing owned
  connection worker performs COM_RESET_CONNECTION before publishing the idle bit
  and notifying waiters. No additional detached reset task is introduced.
- Added the cancellable client reset overload. Pool reset uses ping_timeout and
  the worker's network cancellation token; failed resets become dead for reconnect.
  Explicit return_without_reset retains the immediate idle-return behavior.
- The previously failing 114 regression passed (2.22 s), then passed again with
  explicit no-reset preservation and immediate try_get_connection checks (2.52 s).
  Windows Application passed (6.20 s), Arch ASAN Application passed (5.07 s).
  Arch compiled the live target before the final no-reset assertion extension;
  the remote live test was executed on Windows, not Arch.
- Reset adds the protocol cleanup round trip required by the documented default
  lease contract, independent of OTEL. This is not evidence of historical CPU
  equivalence. Timeout/cancellation fault injection, contended-return allocation
  failure, and comprehensive cross-thread lease ownership remain open.
- Closed the owned SSH tunnel and removed the local credential copy after testing.

### Silent reset response and shutdown cancellation (2026-09-13)

- Added `mysql_pool_reset_cases.inc`, included by the existing ORM/MySQL target.
  A loopback protocol peer sends a 4.1 greeting and authentication OK, receives
  COM_RESET_CONNECTION, asserts zero idle pool slots, then withholds its response.
- Timeout mode uses a 30 ms reset budget and observes old-session closure before
  requesting pool stop. Shutdown mode uses a 10 s reset budget, requests stop
  after receiving reset, and observes closure and all three owned tasks finishing.
  Both assert no pool waiters remain. No detached fixture coroutines are used.
- Windows filtered pool tests passed under a 5 s CTest bound (0.62 s); complete
  ORM/MySQL tests passed on Windows (1.15 s) and Arch ASAN (0.29 s, 5 s bound).
- This verifies interruption of an outstanding reset response over plain TCP;
  it does not prove subsequent successful reconnect, TLS reset cancellation,
  contended return allocation safety, or performance equivalence.

### Reset timeout restores pool capacity (2026-09-13)

- Extended the timeout fixture to accept a second TCP session after observing
  old-session closure. The peer completes authentication and answers COM_PING;
  an already waiting borrower must acquire that replacement and receive success.
- The test asserts a one-slot pool remains size 1, both peer-side protocol events
  occur, all owned fixture/maintenance tasks finish, and waiter count returns to
  zero. The separate stop-during-reset mode remains covered.
- Full ORM/MySQL suites passed with a 5 s process bound on Windows Release
  (1.14 s) and Arch ASAN (0.34 s). Formatting check passed.
- This extends the loopback plain-TCP failure fixture, not the 114 live test.
  TLS, malformed/error reset replies, contended-return allocation failures, and
  cross-thread lifecycle safety remain separate open requirements.

### Reset acknowledgements reject malformed packets (2026-09-13)

- Reproduced the old unconditional non-ERR success path with a malformed reset
  response: the regression timed out after 15.02 s before the fix.
- Reset now validates the response header, both length-encoded OK fields and
  protocol-4.1 status/warning bytes. Malformed packets disconnect with protocol_error.
  ERR packets require their code and SQLSTATE prefix; valid errors retain SQLSTATE.
- Five malformed replies (unknown header, truncated OK, truncated ERR, truncated
  length encoding, incomplete status) and a valid server ERR now exercise old
  session removal, fresh authentication, and a successful waiting-borrower PING.
- Full ORM/MySQL tests passed on Windows Release (1.17 s) and Arch ASAN (0.84 s).
  Formatting and generated AGENTS checks passed. TLS and allocation-failure
  shutdown/return safety remain open.
- Correction to the preceding test-bound notes: the generated test property sets
  TIMEOUT=15, overriding the supplied global --timeout 5. The successful elapsed
  times are valid, but those invocations did not enforce a 5 s process limit.

### MySQL pool tests independent of ORM and HTTP (2026-09-13)

- Moved the reset fixture entry point from the ORM executable into test_mysql_pool,
  with explicit direct module imports. CMake registers it whenever MYSQL is enabled,
  independently of HTTP and ORM, with its own TIMEOUT=5 property.
- Windows default build: pool 0.05 s, ORM 1.12 s. Arch ASAN default validation
  build: pool 0.65 s, ORM 0.22 s; all passed after rebuilding.
- Reconfigured the existing cmake-build-mysql-no-http validation cache with ORM=OFF
  (HTTP was already OFF), built the complete test target and its dependencies,
  then passed test_mysql_pool in 0.05 s. Default build configuration is unchanged.
  Existing async_op nodiscard warnings mean this was not a warning-free build.
- This closes the reset fixture's accidental ORM test dependency, not the full
  platform/protocol matrix or all other pool tests still located in ORM sources.

### Single-source MySQL return state (2026-09-13)

- Removed the write-only conn_node needs_reset atomic. The resetting state now
  carries the reset requirement without redundant per-return and post-reset stores.
- Extracted publish_returned_connection as a private implementation method shared
  by immediate and contended asynchronous return paths. Bitmap publication and
  maintenance wakeup no longer have two separately maintained implementations.
- Windows rebuilt pool and ORM suites passed (0.05 s and 1.12 s). The refactor
  leaves the existing contended return worker allocation path in place; it does
  not claim to fix allocation failure from destructors or prove CPU equivalence.

### Contended return uses the existing connection worker (2026-09-13)

- Removed return_connection_async and its start_worker call from lease return.
  A contended no-reset return publishes returning, wakes the existing maintenance
  worker, and becomes idle only when that worker obtains the pool mutex and
  notifies FIFO waiters. Default returns continue through resetting.
- This removes the coroutine frame and completion-ticket allocations previously
  reachable from lease destruction. Immediate no-reset return retains the idle
  fast path. No replacement detached task or busy retry queue was introduced.
- Windows Application (6.19 s), ORM (1.12 s), and pool (0.05 s) suites passed.
  Arch ASAN ORM (0.24 s) and pool (0.70 s) passed; formatting check passed.
- Existing regressions do not deterministically force metadata-lock contention.
  Targeted contention/allocation instrumentation and cross-thread wakeup auditing
  are still required before claiming destructor safety or performance closure.

### Deterministic contended no-reset return allocation probe (2026-09-13)

- Added mysql_pool_contention_cases.inc using the existing allocation-test executable.
  A one-shot test-only allocator hook runs while demand-driven pool expansion owns
  its metadata mutex, with another borrower already queued. It checks try_get
  returns resource_unavailable_try_again, then arms fail-after-zero and returns
  the held lease without reset. The failure probe must remain unconsumed.
- After the lock is released, the FIFO borrower must receive the returned connection;
  a second queued borrower is cancelled, waiter count reaches zero, and owned
  maintenance is joined. No production test-access API or allocator change was added.
- Windows targeted test passed (0.03 s); full allocation suites passed on Windows
  (0.88 s) and Arch ASAN (3.30 s). Formatting and AGENTS checks passed.
- This is deterministic same-thread reentry under lock contention, not a cross-thread
  data-race proof. Default-reset destruction and historical disabled-OTEL CPU parity
  remain outside this test's evidence.

### Contended default lease destruction (2026-09-13)

- Extended the allocation fixture with a separate default-destruction case. The
  held lease is moved into optional storage and destroyed inside the expansion
  allocator hook while the pool mutex is owned. The armed allocation failure must
  remain unconsumed, just as for explicit no-reset return.
- The peer reads the full COM_RESET_CONNECTION packet, observes zero idle slots,
  sends OK, and the FIFO borrower succeeds. Another borrower is cancelled and
  maintenance is joined. Login reads now consume the framed packet across partial
  TCP reads instead of assuming a single read contains it.
- Full allocation suites passed on Windows (0.92 s) and Arch ASAN (3.36 s), and
  fixture formatting passed. This is still controlled same-thread reentry, not
  cross-thread race coverage or a historical CPU performance result.

### Atomic registration of connection maintenance wakeups (2026-09-13)

- Source audit found task_waiting was a plain coroutine handle shared by return,
  shutdown, and the maintenance awaiter. The awaiter checked in_use before
  registration without checking again, allowing a return-before-registration gap.
- Changed the slot to an atomic handle. Registration now rechecks connection state
  and stop_requested; it either withdraws its own handle and continues or leaves
  wakeup ownership with the producer that already exchanged it. Return publication,
  slot handoff, and stop publication use sequential ordering for this protocol.
- Windows allocation (0.92 s), ORM (1.13 s), pool (0.05 s) and Arch ASAN allocation
  (3.37 s), pool (0.62 s) suites passed after rebuilding.
- This is an audited synchronization change plus existing regression coverage, not
  a deterministic reproduction of the registration interleaving or a ThreadSanitizer
  proof. Other cross-thread node metadata and lifetime interactions remain open;
  no CPU-equivalence claim follows from these tests.

### Removed unused per-connection timestamp writes (2026-09-13)

- Audited all MySQL last_used references: one field and six assignments, with no
  reader. Removed the field and assignments, including steady_clock reads on
  borrow and return. Existing retry, ping, and timeout scheduling does not use it.
- This removes unnecessary shared writable metadata instead of preserving it as
  an unused concurrent timestamp. Other node state, timer, and ownership races
  still require auditing; no full race-freedom or measured CPU claim is made.
- Rebuilt Windows and Arch core/test targets. Windows allocation (0.92 s), ORM
  (1.12 s), pool (0.05 s), and Arch ASAN allocation (3.31 s), pool (0.69 s) passed.
  A subsequent formatting-only blank-line cleanup does not change those semantics.

### Idle-timer preparation rechecks returned state (2026-09-13)

- Added a state recheck after resetting the idle-sleep cancellation token. A return
  occurring between the earlier idle check and token reset must not be followed
  by a fresh long idle wait while resetting, returning, or dead work is pending.
- Count returning as an established connection during warmup, consistent with
  resetting/pinging rather than treating this metadata handoff as a new connection.
- Windows Application (6.20 s), allocation (0.92 s), pool (0.05 s), and Arch ASAN
  allocation (3.38 s), pool (0.61 s) passed after rebuilding. The first Windows
  library link failed with LNK1104; the terminal retry succeeded. Formatting was
  normalized and rechecked afterward; AGENTS remains current.
- The change follows source-level interleaving analysis. These tests do not force
  the exact pre-reset scheduling window, and remaining cancellation/lifetime races
  and performance equivalence are not declared closed.

### Independent pool CI gates (2026-09-13)

The Windows, Linux and macOS application/OTEL gate lists now explicitly run
`test_mysql_pool` with `--no-tests=error`. These workflows use selected test
lists, so building the executable alone did not execute its regression cases.
The Windows no-HTTP probe also runs the pool suite with ORM enabled and then
reconfigures the same cache with ORM disabled, rebuilding and running the
independent pool target again.

Local Windows verification rebuilt `test_mysql_pool` in
`cmake-build-mysql-no-http`; the authoritative cache has MySQL ON, HTTP OFF and
ORM OFF. CTest passed (0.04 seconds). All three edited workflow files parsed as
YAML and the generated AGENTS check passed. This is local configuration and
test evidence, not evidence that the remote workflows or macOS builds passed.
It does not settle disabled-OTEL historical CPU performance or cross-thread
pool wakeup correctness.

### PostgreSQL shutdown resume ownership (2026-09-13)

Pool shutdown now honors the cancellation token's pending-ownership exchange.
If cancellation has already claimed the waiter, close removes it from the queue
but does not post a second resume or clear callback storage. The cancellation
path retains responsibility for its deferred cleanup and resume.

`postgresql_pool_close_preserves_claimed_cancellation_resume` deterministically
models the claimed-but-not-resumed interval using the backend token ownership
field, verifies close does not resume the borrower, and completes the owner's
single resume. This is a state-transition regression, not a real cross-thread
stress test. Arch Clang ASAN rebuilt and passed the application integration
target (1.28 seconds). Windows verification, allocation-free deferred release,
and comprehensive cancellation callback lifetime safety remain outstanding.

### Shutdown ownership regression sensitivity (2026-09-13)

Windows rebuilt the core and application integration target and passed CTest
(1.44 seconds). On Arch ASAN, temporarily removing only the close-path ownership
branch caused `postgresql_pool_close_preserves_claimed_cancellation_resume` to
fail at `!waiting.handle().done()` with exit status 1. The ownership branch was
restored and the Arch integration executable rebuilt. This controlled mutation
demonstrates that the regression detects the erroneous shutdown resume rather
than merely exercising successful cleanup. It does not exercise a concurrent
cancellation callback or remove the remaining detached release/cancel tasks.

### Slot-owned PostgreSQL return notifications (2026-09-13)

Removed `release_async`. Contended lease destruction now posts the slot's
embedded raw notification; it allocates neither a return coroutine nor a queue
node. Stable deque slots remain checked out until notification dispatch, which
keeps close from erasing pending return storage. Dispatch retries on a later
queue drain if the metadata lock is busy. The uncontended path remains direct.
This adds fixed per-slot storage and is not a historical performance guarantee.

Arch ASAN rebuilt and passed application integrations (1.29 seconds) and the
disabled-overhead suite (3.33 seconds). These existing cases do not force the
new contended callback path. Dedicated allocation-failure/contended-return
tests, fairness under contention, Windows build, and removal of detached waiter
cancellation cleanup remain required.

### PostgreSQL contended return allocation regression (2026-09-13)

The waiter-allocation fixture now has a contended-return variant. A one-shot
allocator hook returns the held lease during waiter deque growth, while pool
metadata is locked, with the next nested allocation configured to fail. It
checks that this failure budget remains untouched, waits for the first borrower
to receive the returned connection, cancels remaining waiters, verifies exactly
one grant and zero checked-out/waiting leases, then reacquires and closes.
The original deque-growth allocation-failure test remains a separate case.

Arch ASAN passed the full disabled-overhead target (3.41 seconds), and a direct
filtered run confirmed the new case actually executed. Windows rebuilt core
and both targets; application integrations passed (1.44 seconds) and disabled
overhead passed (0.94 seconds). This forces same-thread lock reentry, not parallel
execution or repeated callback requeue. Cancellation cleanup ownership and
cross-thread lifetime/fairness remain open.

### Waiter-owned PostgreSQL cancellation settlement (2026-09-13)

Removed the remaining pool `cancel_waiter_async` detached task and spawn import.
Cancellation claims the token and posts a notification embedded in the suspended
waiter. The owning executor removes the queue entry under the state lock and
resumes once; contended cleanup is reposted without allocating task/queue storage.
Registered waiter accounting is retired under the state lock after resumption.
Close waits for this accounting as well as checked-out slots, so clearing the
FIFO does not falsely mean cancellation cleanup has completed.

The claimed-cancellation shutdown test now verifies close itself remains pending
until the owner resumes the waiter. Both waiter-allocation fixture variants also
inject allocation failure around cancellation and verify the budget is untouched.
Windows rebuilt core and both targets, passing integrations (1.46 seconds) and
disabled overhead (0.93 seconds). Arch ASAN passed the same targets (1.29 and
3.35 seconds). Source search finds no pool spawn/release_async/cancel_waiter_async.
This does not establish arbitrary cross-thread token registration safety, fairness
under sustained metadata contention, or historical CPU/frame-size parity.

### PostgreSQL synchronized cancellation registration (2026-09-13)

The pool now uses cancel_token's existing latch-protected registration API rather
than legacy public callback fields. Normal return, reconnect handoff and close
use complete_callback to claim completion; cancellation retirement uses
finish_callback. Already-cancelled registration queues its own cleanup, so the
register/cancel boundary does not require a racy callback-pointer recheck.
This synchronization applies to queued acquisition, not idle-slot acquisition.

The shutdown regression now invokes cancellation on an actual joined thread
after registration and starts close before the notification is drained. It checks
both tasks are pending, then lets the owning executor settle them. The thread is
joined outside coroutines. Arch ASAN integrations and disabled-overhead tests
passed (1.31 and 3.32 seconds). This is not simultaneous registration/cancellation
stress, TSAN evidence, or proof of historical CPU parity.

### PostgreSQL registration/cancellation race attempts (2026-09-13)

The shutdown fixture now performs 256 registration/cancellation race attempts
while its only connection slot is reserved by an unfinished handshake. A joined
thread is released immediately before the owning thread resumes acquire; the
executor then drains cancellation. Every iteration requires operation_canceled,
zero queued waiters and a retired token pending flag. Thread waits/joins and
polling occur outside coroutine execution, and frames remain owned until done.
The existing cancellation-before-close sequence then verifies final settlement.

Windows rebuilt and passed application integrations (1.43 seconds); Arch ASAN
rebuilt and passed (1.31 seconds). Scheduling does not guarantee every critical
interleaving is reached. This is bounded race-attempt coverage, not TSAN evidence,
full pool cross-thread support or historical performance evidence.

### Allocation-free PostgreSQL waiter completion posts (2026-09-13)

Normal lease handoff, reconnect handoff, early cancellation and shutdown now
post the existing waiter notification node through one private post_waiter
helper. Completion ownership must be obtained before changing a registered
notification from cancellation callback mode to coroutine mode. This removes
heap queue-node allocation from noexcept lease return with queued borrowers;
it does not remove allocations needed to construct acquisition tasks or grow
the waiter container.

Both waiter-allocation fixture variants now borrow a connection, queue another
borrower, arm immediate allocation failure and return the lease. They require
the failure budget to remain untouched and the queued handoff to succeed before
final close. Windows integrations/disabled overhead passed (1.42/0.98 seconds);
Arch ASAN passed (1.32/3.63 seconds). Shutdown and reconnect posting use the same
helper but do not yet have their own direct allocation-failure injection cases.

### PostgreSQL in-place shutdown ownership (2026-09-13)

Close no longer constructs an empty deque and swaps live slots into it. After
borrowers and registered waiters settle, it terminates connections in the owned
slot container, then clears slots and refreshes snapshots under the metadata
lock. Serialized close callers and the closing flag exclude new slot mutations.
If termination throws, slot ownership remains in the pool for a subsequent close
attempt; direct transport-failure retry injection remains to be added.

`postgresql_empty_close_requires_no_staging_allocation` constructs its close
task before arming allocation failure, then requires synchronous empty shutdown
without consuming the failure budget. This excludes coroutine construction from
its claim. Windows integration/disabled-overhead targets passed (1.42/0.98 seconds),
and Arch ASAN passed (1.32/3.37 seconds). Live shutdown may still allocate transport
tasks and timers and is not claimed to be allocation-free or unconditionally bounded.

### PostgreSQL connected close allocation failure and retry (2026-09-13)

Both waiter-allocation fixture variants now preconstruct a close task while a
real loopback connection is idle, arm failure for the next allocation and resume
close. They require the failure to be consumed, bad_alloc to propagate, the pool
to retain one slot with no borrower, and subsequent acquire to return
operation_canceled. A second close must succeed and remove the slot; the owned
peer task waits for transport EOF before the fixture completes.

The disabled-overhead target rebuilt and passed on Windows (0.97 seconds) and
Arch ASAN (3.35 seconds). This verifies retry after an allocation failure early
in connected shutdown, not failure during a partially sent termination frame,
TLS shutdown, or arbitrary transport stalls.

### PostgreSQL stop-deadline propagation gap (2026-09-13)

Source audit confirms a remaining application shutdown defect:
`postgresql_service::stop` discards service_context and awaits the uncancellable
pool close. Close polls outstanding borrowers without cancellation; client
terminate uses uncancellable write_all and SSL async_shutdown. The lifecycle's
outer with_deadline therefore cannot alone establish bounded settlement.
Successful retry/allocation tests above must not be interpreted as deadline proof.

The required implementation chain is service stop -> cancellable pool settlement
-> cancellable client termination -> cancellable encrypted shutdown when enabled.
Existing SSL fill_rbio/flush_wbio cancellation overloads can be reused, but public
async_shutdown currently has none. Cancellation must retain owned slots and
pending cleanup for retry; it must not destroy borrowed clients or suspended
callback storage. Tests must hold a lease past the deadline, verify timeout without
premature destruction, return it and retry shutdown, and separately stall the
transport. No deadline fix or passing evidence is claimed by this audit.

### Cancellable shutdown transport prerequisite (2026-09-13)

SSL stream now exposes async_shutdown(cancel_token&). Both public overloads
return one templated shutdown task; if constexpr selects ordinary or cancellable
BIO/socket waits, avoiding runtime token checks in the existing ordinary path.
Precancellation returns operation_canceled before calling SSL_shutdown. Socket
ownership remains with the caller. Existing shutdown result semantics otherwise
remain unchanged.

Windows SSL-enabled core and application integrations rebuilt and passed (1.41
seconds), including application_transport_shutdown_honors_precancellation. Core
format checks passed. This prerequisite is not yet wired into PostgreSQL client,
pool or service stop. In-flight encrypted shutdown cancellation and Linux direct
socket BIO coverage remain unverified; the Arch ASAN cache has SSL disabled.

### PostgreSQL cancellable client termination (2026-09-13)

Added client::terminate(cancel_token&) returning expected<void,error_code>.
It takes operation ownership, rejects overlap, preserves a precancelled session,
and passes cancellation through Terminate writes and SSL shutdown. Failed
transport cleanup disconnects and returns its error. The original no-argument
terminate implementation is unchanged; pool/service stop still need integration.

The owned loopback peer now receives the five-byte PostgreSQL Terminate message
after a precancelled attempt first proves the session remains open. A byte-printing
test assertion initially failed compilation on both toolchains and was corrected.
Windows SSL-enabled build/integrations passed (1.42 seconds), and Arch ASAN with
SSL disabled passed (1.31 seconds). Encrypted in-flight cancellation and blocked
write behavior remain unverified, as does end-to-end application stop deadline.

### PostgreSQL application stop deadline reaches pool (2026-09-13)

Added close(cancel_token&) and a compile-time-selected pool close implementation.
The existing no-argument close forwards directly to the void specialization;
there is no extra wrapper coroutine. Cancellable close checks cancellation while
waiting for the close lock, reconnect completion, and outstanding leases/waiters,
then calls cancellable client termination. Cancellation retains pool-owned slots
and notifications; callers must keep the pool alive and retry cleanup. Application
stop now forwards its token/deadline and changes started state only on success.
An unstarted, empty service retains its successful no-op stop behavior.

The service probe/recovery fixture now holds a lease over a 30ms stop deadline,
requires timed_out within one second, verifies the held client is still open,
then returns it and successfully retries stop. The peer stays alive until EOF.
Initial tests exposed the empty-service stop compatibility case, which was fixed.
Windows required retries for transient library link failures; final integrations
and disabled overhead passed (1.48/0.98 seconds). Arch ASAN passed (1.88/3.38 seconds).
This does not establish bounded host destruction with escaped leases, callback
settlement after an abandoned failed close, or blocked encrypted transport shutdown.

### Encrypted shutdown silent-peer deadline (2026-09-13)

Added ssl_shutdown_cases.inc to the existing certificate-backed test_redis_tls
target. Owned client/server tasks complete a real TLS handshake. The server then
reads raw ciphertext without acknowledging close_notify; the client calls the
cancellable shutdown under a 30ms timeout, requires timed_out within one second,
closes its socket, and both tasks settle. The server requires incoming ciphertext
and connection termination (EOF or transport error).

An initial run failed before handshake because the skipped dependency build had
not generated test certificates; CTest terminated it at 15 seconds. Building the
existing redis_tls_test_identity target corrected the fixture. Missing certificate
or key now fails the new case immediately. Windows rebuilt and passed all three
TLS cases (0.56 seconds); a filtered run separately confirmed this case executed.
This is Windows memory-BIO evidence, not Linux direct-BIO or complete PostgreSQL
encrypted service-stop evidence. The case currently depends on the Redis/TLS
fixture target and is not an SSL-only CI gate.

### Independent encrypted shutdown test gate (2026-09-13)

The silent-peer shutdown case moved from test_redis_tls into test_ssl_shutdown.
CMake registers it whenever SSL and the OpenSSL fixture generator are available,
independently of Redis. Both targets reuse the existing certificate files and
generation dependency. The independent test has a five-second CTest limit and
tls/shutdown labels. Three platform application gate lists now require this target
with no-tests=error, so an absent runtime fixture is not silently accepted in CI.

Windows reconfigured, rebuilt and passed the independent shutdown target (0.05
seconds) and existing Redis/TLS target (0.48 seconds). Three workflow YAML files
parsed and AGENTS regeneration/check passed. Remote CI, SSL-only feature-matrix
builds, fixture-tool availability on hosted runners and Linux direct-BIO runtime
verification have not been established by these local results.

### Linux SSL-only runtime validation (2026-09-13)

Created /root/cnetmod-ssl-only-validation without changing existing ASAN caches.
The authoritative cache has SSL ON and HTTP/Redis/ORM OFF. Clang 22.1.8 built
test_ssl_shutdown and required dependencies; CTest passed the silent-peer deadline
case (0.04 seconds). This proves the independent test is available and runs in a
protocol-free SSL configuration. This Release build is not ASAN-instrumented.

The initial system-provider assumption was wrong: ThirdPartyDependencies.cmake
forces the bundled BoringSSL provider regardless of the supplied cache override.
System OpenSSL generated the test identity only. The bundled headers do not define
SSL_OP_ENABLE_KTLS, and ssl_stream enables direct socket BIO only inside that
feature guard with kernel TLS requested. Thus current supported Linux verification
is memory BIO. Earlier entries calling direct BIO an untested active Linux path
should be read with this correction; no direct-BIO/kTLS support is claimed.
Build output included existing warnings, so this is not a warning-clean build.

### Concurrent PostgreSQL close-owner deadline regression (2026-09-13)

The service recovery/stop fixture now has a second independent case that starts
an owned, uncancellable close while a lease is held, then invokes service stop
with a 30ms deadline. It requires timed_out without completing or invalidating
the first close, checks the borrowed client remains open, returns the lease,
awaits the original close, and finally stops the service successfully. The
original lease-wait timeout case remains separately registered.

Windows rebuilt and passed application integrations (1.60 seconds); the modified
fixture passed clang-format validation. This covers competing coroutine close
owners on one executor, not arbitrary concurrent pool use across threads.

### Host rollback retries preserve dependency ownership (2026-09-13)

Extended the real-host bind-failure fixture with a required dependency and two
cleanup scenarios. The original slow cleanup retains cleanup_failed and must
not stop its dependency. The new transient scenario fails the dependent's stop
twice, succeeds on its third attempt, then stops the dependency exactly once and
last. Host state must become stopped while run still returns the original socket
bind error, not a later cleanup error. No production behavior was changed here.

Windows full application tests passed (6.17 seconds); Arch ASAN passed (5.07
seconds). This establishes in-budget host retry with controlled managed services.
It does not prove safe host destruction with escaped leases or uncooperative
coroutines after cleanup_failed, nor provide a public post-run cleanup retry API.

### Disabled scope and terminal callback boundaries (2026-09-13)

Source inspection confirms that HTTP client calls with empty trace and metric
sinks return the original transport task, the empty server metric factory returns
no middleware, and the empty Kafka producer sink bypasses the observed coroutine.
Two additional instrumentation tests verify that a sampler without an exporter
does not invoke span or attribute factories, and that reentrant completion cannot
export twice or evaluate attributes after the scope has retired its span.
The updated `test_instrumentation` target built and passed under Arch Clang 22
ASAN (0.07 seconds); the generated AGENTS check also passed. This is source and
boundary-behavior evidence, not proof of historical CPU performance parity or of
all protocol integrations. No production code was changed for these tests.

### Host final settlement includes OTLP work (2026-09-13)

Added owner-thread `try_settle_shutdown()` to the exporter and hub: it cancels
delivery and reports whether the scheduled worker has settled, closing the idle
connection and discarding queued work through the existing abort path. Host final
connection waiting now includes this check within its existing shutdown budget;
remaining telemetry work also prevents reporting `stopped`. Ordinary delivery
failure still does not replace the business result. No request fast path changed.

Arch Clang 22 ASAN builds and the full application/exporter test executables passed
(5.13s / 0.65s). Tests cover a queued worker before dispatch and cancellation during
collector retry, including repeated checks and existing dropped-record semantics.
Formatting and generated AGENTS checks passed. Windows validation and allocation
fault tests for the new query remain pending. This does not solve safe destruction
of every retained service or uncooperative operation after the final budget expires.

### Settlement query allocation and Windows validation (2026-09-13)

Added allocation-failure coverage for `telemetry_hub::try_settle_shutdown()` with
all signals disabled, enabled but idle, and a scheduled worker not yet dispatched.
The first check makes zero allocation attempts (and allocates zero bytes), the
queued case correctly reports unfinished, and repeated checks after event-loop
settlement remain allocation-free. The test does not claim to cover cancellation
of every in-flight transport operation under sustained allocation failure.

Windows Release rebuilt the core plus application, OTLP exporter, instrumentation,
and allocation-test executables; all four passed (6.22s, 1.32s, 0.01s, 0.97s).
Arch Clang 22 ASAN rebuilt and passed the full allocation executable (3.44s).
This closes the preceding entry's Windows and basic query-allocation checks, not
the historical CPU parity or retained-resource destruction requirements.

### Explicit Host cleanup retry (2026-09-13)

Added `application_host::retry_cleanup(milliseconds)` for exclusive owner-thread
use after run returns cleanup_failed. It resumes the same event loop and reuses
the existing reverse-order service/telemetry settlement, without restarting the
application. The original run error remains separate from the cleanup result.
An explicit retry gets its own positive budget and a 20% final-settlement reserve;
an unfinished top-level task is not replaced. The retry coroutine is host-owned.

The existing failed-bind/slow-stop regression now removes the artificial stop
delay after the first failed cleanup, verifies retry reaches stopped and closes
the retained dependency once, then verifies repeated retry causes no new closes
and run cannot restart the application. Full Arch Clang 22 ASAN application tests
passed (5.08s). Windows, retry allocation failures, unexpected event-loop failures,
and real escaped-lease retries remain to be verified. This is a recovery entry
point, not a guarantee of safe destruction with arbitrary uncooperative work.

### Cleanup retry failure boundaries (2026-09-13)

Retry now rejects non-cleanup states before preparing a coroutine. Escaping
allocation, system, and other execution errors restore cleanup_failed rather
than leaving stopping; the owned root task is not discarded by this boundary.
The fake retained-service regression sweeps 16 allocation offsets, verifies that
fault injection actually occurs, and requires a subsequent retry to settle with
one start and one successful release. Invalid negative/zero/maximum budgets
preserve cleanup_failed before any valid retry. This does not exhaust all
allocation sites or inject every possible event-loop exception.

Windows Release core and both relevant executables rebuilt; application and
allocation suites passed (6.23s / 1.66s). Arch Clang 22 ASAN rebuilt both and passed
(5.06s / 3.63s). Formatting and generated AGENTS checks passed. Real retained
leases, externally stopped event loops with unfinished roots, and arbitrary
uncooperative tasks remain outside this evidence; the full goal stays open.

### Current MySQL code revalidated against the isolated 114 database (2026-09-13)

Rebuilt Windows Release `test_application_mysql_live` against the current core and
ran it through an SSH-skill loopback tunnel to the existing dedicated test account
and database. It passed in 2.18s, with ORM compilation explicitly enabled. Coverage
includes authentication, three health probes, default return clearing a session
variable, explicit no-reset return preserving it, three QUIT and three test-owned
KILL CONNECTION recovery cycles, replacement connection identity/session isolation,
and supervised maintenance shutdown while the service still owns a borrowed lease.
ORM checks compare successful/error results across raw, disabled, enabled, and
throwing sinks, including error codes, SQL state, parent identity, and omission of
query/result content from span attributes.

No business database, table, server configuration, or unrelated connection was
modified. Temporary local credentials were removed, and the created tunnel was
stopped with its process confirmed terminal. This refreshes live protocol evidence
after the pool changes; it does not exercise Host retry with an escaped lease,
end-to-end MySQL OTLP reception, historical CPU parity, or MySQL-over-TLS (the
database protocol was protected by the SSH tunnel).

### MySQL service stop retains outstanding leases (2026-09-13)

Found that mysql_service previously returned successful stop after only requesting
pool cancellation, even with a borrowed connection. Added an owner-thread scan of
existing in_use states (`checked_out_count`) with no new acquisition/return counter.
Service stop now waits for outstanding leases within its cancellation/deadline
contract, leaving started ownership intact on failure for a later cleanup retry.
This adds work only to the explicit stop path and does not change raw pool cancel.

Windows core and application/live-MySQL/pool tests rebuilt. All passed: application
6.17s, real isolated 114 database 2.68s, mock pool 0.05s. The real test now requires
30ms timeout while the lease is held, count 1 before return and 0 afterwards, then
successful repeated service stop; prior reset/reconnect/ORM parity assertions remain.
An initial missing explicit async_op import was corrected before the successful build.
Temporary credentials were removed and the tunnel process confirmed terminal.
Arch ASAN for this change and Host-level real leased-service cleanup remain pending;
the caller must still retain the service until all leases have been returned.

### MySQL lease accounting ASAN regression (2026-09-13)

Rebuilt the current lease-stop change under Arch Clang 22 ASAN. Full application,
application integration, allocation, and MySQL pool executables passed (5.07s,
1.45s, 3.60s, 0.65s). The pool protocol fixture now asserts checked_out_count at
initial borrow, ordinary return before reset, replacement borrow after recovery,
and explicit no-reset return. The updated pool fixture also passed Windows Release
(0.05s); formatting and AGENTS synchronization checks passed. The ASAN configuration
still excludes SSL. These checks do not yet exercise a real leased MySQL service
through the entire Host cleanup_failed/retry_cleanup sequence.

### Real MySQL Host retained-lease rollback and retry (2026-09-13)

Added a second live MySQL test using Application auto-configuration and a dependent
managed fixture. The fixture acquires a real lease; an occupied local business port
then forces rollback. Host must return cleanup_failed while the lease is retained.
After returning it, retry_cleanup must reach stopped with no outstanding lease or
waiter. Fixture start and stop each occur once, including after a repeated retry.
The test keeps Host alive until lease return; it does not authorize destroying a
Host while external users still retain its resources.

Windows Release rebuilt the live executable; both live cases passed against the
isolated 114 test database through the SSH-skill tunnel (2.81s total). Temporary
credentials were removed and the tunnel process confirmed terminal. No business
tables or unrelated sessions were changed. Format validation passed. The new Host
case disables telemetry and does not prove enabled-OTLP Host parity, arbitrary
uncooperative cleanup, or historical CPU performance equivalence.

### Unavailable OTLP collector with real MySQL Host rollback (2026-09-13)

The real Host leased-service fixture now runs both disabled and enabled telemetry.
Enabled traces/metrics/logs target a reserved, unlistened loopback collector port;
the test requires all three accepted signal counters and failed_batches to be
positive, with no exported records. Both variants compare the Host run error to
the raw HTTP bind error, retain the lease through cleanup_failed, and settle after
return through retry_cleanup without restarting or double-stopping the fixture.

Windows Release rebuilt; all three live MySQL cases passed against the isolated
114 database (2.66s total). Format check passed. Local temporary credentials were
removed and the tunnel stopped with its process terminal. This verifies observed
collector failure isolation on this rollback path, not successful signal delivery,
historical CPU parity, or every lifecycle path under arbitrary failures.

### Real MySQL Host with receiving OTLP endpoint (2026-09-13)

Added a successful-collector variant to the live Host lease fixture. A local HTTP
receiver on its own joined event-loop thread parses actual OTLP request bodies,
checks each signal's nonempty resource envelope, records receipt of traces,
metrics, and logs, and rejects a payload containing the test password. After Host
cleanup/retry it joins the collector accept loop and checks active connections
reach zero. The same raw bind error, retained-lease, no-restart, and exactly-once
fixture stop assertions remain in place.

Windows Release rebuilt and all four live MySQL cases passed through the isolated
114 tunnel (3.11s). The receiving variant checks positive exported records and
independent HTTP receipt, not solely exporter counters. Credentials were removed,
the tunnel process ended, and formatting passed. This covers resource envelopes
and password omission for these lifecycle records, not every OTLP semantic field,
SQL operation propagation, every sensitive-data encoding, or CPU equivalence.

### Received MySQL lifecycle signal correlation (2026-09-13)

Strengthened the real MySQL Host receiver beyond resource-envelope checks. It now
requires exactly one mysql/default application.service.start INTERNAL span and
one successful start log, with matching nonempty trace and span identities. The
received application.service.operations metric must contain a positive successful
start point for the same service/instance. Assertions inspect parsed wire records
after joining the receiver, rather than relying on exporter statistics alone.

Windows Release rebuilt and all four isolated-114 live cases passed (2.88s).
Formatting passed; temporary credentials and the owned tunnel were cleaned up,
and the tunnel process was terminal. This establishes correlation for service
startup, not automatic observation of every SQL statement or full end-to-end
distributed trace coverage across all supported protocols.

### Specialize bounded HTTP response decoding (2026-09-13)

The HTTP/1 token overload now dispatches directly to a private coroutine template:
bounded clients retain the strict OTLP response decoder, while unlimited clients
instantiate the decoder with a constant zero body limit. This allows the compiler
to remove the bounded checks from unlimited receive loops without duplicating the
parser source or adding a dispatch coroutine frame. The default parser semantics
and public API are unchanged; selection still costs one request-level branch.

Windows Release and Arch Clang ASAN rebuilt the core and both affected test
executables. test_http_disabled_overhead and test_otlp_exporter passed on Windows
(3.00s) and Arch (4.42s). Existing coverage compares raw/disabled allocations and
wire headers and exercises bounded collector response overflow, truncation, and
exact boundaries. Formatting and generated AGENTS checks passed. The first quiet
Windows build exited CL.exe code 1 without a specific diagnostic; a terminal-state
incremental retry with minimal verbosity succeeded. Its initial cause is unknown.

These tests establish compatibility and bounded-response regression coverage,
not historical CPU equivalence or inspection of optimized machine code. The
historical performance requirement remains unproven and the complete lifecycle
and protocol acceptance scope remains open.

### Independently replay process-level performance evidence (2026-09-13)

Fixed the analyzer CLI's inability to read the runner's saved runs format. Replay
now reconstructs process aggregates from raw measurements and ignores the stored
assessment. It rejects duplicate process IDs, missing baseline rounds, mismatched
request counts and malformed pairs. Both fresh sampling and replay share the same
aggregation implementation, including historical disabled-only comparisons.

All 18 benchmark tool tests pass. Independent replay of the archived 32-pair CPU-2
historical report reproduces ratio 1.0226362760644 and interval
[0.9956411711728349, 1.0561769429802457], still inconclusive at zero tolerance.
This repairs evidence reproducibility; it is not a new current-code measurement
and does not satisfy the outstanding historical performance acceptance gate.

### Redis live reconnect coverage and remaining ownership risk (2026-09-13)

Extended the isolated Redis live test with three cycles of closing only its own
borrowed connection, returning it, and requiring a successful health probe through
a reconnected pool client with size one and no queued waiter. The test does not
create keys or stop the Redis server. Windows Release and Arch ASAN compilation
passed. A local Redis/Valkey executable or Docker was not found in the checked
runtime locations, so the added cycles have not yet been executed against a real
server. The existing Linux CI isolated Redis step invokes this executable directly.

Inspection also found a detached coroutine lambda in Redis return_connection's
contended slow path and cancel marking in-use nodes dead before lease return.
These ownership paths still require focused fault/retained-lease tests and a safe
implementation; neither the new compile result nor previous PING success proves
them safe. Server outage recovery, Host readiness and Redis OTLP correlation remain
unverified by this live test.

### Redis contended returns use owned notifications (2026-09-13)

Replaced the temporary capturing detached coroutine in return_connection with an
intrusive notification stored in the stable connection node. Lock contention
reposts that notification; registration increments the existing maintenance wait
group before dispatch and completion decrements it after releasing the lock.
The uncontended return path is unchanged. This removes the detached return frame
and closure lifetime hazard, while adding per-connection notification storage.

Windows Release and Arch ASAN rebuilt successfully. test_application_redis and
test_http_disabled_overhead passed on Windows (1.95s) and Arch (3.93s). These are
existing regression suites, not a deterministic contention-branch proof. Forced
contention, repeated redispatch, cancellation/return races and allocation-failure
tests remain necessary. This change does not solve externally retained leases or
prove historical performance equivalence. No real Redis server was run this turn.

### Deterministic Redis contended-return shutdown test (2026-09-13)

Added redis_return_contention_cases.inc to the allocation regression executable.
The fixture establishes an unauthenticated local TCP connection, holds its lease,
queues another acquisition, then returns the first lease from the allocation hook
used while creating a third pool node under the pool lock. It requires exactly
one additional maintenance registration and an untouched allocation-failure
sentinel during return. Shutdown starts before notification dispatch; both queued
acquisitions must cancel and shutdown/run must settle with zero waiters and owned
maintenance. No production test-access API or external Redis dependency was added.

The focused Windows test passed, followed by the complete allocation regression
executable on Windows Release (1.67s) and Arch ASAN (3.64s). This directly exercises
the previously unproven contended registration plus stop-before-dispatch path.
Repeated redispatch while the lock remains held, normal waiter handoff from this
branch, and external leases surviving application shutdown remain separate gaps.

### Redis contended-return normal handoff (2026-09-13)

The deterministic contention fixture now has a normal-dispatch variant in
addition to stop-before-dispatch. It cancels the later waiter, lets the earlier
waiter complete, and checks that it receives the exact original client pointer
with an open connection before returning it and settling pool shutdown. This
checks usable lease transfer rather than only notification count bookkeeping.

Both focused Windows cases passed. Full allocation regression passed on Windows
Release (1.67s) and Arch ASAN (3.66s); formatting passed. No production changes
were needed for this added assertion. Repeated callback redispatch and external
leases surviving application shutdown remain unverified, as do real Redis server
outages and complete Host/OTLP recovery semantics.

### Redis service retains outstanding lease ownership during stop (2026-09-13)

Pool cancellation now transitions borrowed nodes to retired_in_use rather than
forgetting their ownership. Repeated cancellation preserves that state; returning
a retired lease closes its client and marks it dead instead of republishing idle.
checked_out_count scans node states on the owner thread, avoiding a new counter
on acquisition and ordinary return. Application Redis stop waits for outstanding
leases using cancellation and the supplied deadline, preserving started state on
failure so a later stop can complete after return.

The local wire fixture retains a lease through supervisor shutdown and a 30ms
service-stop timeout, requires count one and a valid lease, returns it, checks
count zero, and then successfully completes service stop. Windows Release and
Arch ASAN rebuilt; Redis service and complete allocation regressions passed
(1.99s Windows, 4.09s Arch). Real Redis and Host-level retained-lease retry remain
unverified. Arbitrary maintenance cancellation still needs a bounded-settlement
audit; these results do not authorize destruction with escaped leases or establish
historical CPU performance equivalence.

### Redis shutdown closes settled idle transports (2026-09-13)

Added a post-shutdown closed-client assertion to both contended-return variants.
The normal-handoff case failed on Windows before the fix: cancel had retired the
idle node but left its client open. After awaiting maintenance completion, cancel
now closes non-null dead-node clients. Retired borrowed nodes are excluded and
remain owned by their lease until return. The additional loop runs only at stop.

After rebuilding, Redis application and full allocation regression suites passed
on Windows Release (1.98s) and Arch ASAN (3.93s). The first Windows core link hit
LNK1104 for the output library; its terminal-state incremental retry succeeded.
The new check establishes local client closure, not remote Redis process recovery
or complete distributed OTLP/Host acceptance. Broader requirements remain open.

### Host retries retained Redis service cleanup (2026-09-13)

Added application_redis_host_cleanup_cases.inc to the Redis application suite.
A custom managed fixture constructs the real Redis service on Host's execution
context and retains a borrowed connection outside its stop callback. An occupied
business port forces rollback. Host must return cleanup_failed with one lease;
after external return, retry_cleanup must reach stopped with no maintenance or
lease ownership. A second retry succeeds without a second service start.

Windows Release and Arch ASAN rebuilt and passed the Redis application suite
(0.41s and 0.40s respectively). Formatting passed. This test uses a loopback TCP
listener with Redis authentication/RESP3 negotiation disabled and OTEL disabled;
it verifies Host/resource lifecycle, not real Redis protocol interoperability,
auto-configuration, dependency-chain ordering or observed collector behavior.
Those broader cases and the zero-performance-regression requirement remain open.

### Redis Host collector-failure isolation (2026-09-13)

The retained-Redis Host fixture now runs with telemetry disabled and with all three
signals enabled against a reserved, non-listening loopback collector port. Both
compare the Host run error with the error from a raw HTTP bind attempt at the same
occupied business port. The observed variant requires positive accepted counters
for traces, metrics and logs, positive failed batches and zero exported records,
then preserves the same retained-lease cleanup/retry and no-restart assertions.

Windows Release and Arch ASAN rebuilt and passed the Redis application suite
(0.51s and 0.47s). Formatting and generated instructions checks passed. This is
collector-failure isolation on a local TCP fixture, not successful OTLP delivery,
real Redis interoperability, all lifecycle paths or performance equivalence.

### MySQL settled idle transport cleanup (2026-09-13)

Added returned-client closure assertions after pool maintenance exits. Three
Windows cases failed before the fix. New synchronous client::close delegates to
transport invalidation, preserving last_error and requiring settled operations.
Pool maintenance closes non-borrowed clients after joining workers; cancel also
closes eligible clients when no worker remains. No request-path logic changed.

Windows Release and Arch ASAN rebuilt; MySQL pool and allocation suites passed
(1.69s and 4.23s). A lease returned while Application stop is already waiting still
needs a direct closure assertion and any necessary final sweep. No fresh real
114-server run or historical performance measurement occurred this turn.

### Real MySQL late-return cleanup during service stop (2026-09-13)

The isolated-114 live test now starts a second service stop while still retaining
the lease, verifies that stop suspends, returns the lease, waits for completion,
and requires the original client to be closed. Before the fix this new assertion
failed while the other three live Host/OTLP cases passed. Service stop now repeats
pool cancellation after waiting for leases, closing clients returned after its
initial cleanup scan. This extra cleanup occurs only on the waiting stop path.

Windows Release rebuilt and all four live MySQL cases passed (3.09s) through the
SSH-skill tunnel. Temporary local credentials were deleted and the owned tunnel
process was confirmed terminal. The existing isolated test database was reused;
no business database or unrelated session was changed. This verifies late return
after supervised maintenance settlement; arbitrary uncooperative workers and
broader cross-platform/performance acceptance remain open.

### Broader post-cleanup regression and independent MySQL build (2026-09-13)

Rebuilt six affected suites from current sources: Application, Application
integrations, OTLP exporter, allocation regression, MySQL pool and Redis
Application. All six passed on Windows Release (11.39s) and Arch Clang ASAN
(11.90s). This refreshes coverage after the latest MySQL service late-return fix.

Verified the independent Windows cache has HTTP=OFF, ORM=OFF, MYSQL=ON; rebuilt
its core and MySQL pool executable, and the pool suite passed (0.06s). The new
transport close API therefore does not require HTTP/observability imports in this
configuration. Eighteen benchmark-tool tests also passed and AGENTS was current.
These checks are not all-platform/all-protocol CI or fresh performance evidence;
macOS, real protocol-wide recovery and zero-regression acceptance remain open.

### Await MongoDB pool close action from Application (2026-09-13)

Application MongoDB stop previously called possibly deferred close and immediately
reported success. Added async_close returning the existing shared-state close
coroutine, with frame creation before publishing close_requested. Service stop
now awaits that action. The synchronous close API remains unchanged. Added tests
for idempotent awaited close, rejected acquisition afterward, and a second close
task completing after the facade is destroyed while retaining shared state.

Windows Release core, MongoDB wire and Application integrations rebuilt; both
suites passed (1.67s). A terminal LNK1104 output-library failure cleared on retry.
Current Arch ASAN excludes MongoDB, so no Linux claim applies. Awaited close is
not a full join barrier: MongoDB waiter timeouts, return helpers, active borrowers,
health cancellation and exception-atomic close still need ownership audits/tests.
This change does not prove complete MongoDB/OTLP lifecycle acceptance.

### MongoDB close preparation and allocation-free waiter dispatch

The close action prepares its slot snapshot, notification storage, and waiter
errors before publishing closure or removing queued borrowers. Preparation
failure leaves their completion flags and queue membership intact for retry.
Waiters now own an intrusive I/O notification node; successful completion no
longer allocates a separate post node after publishing the outcome. The
suspended acquire frame retains that storage through dispatch. This changes
waiting-path storage, not the ordinary idle-connection acquisition path.

The allocation regression sweeps 12 failure offsets with a real queued acquire
behind an in-progress loopback connection. A failed close must retain its waiter;
retry must settle both acquisitions and return connection_closed to the queued
borrower. The sweep also requires at least one successful close, so its range
reaches beyond preparation allocations. Windows Release rebuilt the core and
three affected test executables; Application integrations, allocation/lifecycle,
and MongoDB wire suites passed (3.76 seconds total). clang-format validation and
the generated AGENTS check passed.

This is not a complete MongoDB shutdown barrier or a historical performance
result. Synchronous noexcept close, contended return ownership, allocation safety
of other metadata transitions, pending borrower/maintenance task settlement,
real MongoDB integration and Linux/macOS validation still require work. The
intrusive notification assumes the existing rule that callers do not destroy
pending acquire frames before completion.

### MongoDB single-borrower return notification

Connection return and failed-connection retry now retain a single waiter directly
instead of allocating a vector for a notification that can contain at most one
borrower. Together with the intrusive post node, the uncontended return handoff
and discarded-connection retry notification no longer allocate after metadata
changes. This does not remove the separate allocating coroutine fallback when
the pool metadata lock is contended.

A new loopback OP_MSG hello responder establishes a usable pooled connection.
The regression queues a second acquire, arms allocation failure at offset zero,
and returns the lease. Normal handoff must preserve the exact connection object
and its open transport. The discard variant must notify the queued borrower,
which then observes a pool close before retrying connection creation. Both
variants require no allocation during lease return and no remaining checkout.
This is a controlled wire fixture, not a real MongoDB deployment or recovery
test. Windows Release rebuilt the core and the three affected executables;
Application integration, allocation/lifecycle (98 cases), and MongoDB wire
suites passed in 3.78 seconds. Historical disabled-OTEL CPU parity and the wider
platform/protocol acceptance gates remain unproven.

### MongoDB health-check lease ownership on allocation failure

Health-check candidate storage is reserved before any slot is marked checked
out. Candidates are now pooled_connection owners rather than bare slot pointers;
exception unwinding releases claimed slots. An exception during a probe marks
that lease for discard before propagation, while unprobed leases are returned.
The ordinary acquire path is unchanged by this health-check-only change.

The loopback hello fixture now injects failure both at candidate reservation and
at the first probe-task allocation. Reservation failure must allow immediate
reacquisition of the original open connection. Probe construction failure must
leave zero checked-out connections and remove the discarded slot. Windows
Release rebuilt the core and affected tests; the allocation/lifecycle suite
(99 cases), MongoDB wire and Application integration suites passed in 3.78
seconds. This evidence covers one candidate and construction-time exceptions,
not concurrent shutdown, active-I/O cancellation or multi-candidate probe errors.
Those cases and the full performance/integration acceptance gates remain open.

### MongoDB connection-creation admission after exceptions

Admitted creation attempts now capture exceptions from candidate construction
and connection setup, reacquire the metadata lock, release their creation budget,
and notify a queued retry before rethrowing. Slot publication failure follows
the same completion path; checkout is marked only after insertion succeeds.
This prevents an allocation exception from permanently consuming a creation
permit. It does not add work to idle-slot acquisition.

The loopback regression injects four early allocation failures into acquire,
then uses the same maximum-size-one pool to complete hello and hand off the
connection to a queued borrower. Windows Release rebuilt the core and three
affected executables. Application integrations, allocation/lifecycle (100 cases)
and MongoDB wire tests passed in 3.81 seconds. These tests demonstrate early
exception recovery, not slot-publication fault injection, active handshake
cancellation, multi-threaded contention or real-server recovery. Full lifecycle
and historical disabled-OTEL performance acceptance remain open.

### MongoDB queue admission rollback on task construction failure

Acquire now removes its not-yet-suspended waiter under the existing metadata
guard when timeout-task or dispatch-wrapper construction throws. Snapshots are
refreshed before propagating the exception. A three-offset early allocation
regression requires zero queued waiters after failure, then successfully queues
a replacement borrower and closes/retries the pool until all requests settle.
Windows Release core and affected executables rebuilt (an initial output-library
LNK1104 cleared on terminal retry). Application integrations, allocation/lifecycle
(101 cases), and MongoDB wire suites passed in 3.95 seconds.

This catches exceptions escaping task construction only. Existing bare spawn
still terminates for exceptions inside detached dispatch or execution; replacing
MongoDB internal detached work with owned, failure-propagating tasks remains a
required lifecycle item. The test deliberately does not claim dispatch-failure
coverage. Historical performance and full real-service/platform gates remain open.

### MongoDB acquire owns and joins its timeout operation

Queued acquire now owns its timeout task and caller-owned dispatch node. It
cancels and joins the task at final suspension before returning a borrowed
connection or an error. The private completion awaiter installs a continuation
without resuming an already-started I/O operation; this assumes the pool's
owning I/O thread. Timer setup/execution exceptions are captured, remove the
queued waiter under the metadata lock and propagate to the borrower. The
timeout path no longer uses bare spawn or allocates a detached dispatch wrapper.
Its creation-failure regression consequently has two early allocation points
instead of the former three.

A new execution-phase allocation fault test dispatches the queued timer with
allocation failure armed, verifies bad_alloc reaches acquire without terminating,
then queues a replacement and hands off the same connection. Existing immediate
close and return tests also exercise cancellation before timeout dispatch.
Windows Release rebuilt the core and affected executables; Application
integration, allocation/lifecycle (102 cases), and MongoDB wire suites passed
in 3.84 seconds. The other contended return/cancellation/close detached paths,
multi-threaded cancellation, full service shutdown barrier, real-server recovery
and historical disabled-OTEL CPU gates remain open.

### MongoDB owned timer behavior after dispatch

The wire fixture additionally exercises a real 30 ms queue timeout (pool_exhausted),
caller cancellation after timer dispatch (operation_cancelled), and connection
handoff after timer dispatch. Failed borrowers leave zero waiters and preserve
the existing lease; a replacement borrower receives the exact same open
connection. Every variant finishes pool closure with zero checkouts. The
allocation-free lease return assertion also runs with an active waiting timer.
Windows Release rebuilt the affected executable; all three related suites passed
in 3.87 seconds, with format and generated-document checks passing. These are
single-owner-thread cases, not cross-thread cancellation race coverage or a
real MongoDB deployment. The wider lifecycle and performance gates stay open.

### MongoDB cancellation resolves on the owning I/O thread

The stop callback now only cancels the owned timeout token. The timeout task
interprets the caller stop token and resolves queue metadata under the pool lock
on its I/O thread. The former finish_waiter synchronous/coroutine fallback pair
has been removed, eliminating its detached cancellation task and cross-thread
error-string construction. Completed handoff/close outcomes are not overwritten
when the timer resumes after another completion has already won.

The new foreign-thread regression requests cancellation from a joined jthread
with allocation failure armed in that thread. It requires no allocation in the
request, operation_cancelled at acquire, no residual waiter, and successful
replacement handoff of the original connection. Windows core and affected tests
rebuilt; Application integrations, allocation/lifecycle and MongoDB wire suites
passed in 3.93 seconds. This is an ordered foreign-thread cancellation test,
not a concurrent handoff/timeout race stress test or ThreadSanitizer result.
Contended return and synchronous close ownership, full service shutdown, real
integration, cross-platform and historical performance gates remain open.

### MongoDB concurrent cancellation and handoff regression

The loopback fixture now starts a cancellation thread against an active waiting
timer while the owner polls and returns its lease. Thirty-two repetitions accept
either operation_cancelled or successful handoff, then require zero outstanding
waiters/checkouts and immediate reuse of the same open connection. Allocation
failure is armed separately in the cancellation thread and during lease return;
both actions must remain allocation-free. The test does not require both race
outcomes to occur, so it is bounded contention coverage rather than exhaustive
scheduling or a race-detector proof.

Windows Release rebuilt the test executable and passed Application integration,
allocation/lifecycle and MongoDB wire suites in 3.91 seconds. Existing ordered
cancellation and ordered handoff tests remain alongside the competing schedule.
Contended return ownership, full close barriers, other platforms, real services
and historical disabled-OTEL performance still require acceptance evidence.

### MongoDB deferred return ownership and close join

Contended lease return now registers an intrusive slot-local callback instead
of spawning an unowned coroutine. The queued slot retains itself and pool state
until dispatch; a still-contended callback requeues the same node. A wait group
tracks only deferred returns, and async_close joins registered returns after
publishing closure. The ordinary uncontended return does not touch that group.
The I/O context must run until cleanup completes; abandoning its pending queue
is not a supported release mechanism for these retained objects.

A test allocation hook returns a live lease while health_check holds the pool
metadata lock. It requires no allocation and verifies both deferred handoff and
close-before-dispatch. In the latter case async_close must initially remain
pending, then complete after the return is processed; the borrower sees
connection_closed and checkout count reaches zero. Windows rebuilt the core and
three affected test executables; suites passed in 3.91 seconds. Repeated lock
contention during redispatch, foreign-thread returns, external outstanding
leases, synchronous close failure handling and full integration/performance
acceptance remain unproven.

### MongoDB deferred return survives repeated lock contention

The contended-return fixture now starts the borrower timer, then dispatches
the return callback during three subsequent health-check metadata critical
sections. Each dispatch must process work without allocating, leave the single
checkout and waiter intact, and keep the borrower suspended. After those
requeues, both normal handoff and close-before-final-dispatch still settle.
This directly exercises the callback's still-locked branch using a test-only
allocation hook; it is not a production recommendation to nest event polling.
Windows rebuilt the allocation test and passed the three related suites in
3.93 seconds. Full external lease shutdown, synchronous close safety, other
platforms and historical disabled-OTEL performance remain unverified.

### Arch MongoDB ASAN validation exposes command-timeout hang

The existing /root/cnetmod-otel-asan cache now enables MongoDB; it remains
Clang 22.1.8/libc++, epoll, system allocator, RelWithDebInfo with AddressSanitizer
and SSL disabled. Core and all three selected test executables compiled and
linked successfully. Application integrations and MongoDB wire passed, but the
allocation/lifecycle suite aborted in the first MongoDB close-preparation case.
A focused rerun reproduced it: connecting_done=false, queued_done=true,
waiters=0 after two seconds. This is a failed validation, not an ASAN pass.

The pending handshake uses execute_command/when_all. Its watchdog closes the
socket on expiry, while read_exact uses the non-cancellable async_read overload.
On this epoll run the pending read did not settle after close, so when_all could
not finish. Command I/O must use explicit cancellation and settle before socket
state destruction; increasing the test wait is not a fix. The focused diagnostic
is retained in the test. No real MongoDB server or TLS was involved. Linux
MongoDB acceptance is now explicitly failing until this path is repaired.

### MongoDB epoll command timeout now cancels pending I/O

Connection command reads/writes now use a stable connection-owned cancel_token,
including the SSL overloads. The watchdog requests deadline cancellation rather
than closing a descriptor while an epoll read remains pending. Explicit command
cancellation signals the same token; command setup resets it before publishing
the active flag. Existing command cleanup closes transport after the I/O result.
The reproduced silent-hello hang no longer occurs.

Arch Clang 22.1.8/libc++/epoll ASAN rebuilt core and three selected tests; all
passed in 5.61 seconds, including the previously failing MongoDB close-fault
case and the queued timeout, foreign cancellation, concurrent handoff and
contended return fixtures. Windows Release rebuilt core (SSL branch enabled)
and the same executables, passing all three suites in 3.90 seconds. Formatting
passed. This supersedes the preceding failed Linux run for these tests only.
Actual TLS traffic, explicit active-command cancellation races, watchdog setup
exceptions, outstanding external leases, real brokers/databases, macOS and
historical disabled-OTEL performance still need direct evidence. Cancellable
I/O has necessary protocol-level work; these functional tests do not establish
its historical CPU cost or the overall no-regression requirement.

### MongoDB active hello cancellation and token reuse

A dedicated wire test connects to a silent loopback endpoint, waits for an open
transport with hello still pending, and requests cancel_active_command from a
separate joined thread. It requires operation_cancelled and a closed client
within two seconds with command_timeout either zero or 30 seconds. Each mode
repeats twice using the same connection object, proving the cancelled token does
not immediately poison the next hello attempt. Both attempts intentionally
cancel; this does not prove successful authenticated reconnect or TLS behavior.
Windows rebuilt and passed the six-case wire suite (0.04 seconds); Arch
Clang/libc++ epoll ASAN rebuilt and passed it (0.10 seconds). Format and AGENTS
checks passed. Full shutdown, real integration and performance gates remain open.

### MongoDB command/watchdog exception settlement

The command branch cancels its watchdog when execution throws. The watchdog
cancels command I/O when timer setup throws or returns a non-cancellation error.
The command guard also resets active-command state during exception unwinding.
A 48-offset allocation sweep on a connected client uses a 30-second watchdog
and a two-second settlement limit, cancelling pending command I/O and requiring
error completion before discarding the lease. It accepts bad_alloc or the
specific system_error(not_enough_memory) emitted by epoll registration; other
system errors fail. The initial Linux assertion omitted the latter mapping;
after correcting it, all three related suites passed on Arch ASAN (5.57 seconds)
and Windows Release (3.94 seconds). This bounded sweep is not exhaustive
watchdog-state or TLS coverage, nor a performance or complete shutdown proof.

### MongoDB warmup rejects closed admission

warm_up now rejects both close_requested and closed under the metadata lock,
instead of treating closed as successful readiness. A zero-minimum-size pool
test covers an unresumed close task (request already published) and a completed
close; each warmup must immediately return connection_closed without creating
slots. Arch ASAN rebuilt core and three suites and passed in 6.00 seconds;
Windows rebuilt the same affected targets and passed the three suites. This
prevents false pool warmup success but does not implement reopening or full
Application recovery after stop. Those lifecycle and performance gates remain open.

### Cross-component regression after MongoDB lifecycle changes

Rebuilt seven selected targets against the current core on both platforms:
test_application, test_application_integrations, test_otlp_exporter,
test_http_disabled_overhead, test_mysql_pool, test_application_redis and
test_mongodb_wire. All seven passed on Windows Release in 12.00 seconds and
Arch Clang 22/libc++ epoll ASAN in 12.57 seconds. The Arch configuration now
includes MongoDB, unlike the earlier pre-MongoDB ASAN evidence.

This regression covers the selected lifecycle, allocation-fault/disabled-path,
OTLP receiver and pool fixtures; it does not execute the live 114 MySQL test,
real Redis/MongoDB/broker containers, TLS runtime matrix, macOS or historical
CPU benchmarks. Passing these tests therefore does not close those acceptance
gates or prove the complete no-performance-regression requirement.

### MongoDB streaming consumer exception settlement

The streaming command boundary retires the connection and resets command state
before rethrowing a consumer exception. Loopback OP_MSG fixtures now cover both
an immediate exception and an exception after awaiting a timer, with moreToCome
set so the connection cannot safely be reused. Both require the original error
to reach the caller, a closed transport, and no checked-out lease after cleanup.
Windows Release rebuilt the affected test target and passed the three selected
MongoDB/allocation/Application integration suites in 3.89 seconds; Arch
Clang/libc++ epoll ASAN rebuilt and passed the same suites in 5.65 seconds.
Formatting and generated AGENTS checks passed. This does not establish active
borrower shutdown, MongoDB TLS behavior, real MongoDB integration, or CPU parity.

### Existing 114 MySQL live suite rerun (2026-09-13)

The existing Windows executable passed all four live MySQL cases through an
owned loopback SSH tunnel in 2.85 seconds, using the existing isolated test
database. Cases cover authentication/health, connection recovery, retained-lease
cleanup, collector failure and three-signal export during rollback. This run did
not rebuild the executable and is therefore not evidence that every current
source change was included. Temporary local credentials and the owned tunnel
were removed afterwards; business databases were not modified.

### MongoDB pool closure respects borrowed transport ownership

Pool close no longer destroys every slot's transport indiscriminately. Idle
slots close immediately; checked-out slots receive command cancellation and
remain stale until their owner returns them. This allows pending command I/O
to unwind before its socket/SSL state is destroyed. No checkout fast-path work
or telemetry dependency was added by this change.

Two loopback tests cover a borrowed connection without an active command and a
borrowed pending ping. The former retains an open transport and checked-out
count until return; the latter requires operation_cancelled and a closed
transport after command completion, before lease cleanup. The acquired-lease
fixture now accepts a scenario callback rather than a streaming-only flag.
Windows Release rebuilt core and the three related test targets, passing in
3.99 seconds. Arch Clang/libc++ epoll ASAN rebuilt and passed in 5.64 seconds.
Format and AGENTS checks passed. Application stop still needs a deadline-aware
borrower barrier; these tests do not prove that barrier, TLS runtime behavior,
or historical CPU parity.

### MongoDB service waits for outstanding leases within its stop budget

After closing pool admission and requesting maintenance cancellation,
mongodb_service::stop now waits for checked-out leases before reporting success.
The wait uses the existing operation deadline and cancellation token. Timeout
and cancellation retain unfinished service state so stop can be retried after
the caller releases its lease. Timer cancellation is normalized to the same
generic operation_canceled error used by pre-wait cancellation; other timer
errors remain unchanged. Only shutdown gains this wait, not normal acquisition.

An independently organized loopback fixture starts the real adapter and its
supervised maintenance task, authenticates a connection using a synthetic hello,
and retains a lease across stop. Three cases cover return during waiting,
deadline expiry, and cancellation during waiting. Failed stops preserve the
lease; release followed by another stop succeeds, with zero pool slots and
checked-out leases, and supervisor join succeeds. Windows rebuilt core and the
integration target, then passed the suite in 1.64 seconds; Arch Clang/libc++
epoll ASAN rebuilt and passed in 1.46 seconds. Format and AGENTS checks passed.

This bounds the lease-wait phase, not arbitrary contention inside async_close
or uncooperative tasks. In-flight connection creation, full Host ownership
retention/retry, restart of a closed pool, TLS and real MongoDB remain separate
acceptance gaps. No historical CPU performance claim follows from these tests.

### MongoDB cleanup state prevents false recovery admission

The adapter now publishes cleanup_pending before stop begins, retaining it
through exceptions, cancellation and deadline failure. While cleanup remains
pending, start returns operation_in_progress instead of treating the old
started flag as success, and probe returns stopping without touching the pool.
Successful cleanup clears the pending state. The timeout and cancellation
fixtures now explicitly attempt restart and probe before releasing the lease,
then verify cleanup can still be retried successfully.

Windows and Arch epoll ASAN rebuilt the changed module and integration target;
the integration suites passed in 1.65 and 1.47 seconds respectively. Formatting
and AGENTS checks passed. This prevents false success during cleanup, but does
not yet rebuild/reopen a fully closed pool. Safe pool replacement also needs
settlement of creation attempts and maintenance ownership; those requirements
remain open alongside full Host recovery and disabled-OTEL CPU parity.

### MongoDB service cleanup accounts for connection creation

The pool exposes connecting_count from a snapshot updated only at connection
attempt admission and settlement. Existing idle checkout/return snapshot
refreshes do not write this counter. Service stop waits for both outstanding
leases and admitted connection attempts, retaining cleanup_pending when its
deadline expires before either has settled.

A loopback test retains an acquire against a listener that withholds hello.
With zero checked-out leases and one creating connection, an expired stop must
return timed_out rather than success. After the original connection attempt
fails and its count reaches zero, cleanup retry succeeds with no pool slots.
Windows and Arch Clang/libc++ epoll ASAN rebuilt core and the integration target,
passing in 1.75 and 1.55 seconds. Formatting and AGENTS checks passed.

Pool close still does not actively cancel candidate handshakes; this case uses
their configured timeout while preserving ownership. Active candidate
cancellation, safe restart, maintenance settlement and Host-level recovery
remain open. This is not an end-to-end shutdown or CPU-parity completion claim.

### Disabled-OTEL performance gate includes latency evidence

The analyzer now validates p50/p99 observations and computes paired bootstrap
intervals for their disabled/raw ratios, using the same process-pair aggregation
as throughput for multi-process reports. These are geometric means of reported
round quantiles, not percentiles of pooled requests, and intervals are per
metric rather than a joint 95% region. Missing latency makes the combined gate
inconclusive; a clear latency regression overrides a throughput improvement.
Both runner and replay CLI exit on combined_assessment. The original assessment
field remains throughput-only for explicitly labeled historical comparisons.
Default permitted throughput loss and latency increase remain zero.

All 21 analyzer/runner tests pass, including missing latency, invalid quantiles,
historical process aggregation, tail regression despite throughput gain, and
CLI propagation of the combined result. Replaying the two existing 32-pair
historical reports remains inconclusive: equal-workload p99 ratio 1.01561 with
interval [0.97755, 1.05510], CPU-2 p99 ratio 0.98308 with interval
[0.93861, 1.03017]. Their throughput intervals also cross 1. No new benchmark
execution or current-binary performance pass is claimed. Original raw evidence
files were preserved unchanged.

### Fresh current-source historical HTTP comparison

Rebuilt bench_http_disabled in cmake-build-otel-parity and the historical raw
driver at revision 85fbba254fd2b5c69e17c53676d3e9a971bd0881. The historical
worktree has no tracked modifications. Compared protocol/dependency enable
switches, allocator option, Release flags and generator settings with no
differences in those selected cache entries. The scope remains HTTP/ORM enabled,
TLS and other protocols disabled, Windows Release, not an all-protocol baseline.

Completed a preselected 32 alternating process pairs with client CPU 2 binding.
The new report is testing/bench/http-observation-windows-historical-current-20260913.json.
Both executable hashes still match the report after measurement. Throughput
ratio 0.99748 has interval [0.97401, 1.02480], p50 ratio 0.99750 has interval
[0.96896, 1.01899], and p99 ratio 0.99700 has interval [0.95644, 1.03934]. The
zero-margin combined result is inconclusive, not accepted as a pass. No batches
were discarded or appended in response to their result. Python-peer scheduling,
process correlation and other previously documented limitations remain. This
fresh build/result replaces stale-binary inference, not the outstanding need
for stronger isolated performance evidence and broader workload coverage.

### Disabled HTTP cancellation during response wait

A new allocation fixture sends real HTTP requests to a separate loopback peer
thread, which records complete headers but withholds responses. Once each
request reaches that boundary, the owning client thread cancels its token and
waits for request completion. Warmed raw and empty-sink decorated paths return
the same nonzero error, with equal client-thread allocation calls and bytes.
A heap-backed parent tracestate is supplied, yet neither traceparent nor
tracestate appears on the wire. The peer observes connection termination and
both owned tasks complete. No production HTTP implementation changed.

Windows Release rebuilt and passed the full disabled-overhead suite in 2.34
seconds (112 cases); Arch Clang/libc++ epoll ASAN rebuilt and passed in 4.13
seconds. Formatting passed. This extends current-build allocation/behavior
parity to in-flight cancellation, not historical CPU parity, TLS, streaming,
all cancellation races or allocations on other threads.

### MongoDB warmup requires established usable connections

Warmup no longer counts reserved creation attempts as completed connections.
It counts open non-stale slots, creates within available admission capacity,
and asynchronously rechecks pending attempts when creation capacity is occupied.
If unusable retained slots consume capacity and no creation is pending, it
returns pool_exhausted rather than spinning or reporting success. This logic
is limited to warmup and does not change the normal checkout fast path.

The adapter fixture now begins checkout before minimum-size-one startup,
asserting startup remains pending until the shared hello succeeds. The silent
peer fixture also starts warmup while an attempt is pending, then verifies pool
closure makes warmup fail with connection_closed. Windows Release rebuilt and
passed the integration suite in 1.81 seconds; Arch Clang/libc++ epoll ASAN rebuilt
and passed in 1.55 seconds. Format and AGENTS checks passed. Warmup still lacks
an explicit cancellation/deadline parameter; active attempt cancellation and
complete service restart remain open, as does historical CPU performance parity.

### Current linked regression and protocol-free module builds

After the MongoDB interface/lifecycle changes, rebuilt seven selected targets
against current core: application, application_integrations, otlp_exporter,
http_disabled_overhead, mysql_pool, application_redis and mongodb_wire. All
passed on Windows Release in 12.17 seconds and Arch epoll ASAN in 12.60 seconds.

Separately rebuilt cnetmod_core with all protocol switches off in the existing
Windows protocol-free-verify and Arch protocol-free-validation caches. Rebuilt
and passed instrumentation, metric_aggregation, application_result, deadline
and task_group tests in both configurations (0.07 / 0.01 seconds). The Arch
protocol-free build is Release, not the ASAN cache; its LevelDB/LZ4 dependency
options remain enabled. It reported an unused get_socket_family function in
epoll_async_op.cpp, so this is not zero-warning evidence. AGENTS check passed.
These checks establish the selected current module graphs and tests, not all
protocol subsets, all tests, macOS/CI, real external integrations or performance
equivalence. Those wider acceptance requirements remain open.

### MongoDB adapter recreates a pool for a new service run

The adapter owns its current pool through shared ownership and retains original
configuration for reconstruction. Each supervised maintenance operation and its
stop callback capture the pool/token/source for that run, not this. Stop waits
for maintenance state to become stopped/failed as well as leases and connection
attempts; only successful cleanup enables pool replacement at the next start.
Replacement allocation completes before publishing the new pool. Normal pool
access borrows a reference and does not copy a shared_ptr per operation. The
public contract now warns that pool references must not cross stop/restart.

The three shutdown fixtures each perform two additional start/hello/acquire/
stop cycles after cleanup retry, verifying a new pool, usable connection and
successful supervision cleanup. Windows and Arch epoll ASAN rebuilt production
changes and integration tests, passing in 1.89 and 1.57 seconds. The subsequent
interface edit only adds the reference-lifetime comment. Format and AGENTS
checks passed. This covers adapter restart with a synthetic plaintext peer,
not arbitrary external references, concurrent lifecycle calls, allocation
failure during restart, real MongoDB or Host recovery-budget behavior. Startup
cancellation and active candidate cancellation remain open, as do other full
integration and historical performance gates.

### MongoDB late startup cancellation rejects maintenance admission

After successful warmup, the adapter rechecks cancellation and deadline before
registering maintenance. A late rejection attempts stop with the original
budget and returns the startup cancellation/timeout when cleanup returns
normally, leaving cleanup_pending if retained leases prevented completion.
This does not yet contain exceptions thrown by that cleanup or actively abort
the handshake when startup is cancelled.

The loopback peer cancels startup after receiving hello, then returns a valid
hello. The pending startup must return operation_canceled, with no maintenance
registration. An externally retained lease remains owned until release; explicit
cleanup then succeeds with no pool slots. Windows and Arch epoll ASAN rebuilt
and passed the integration suites in 1.89 and 1.58 seconds. Format and AGENTS
checks passed. Deadline-after-handshake, startup cleanup allocation failure,
active cancellation and full Host recovery still require further evidence.

### MongoDB startup has a unified failure-cleanup boundary

Public start now separates admission checks from start_runtime. A failed result
or propagated exception from pool reconstruction, warmup or supervision enters
stop before returning/rethrowing the original failure. Cleanup exceptions do
not replace that original failure and leave cleanup_pending set, preventing
new startup until explicit cleanup retry. Admission rejection before runtime
work still returns immediately. This adds a startup-only coroutine boundary,
not work on database query/checkout paths.

The new fixture completes a plaintext hello but uses an already-stopped
supervisor, forcing maintenance registration rejection. It verifies the original
invalid_argument, no maintenance entry, zero idle/checked-out connections and
rejection of another checkout. Existing late-cancellation and restart cases
also pass. Windows and Arch epoll ASAN rebuilt and passed the integration
suites in 1.91 and 1.58 seconds; formatting passed. Arbitrary startup allocation
faults, Host retention after failed partial startup, immediate handshake
cancellation and full performance equivalence remain unproven.

### MongoDB startup/reconstruction allocation fault sweep

An isolated test include exercises 32 synchronous allocation positions in first
startup and the same 32 positions after a successful start/stop, where the next
start reconstructs the pool. Each operation task is constructed before injection;
fault injection is disabled before event-loop dispatch. Both rejected and
successful positions must occur. Every iteration then requires successful
cleanup, zero leases/creation attempts, another successful start/stop, and a
successful supervisor join. The pool minimum is zero: this specifically tests
runtime preparation and supervision, not wire authentication allocations.

Windows Release rebuilt and passed the complete disabled-overhead suite in
4.29 seconds; Arch Clang/libc++ epoll ASAN rebuilt and passed in 4.28 seconds.
Formatting and AGENTS checks passed. This bounded single-failure sweep does
not prove sustained low-memory recovery, active dispatch fault coverage, Host
partial-start ownership or historical no-overhead equivalence.

### MongoDB connection-attempt ownership regression verification

The current connection guard rejects overlapping connect calls before close()
can touch the first operation's transport. It retires incomplete setup on
failure or exception and releases the admission flag on exit. The wire test
attempts reentry while hello is pending, verifies protocol_error without
closing the original transport, then cancels and settles that original attempt.

The previously running Arch build was polled to terminal success; the Windows
test targets also built successfully. Both test_mongodb_wire and the complete
test_http_disabled_overhead suite passed: Windows Release 4.34 seconds total,
Arch epoll ASAN 4.36 seconds total. AGENTS synchronization passed. These tests
do not establish cancellation across DNS/TCP/TLS/authentication, historical CPU
performance equivalence, or real MongoDB interoperability. No remote database
modification was performed during this verification.

### MongoDB startup error identity

Application warmup errors no longer all become connection_refused. The private
adapter category cnetmod.mongodb preserves the protocol enum as value + 1 (the
protocol enum has no success value); cancellation and timeout use their generic
lifecycle codes. Fixed numeric messages exclude protocol diagnostic text.
Common error conditions remain comparable without losing the domain value.
This conversion only runs on failed service startup, not query/checkout paths.

New regression coverage starts with an invalid host and a closed pool, checks
distinct domain values/conditions, fixed safe messages, empty pool state and
no maintenance registration. Windows and Arch ASAN integration suites rebuilt
and passed. Windows required a retry after a terminal LNK1104 library-open
failure. Formatting and generated AGENTS checks passed. Real server auth/TLS
failure coverage and cancellation throughout connection setup remain open.

### MongoDB silent-hello startup timeout and retry

A separate startup-timeout fixture accepts a real loopback TCP connection,
reads the MongoDB request header and deliberately withholds the reply. With a
50ms command timeout and a distinct 3s service deadline, startup must return
generic timed_out without cancelling the service token. The pool must have zero
slots, checked-out leases and connecting attempts, with no maintenance task.
The same service repeats the attempt, proving failed startup cleanup permits
pool reconstruction and a second connection attempt rather than rejecting it
as cleanup_pending. Both attempts deliberately time out; this is not evidence
of recovery to a healthy real MongoDB server.

Windows Release and Arch epoll ASAN rebuilt and passed the complete Application
integration executable in 2.03s and 1.66s respectively. AGENTS synchronization
passed. This fixture validates command-timeout propagation, not active service
cancellation during DNS/TCP/TLS setup or full process shutdown bounds.

### MongoDB health-probe admission

The service probe rejects precancelled and already-expired work before calling
pool health_check. It returns down with the corresponding generic error, leaves
idle slots intact, and preserves stopping for cleanup-pending services. Empty
started pools now report unavailable rather than the contradictory available
message. These checks are confined to managed health probes.

Existing loopback restart cases now exercise both rejection paths while a real
authenticated-by-hello idle slot exists. Probe tasks complete synchronously,
keep one idle slot and zero checkouts, and subsequent borrowing succeeds.
Windows Release and Arch epoll ASAN rebuilt and passed the complete integration
suite in 2.01s and 1.69s. AGENTS remains synchronized. This does not implement
in-flight probe cancellation: the pool health_check still lacks a token and
deadline, and successful health must ultimately come from a bounded ping.

### MongoDB timeout peer-side retirement evidence

The silent-hello startup regression now drains the remaining request on the
accepted peer after startup reports timed_out. It requires EOF or an explicit
connection reset/abort, within the existing bounded settlement loop, on both
attempts. Thus empty pool counters alone no longer satisfy this fixture: a
transport left open would leave the peer read pending and fail the test.
Windows Release rebuilt and passed the integration suite in 2.04s. The initial
Arch run rejected the framework end_of_file representation of EOF; inspection
of epoll async_read confirmed this contract. The assertion now explicitly
accepts it in addition to zero bytes and reset/abort, not arbitrary I/O errors.
After rebuilding, Windows passed again in 2.04s and Arch ASAN in 1.67s.
Formatting passed.

Cancellation design inspection confirms cancel_token owns a single operation
registration and legacy callbacks may resume waiters inline. A future active
probe bridge must use distinct operation tokens and join queued notification
work before releasing its connection; a direct callback chain is not proven
safe. This turn changes test coverage only, not the production query path.

### Owner-thread cancellable MongoDB ping

connection::ping(cancel_token&) is a separate opt-in overload. Its frame owns
a raw post_node cancellation bridge; foreign-thread notification only queues
work, and the owner loop calls cancel_active_command. Completion unregisters
normal notifications or awaits an already queued notification before returning.
Protocol exceptions also pass through this notification join before rethrow.
The token is exclusive to the operation and must outlive it; calls are confined
to the connection owner loop. The old ping() and command implementations are
unchanged, so they do not instantiate this bridge.

Loopback restart regressions exercise precancelled ping (connection remains
open) and cross-thread cancellation of a suspended ping (cancelled result,
closed connection, no pending token registration) through repeated service
reconstruction. Windows Release and Arch epoll ASAN rebuilt and passed the
integration suite in 2.03s and 1.70s. This is not yet wired into service probe:
pool acquisition cancellation, probe deadlines, healthy ping completion races,
allocation fault injection and real TLS still need coverage. Historical CPU
equivalence remains unproven.

### Cancellable pool checks wired into managed health

MongoDB service probes now run actual cancellable pool PINGs inside their
service deadline, preserve the resulting error, and discard failed connections.
Idle candidates are lease-owned across every exit. No idle candidate produces
pool_exhausted rather than a success inferred from pool size. The legacy void
health_check keeps its all-candidate maintenance behavior through a compile-time
specialization; the public forwarding function adds no wrapper coroutine.

An initial runtime-wrapper version changed allocation-failure locations on
Windows and failed two existing pool tests. Replacing that extra frame with
compile-time specialization restored both tests without relaxing assertions.
Current Windows Release integration and allocation suites pass in 6.35s total;
Arch epoll ASAN passes both in 6.03s. Managed probe regressions cover a silent
peer, deadline cancellation and foreign-thread cancellation after restart,
discarded slots, released token registration, and successful service stop.

Remaining gaps include cancellation while waiting for the pool mutex, active
connect cancellation, meaningful probes for zero-minimum/full-busy pools,
successful PING races, fault injection in the new bridge and real-service
recovery. This does not establish historical CPU performance equivalence.

### Successful managed MongoDB PING and token retirement

A separate health-probe fixture reads and decodes a real loopback OP_MSG ping,
responds with ok and matching responseTo, and requires managed health up with
no error, no token registration and one reusable idle slot. After completion it
cancels that old token, drives the loop and repeats a successful ping on the
same connection. Each existing service restart scenario runs this pair before
its failure-path tests. This covers normal notification retirement and reuse,
not cancellation racing simultaneously with response delivery.

Windows Release and Arch epoll ASAN rebuilt and passed the complete integration
executable in 2.05s and 1.67s. Formatting and AGENTS checks passed. Production
code is unchanged this turn; no CPU equivalence claim follows from these tests.

### Cancellable PING allocation unwind

A new isolated 48-position allocation sweep constructs the cancellable PING
task before injection, then injects during its initial execution. Remaining
suspended requests are cancelled with allocation disabled, joined, and checked
for retired registration and transport. Token reset/cancel after completion
must remain harmless. The initial Windows run found 22 positions where the
transport stayed open after an exception, despite notification retirement.

The cancellable overload now closes the uncertain transport after joining its
notification and before rethrowing the original exception. Its success path
and legacy no-token PING are unchanged. Windows Release integration and full
allocation suites pass in 6.37s; Arch epoll ASAN passes in 5.99s. Formatting
and AGENTS checks passed. This bounded synchronous sweep does not cover
arbitrary response parsing allocations, sustained OOM or concurrent token reuse.

### Post-cancellable-probe cross-component regression

After the PING bridge, pool health and allocation-unwind changes, seven test
targets were rebuilt on Windows Release and Arch epoll ASAN: Application,
Application integrations, OTLP exporter, disabled-overhead/allocation faults,
MySQL pool, Application Redis and MongoDB wire. All seven executables passed
on each platform: Windows 14.48s total, Arch 12.92s. This refreshes evidence for
shared lifecycle behavior, but does not substitute for real service or complete
platform/protocol matrix validation.

Inspection of async_mutex confirms lock() has no cancellation contract or
waiter-removal API. Pool metadata locks are not held across database I/O, but
an asynchronously queued probe cannot currently leave that lock queue on its
deadline. A correct cancellable-lock implementation must arbitrate ownership
handoff against waiter removal and preserve the legacy lock fast path; neither
timeout wrappers alone nor a green PING test prove that contract.

### Late cancellation cannot target the next managed PING

The successful-probe fixture now retains both operation tokens. It completes
the first PING, starts the second on the same connection, asserts that the
second is suspended, and cancels the first token from another thread. The
second must still receive its matching reply and report up with its lease
returned. This replaces the weaker cancellation-while-idle check, covering a
deterministic reuse boundary rather than relying on probabilistic scheduling.
Windows Release and Arch epoll ASAN rebuilt and passed the integration suite
in 2.08s and 1.72s. Formatting passed. Cancellation concurrent with the same
operation's response, pool-lock cancellation and full connect cancellation
remain separate requirements.

### Opt-in cancellable MongoDB connection establishment

connection::connect(options, cancel_token&) now owns a transport token and a
joined owner-loop notification bridge. The cancellable implementation supplies
that transport token to happy-eyeballs DNS/TCP and TLS handshake, while the
notification also cancels an active hello/authentication command. Admission is
checked before work and cancellation is checked between setup stages. The
legacy overload forwards directly to its compile-time noncancellable variant,
without constructing a cancellation bridge. Both cancellable PING and connect
retain their admission flags while a queued notification is being joined.

The wire regression exercises legacy active-command cancellation and the new
external-token connection cancellation during hello, with command timeout zero
and 30s, plus overlap rejection and no pending token on completion. Windows
Release wire/integration/allocation suites pass in 6.45s; Arch epoll ASAN passes
in 6.08s. Formatting passed. Windows linking needed terminal-failure retries
for LNK1104. DNS/TCP-phase races, real TLS/authentication cancellation, new
connect-frame fault injection and pool/Application adoption remain unverified
or unfinished; the new API alone does not close those requirements.

### Early cancellable connection admission

The wire suite now exercises a precancelled connect (synchronous rejection)
and a connect cancelled from another thread immediately after its first resume,
before driving the owner loop. Both use a loopback listener and disabled command
timeout, require operation_cancelled, a closed connection and no pending token,
then reset/cancel the completed token and drive the loop again. The same client
is reused across both cases. Windows Release rebuilt and passed the wire suite
in 0.04s; Arch epoll ASAN passed in 0.10s. Formatting and AGENTS checks passed.
This provides early-admission/transport progress evidence, not deterministic
coverage of an external DNS resolver or every TCP/TLS completion ordering.

### Pool closure cancels admitted connection attempts

Each create_connection frame now owns an intrusive attempt registration and
exclusive cancellation token. Admission links it under the pool lock; close
signals every admitted attempt through the queued connection bridge; settlement
unlinks it and restores the creation budget before publishing any result.
No separate heap container is introduced for this registry. Existing connection
creation now uses the cancellable overload; steady-state lease reuse and query
paths are unchanged. This is not a creation-cost equivalence claim.

The service-stop fixture now disables command timeout and sets TCP timeout to
30s. An expired first stop still reports unfinished cleanup, while driving the
loop settles pending creation and warmup within 3s, restores connecting_count
to zero, and permits successful cleanup retry. Windows Release integration,
allocation and wire suites pass in 5.81s; Arch epoll ASAN passes in 5.58s.
Startup-token propagation into warmup, lock-wait cancellation, multiple-attempt
close races and real TLS/authentication tests remain incomplete.

### Concurrent creation cancellation on pool close

The wire fixture now admits three connection attempts concurrently and starts
warmup against their reserved capacity, with command timeout disabled and TCP
timeout 30s. It exercises synchronous close followed by async_close, and direct
async_close. Within a 3s settlement bound every attempt and warmup must report
connection_closed; connecting, checked-out, waiter and slot counts must all be
zero. Repeated close and another event-loop poll must remain safe. Windows
Release rebuilt and passed the wire suite in 0.07s; Arch epoll ASAN passed in
0.08s. Formatting passed. This covers batch cancellation, not all possible
cross-thread admission/completion orders or successful concurrent publication.

### MongoDB startup warmup cancellation and deadline propagation

- Added an opt-in `warm_up(cancel_token&)` path. Its caller token links only to
  connection attempts owned by that warmup; waiting on other admitted attempts
  uses a cancellable timer. The original overload directly returns its
  non-cancellable specialization, without an extra wrapper coroutine.
- Application warmup now uses the service deadline and cancellation token.
  The attempt link unregisters before creation settlement and retains no callback
  after completion. Failed startup retains the existing cleanup/retry contract.
- Silent loopback hello tests cover command timeout, lifecycle deadline with
  command timeout disabled, and foreign-thread cancellation, each across two
  startup attempts. They verify original errors, zero pool counters, absent
  maintenance, cleared callback registration, and peer-observed disconnect.
- The late-cancellation fixture now cancels after acquiring its lease, rather
  than assuming acquisition succeeds when cancellation precedes the hello reply.
  Independent-creation coverage confirms cancelling warmup leaves three other
  attempts pending until explicit pool closure.
- Current-source builds and three suites (`test_application_integrations`,
  `test_http_disabled_overhead`, `test_mongodb_wire`) pass on Windows Release
  (5.89 seconds) and Arch Clang ASAN (5.72 seconds). Changed C++ files pass
  clang-format; generated AGENTS.md is current.
- This does not prove cancellable pool-lock waiting, real TLS/authentication
  cancellation, full production recovery, or zero CPU performance regression.

### Empty MongoDB pool health and idle close registration

- Opt-in cancellable health checks create a candidate when none are idle,
  respecting pool capacity and maximum concurrent creation. The candidate is
  immediately owned by a lease before insertion into the probe vector, so
  insertion failure returns it safely. Legacy maintenance remains unchanged.
- A loopback test starts with minimum_size=0, verifies hello plus two PING
  exchanges over one connection, idle/checkout/creation counts, token cleanup,
  and closure. This exposed retained idle slot registration after close; close
  now removes idle registrations while keeping borrowed slots until return.
- Current-source Windows Release and Arch ASAN builds pass all three targeted
  suites (Application integrations, disabled-overhead, MongoDB wire), respectively
  5.81 and 6.02 seconds. Formatting and generated instruction checks pass.
- Full-capacity busy probes still return pool_exhausted without waiting for a
  lease. This test does not prove real authentication, failure recovery under
  saturated load, all allocation failures in the new candidate path, or CPU
  performance equivalence. Those remain open.

### Cancellation of empty-pool health connection creation

- Added a loopback test with command timeout disabled. After observing hello
  bytes at the peer, a foreign thread cancels health-check creation. The same
  pool admits a second attempt and completes cancellation again; both attempts
  preserve operation_cancelled, clear token registration and all creation/lease
  counts, and close the transport as observed by the peer (not just counters).
- Rebuilt MongoDB wire suites pass on Windows Release (0.07 seconds) and Arch
  ASAN (0.09 seconds); clang-format passes. No production changes were required
  for this case. Repeated cancelled admission is not proof of successful
  authenticated recovery or saturated-pool health policy.

### Saturated-pool health checkout

- Cancellable health fallback now reuses acquire's existing queue instead of
  immediately returning pool_exhausted at capacity. A frame-owned notification
  adapts the health token to an owner-loop stop request and is joined before
  checkout returns, including exceptional completion. In-flight creation also
  receives the acquire stop token. Existing no-token maintenance is unchanged.
- Saturated single-slot tests retain the business lease while a managed probe
  expires or is cancelled cross-thread: the queue empties, token unregisters,
  and the business connection remains open. A subsequent queued probe succeeds
  after lease return, with real PING framing over the same connection.
- Three suites passed on Windows (5.84s) and Arch ASAN (5.67s); after adding the
  deadline case, Application integrations passed again (2.26s / 1.86s).
  Formatting and generated instructions checks pass.
- This closes the immediate full-pool rejection gap, not all mutex waits,
  allocation-failure races, authenticated recovery, or CPU equivalence proof.

### Queued health admission allocation failures

- A 32-position initial-resume allocation sweep covers health checkout while
  the only pool connection remains borrowed. Failed admission propagates memory
  errors; suspended cases are cancelled and joined. Assertions require cleared
  parent registration, zero waiters/connecting count, one intact borrower, and
  harmless reuse/cancellation of the completed token. Actual injected failures
  are required, not merely successful loop completion.
- Rebuilt disabled-overhead suites pass on Windows Release (3.65s) and Arch
  ASAN (3.78s); formatting passes. No production changes were required.
- Injection ends before event-loop polling; asynchronous allocation failures
  after suspension, sustained OOM, and all handoff races remain unproven.

### Queued health asynchronous allocation failures

- Extended the admission sweep to three separately asserted phases (32 positions
  each): initial resume, dispatch of the owned timeout operation, and cancellation
  settlement after timer dispatch. The last phase keeps injection enabled across
  polls until actual completion; a single poll originally missed allocations and
  correctly failed the mandatory injected-failure assertion.
- All phases preserve the business connection and lease, remove queued health
  work, and clear callback registration. Rebuilt disabled-overhead suites pass
  Windows Release (3.66s) and Arch ASAN (3.84s); formatting passes.
- These are single allocation failures at controlled phases, not sustained OOM,
  arbitrary concurrent handoff, or complete application cleanup proof.

### MongoDB close without staging allocation or detached dispatch

- Close transfers shutdown ownership through existing slot/waiter links rather
  than allocating copied vectors. A waiter records closed_by_pool; error objects
  are constructed on borrower resumption, outside noexcept close. This avoids
  error-object allocation discovered by the fault test.
- Contended close posts a state-owned node with retained shared ownership and
  records it in the completion barrier. async_close joins that notification;
  the former detached spawn is removed. Borrowed transports remain lease-owned.
- Tests cover synchronous zero-allocation close with a queued borrower and
  admitted creation, contended zero-allocation dispatch under the metadata lock,
  barrier joining, and retry after asynchronous waiting allocation failure.
  The synchronous fixture was corrected to avoid setting close_requested via
  async_close before testing close (which previously tested a no-op).
- Rebuilt Application integrations, disabled-overhead, and MongoDB wire suites
  pass Windows Release (6.02s) and Arch ASAN (5.75s). Formatting and generated
  instruction checks pass. Async task construction/waiting can still fail;
  event-loop shutdown before posted completion and arbitrary concurrency are
  not covered by this evidence. The full goal remains open.

### Current-source protocol-free module check

- Revalidated both caches: every protocol, SSL, QUIC and ORM are OFF. Windows
  also disables LevelDB/LZ4; the Arch cache retains those two non-protocol
  dependencies, so these are not identical dependency-minimal configurations.
- Incremental current-source builds of core/instrumentation and coroutine test
  targets succeed. `test_instrumentation`, `test_task`, `test_deadline`, and
  `test_task_group` pass Windows Release (0.09s) and Arch Release (0.03s).
- This verifies the selected protocol-free module graph and tests, not all
  targets, all platforms, a fully stripped dependency configuration, or disabled
  OTEL CPU-performance equivalence. No source fix was needed for this check.

### Close request versus synchronous dispatch ownership

- Split close_requested from close_dispatched: constructing an async_close task
  rejects new borrowers but no longer suppresses a later synchronous close when
  that task has not started. Only synchronous dispatch uses the new admission
  flag; closed state under the mutex remains the actual completion authority.
- The idle-pool regression obtains an unstarted async-close task, calls close,
  checks size is already zero, then runs the task to verify idempotent joining.
- Current-source three-suite runs pass Windows Release (5.97s) and Arch ASAN
  (5.77s); formatting passes. No global performance conclusion follows from
  these lifecycle-specific checks. All remaining goal gates stay open.

### Current-source enabled-observation regression refresh

- Rebuilt and ran HTTP observability, HTTP tracing, metric aggregation, OTLP
  exporter, and OpenAI suites: Windows Release 5/5 (2.82s), Arch ASAN 5/5 (2.27s).
- Inspected assertions include response/cancellation preservation with independent
  trace and metric sinks, sink exception containment, and nested Agent/Model
  parent-span/trace identity. The exporter suite includes a loopback HTTP receiver;
  these runs are not evidence of deployment against a production collector.
- No implementation change was needed. Cross-service production recovery and
  zero disabled CPU regression remain separate, unproven acceptance gates.

### Live-test watchdog scope correction

- Environment discovery found no Docker command on Windows/Arch and no Arch
  redis-server, mysqld or mariadbd. No service installation or remote mutation
  was performed; real-service deployment remains unverified.
- Rebuilt Windows test_application_mysql_live. Its negative checker exceeded
  15 seconds because it ran the entire expanded live suite rather than its named
  authentication/health/stop scenario. The checker now explicitly sets that
  filter, overriding inherited filters; its documented scope is unchanged.
- Seven malformed ports are rejected; the reserved unlistened loopback endpoint
  produces the intended failing test and exits in about five seconds. A new
  unit test verifies all eight launches use the intended filter and the existing
  15-second watchdog. Database required-run tests (4), messaging gate tests (5),
  and the new watchdog test (1) pass. This does not prove live MySQL success.

### MySQL negative-check completion evidence

- The watchdog now requires the named failed-test record and the exact one-test
  completion summary, not only exit code 1 and a name substring. Added rejection
  tests for empty/partial output, the wrong test/count, success, skip and abnormal
  exit codes. Both checker unit tests pass; the real negative invocation still
  rejects seven malformed ports and completes the unavailable-endpoint case
  within its original 15-second watchdog (about five seconds locally).
- This strengthens test evidence only; it is not live database or full lifecycle
  acceptance, and does not change the remaining goal requirements.

### Cross-platform CI wiring for execution gates

- Windows, Linux and macOS workflows now run the database required-run tests,
  MySQL watchdog tests and messaging runner gate tests immediately after Python
  setup. PowerShell explicitly propagates each native command failure instead
  of allowing a later successful command to mask it.
- All 11 tests pass locally on Windows and Arch. The three YAML files parse,
  each contains one new gate step, and scoped diff whitespace checks pass.
- These are local command/syntax checks, not successful GitHub Actions or macOS
  executions. No commit, push or remote workflow trigger was performed.

### OpenAI observation module dependency separation

- Moved the telemetry listener interface and implementation from the OpenAI
  protocol partition into `cnetmod.observability.openai`. The public namespace
  remains `cnetmod::openai`; callers explicitly import the optional adapter.
  The protocol aggregate no longer exports it. Application and both listener
  test consumers have explicit imports; event generation code is unchanged.
- Current source search finds no direct `import cnetmod.observability` in
  `src/protocol`. This is a direct dependency check, not a transitive dependency
  audit or proof that all observation logic has left protocol implementations.
- Windows core and both OpenAI/disabled-overhead test targets rebuilt; tests
  pass 2/2 (3.90s). Arch Clang 22 ASAN builds and the same tests pass 2/2 (4.55s).
  Windows required CMake regeneration after moving files; the disabled-overhead
  consumer initially failed until its new explicit import was added.
- Adapter formatting, scoped diff checks and generated AGENTS synchronization
  pass. Migration instructions document the import change. This does not prove
  disabled CPU-performance equivalence, remaining protocol adapter separation,
  or partial-start cleanup ownership. No commit or push was performed.

### Partial-start cleanup ownership

- Added executor-affine `managed_service::cleanup_required()` with an out-of-line
  false default. MongoDB exposes its retained cleanup_pending state. Initial
  startup and recovery preserve reported ownership after failures/exceptions,
  before telemetry or health publication. Successful startup remains tracked
  independently; no query/request hot path was changed.
- Recovery completes pending cleanup before another start, sharing the attempt
  deadline. Failed cleanup remains registered; successful cleanup retires its
  ownership before reacquisition. Shutdown retains dependencies of an unfinished
  partial service using the existing reverse-dependency rules.
- New fake-service tests cover returned/thrown startup failures, failed rollback,
  original error preservation, dependent retention, idempotent cleanup retry,
  and optional repeated partial-start cleanup before recovery succeeds.
- Windows core and Application/integration targets rebuilt; both suites pass
  (8.49s). Arch Clang 22 ASAN rebuilt the same targets; Application passes
  (5.09s), integration passes (1.84s). Core comments/format and AGENTS are checked.
  A final whitespace-only formatting fix followed the executable builds.
- These tests establish the explicit reporting contract, not that every adapter
  reports every partial resource or that actual MongoDB startup/cleanup races
  are exhausted. Real dependency recovery, full platform gates and disabled CPU
  parity remain open. No commit or push was performed.

### MySQL and Redis startup admission

- Both managed adapters reject already-cancelled or expired startup contexts
  before registering pool maintenance. Cancellation takes precedence when both
  apply. Protocol pool/query implementations are unchanged by this fix.
- Extended the existing database admission test to all four pool adapters;
  MySQL/Redis also require no supervisor task entry. No event loop is driven
  during rejected admission, and pool size remains zero.
- The MySQL unconnected-health regression previously used pre-cancelled startup
  to create a pool as a side effect. It now admits startup with a 20ms deadline,
  requires an accepted silent peer and an allocated slot, then verifies health
  still times out rather than reporting up. The original health assertions stay.
- Windows rebuilt core/Application/integrations, both suites pass (8.48s).
  Arch Clang 22 ASAN rebuilt the same targets; both pass (7.10s). Formatting,
  scoped diff checks and AGENTS synchronization pass. No commit or push.
- Resource ownership after admitted startup failure remains a separate audit:
  MySQL/Redis intentionally retain supervised pool recovery. It must not be
  confused with MongoDB pending cleanup or blindly changed to teardown/restart.
  Full real-service recovery and disabled CPU parity remain unproved.

### Recoverable pool ownership versus cleanup-before-retry

- Added `shutdown_required()` separately from `cleanup_required()`. Its default
  delegates to the cleanup query; MySQL/Redis override it with their registered
  maintenance state. Failed initial acquisition and failed recovery now retain
  shutdown ownership without forcing pool reconstruction before retry.
- New real-adapter loopback unavailable-port tests assert optional failure owns
  a service despite never becoming ready, does not require cleanup before retry,
  and relinquishes ownership only after ordered shutdown completes.
- Updated the required MySQL recovery-budget regression: it no longer assumes
  an empty ownership registry and manually stops the adapter. It now shuts down
  through lifecycle, checks ownership is cleared, and verifies the supervisor's
  original failure remains the stop result even though resources were released.
- Windows rebuilt core and both Application/integration tests; both suites pass
  (8.53s). Arch Clang 22 ASAN builds and both suites pass (7.01s).
  Format/scoped diff and generated instruction checks pass. No protocol query
  hot path, commit or push is involved. Other adapters and real-service recovery
  remain open, as do disabled CPU equivalence and full platform acceptance.

### PostgreSQL warmup ownership and post-separation core build

- Inspected cancellable warmup: its failure path releases acquired connections,
  while discarded slot metadata remains available for retry. Extended the
  existing two-peer lifecycle regression to assert no cleanup/shutdown obligation
  after that failure. Existing peer disconnect and successful retry checks remain;
  no PostgreSQL production code was changed. Concurrent external borrowers and
  reconnect work during failed warmup are outside this fixture's evidence.
- Reconfigured/rebuilt selected protocol-free core and instrumentation/coroutine
  targets after the OpenAI module move and lifecycle contract additions. Windows
  four tests pass (0.08s); Arch Release four tests pass (0.03s). Arch retains
  LevelDB/LZ4; no protocol, SSL or ORM is enabled. This is not all-target or CPU
  equivalence evidence. Windows Application integration regression passes (2.31s).
- The full objective, including real-service acceptance, remains open. No commit
  or push was performed.

### Real MySQL verification through an SSH tunnel

- Used the user-authorized replacement server and the btpanel SDK to create and
  verify a dedicated database and loopback-only test account. No production
  database or public MySQL firewall rule was changed. Credentials stay outside
  the repository; the local generated test credential file has a user-only ACL.
- Rebuilt Windows Release test_application_mysql_live with ORM enabled. Ran all
  four tests with CNETMOD_MYSQL_INTEGRATION=1 against remote MySQL through an
  SSH-skill loopback tunnel: authentication/health/supervised stop, Host cleanup
  retry after lease return, Collector failure preserving cleanup, and three-signal
  export during rollback all pass. The runner reported exactly four passed tests,
  not a skipped or filtered success. The initial invocation without the enable
  flag skipped and is not counted as evidence.
- Test database retained for subsequent regression; the test tunnel was stopped.
  Panel API calls used the provided token; the server-local stored token did not
  authenticate. API wrappers needed explicit action parameters. No secrets or
  endpoint credentials were added to repository artifacts.
- This verifies Windows over SSH with MySQL TLS disabled, not native Linux/macOS,
  MySQL TLS, dependency interruption/recovery, every protocol, or disabled CPU
  equivalence. The complete goal remains open; no commit or push was performed.

### Explicit real MySQL session-recovery coverage

- Inspected the live authentication test beyond its name: it already contains
  three COM_QUIT/reacquire cycles and three server-side KILL CONNECTION cycles,
  targeting only the ID returned by its own retained test-user connection.
  Added separate completion counters requiring all three cycles of each kind.
  Existing assertions require changed connection IDs, cleared session state,
  capacity one, no queued borrowers and non-timeout detection of killed sessions.
- The same ORM-enabled Windows test compares successful and SQL-error results
  with empty, collecting and throwing span sinks against an unobserved baseline.
  This is functional parity, not throughput/latency equivalence.
- Rebuilt and reran all four live tests against the dedicated remote database:
  exactly 4/4 pass. SSH tunnel stopped afterward. No production code or service
  was changed; no commit/push. Prior wording that connection recovery was wholly
  untested was too broad: whole-service outages/readiness recovery remain open,
  whereas this bounded per-session replacement is now explicitly evidenced.

### Named-module dependency boundary regression guard

- Added `tools/check_observability_dependencies.py`: lower protocol, database,
  core, coroutine, I/O, executor and instrumentation modules must not reach
  Application or observability adapters through named-module imports. Imports
  from interface and implementation units are merged; partition imports are
  resolved, cycles terminate, and diagnostics report a shortest offending chain.
- The source scan conservatively includes inactive preprocessor branches and
  ignores comments/string literals. It is not a C++ preprocessor, header graph,
  link dependency audit or runtime overhead measurement. Current 475 named
  modules pass. Seven positive/negative checker fixtures pass on both Windows
  and Arch Python. Generated instructions remain current.
- Added the guard and its regression fixtures before configuration in Windows,
  Linux and macOS workflow definitions. Remote CI has not run; no commit/push.
  No production C++ or protocol behavior changed in this step. Disabled CPU
  equivalence and the remaining full lifecycle/integration acceptance are open.

### Disabled exporter endpoint initialization

- Found that exporter construction derived metrics/log URLs from a configured
  trace endpoint before clearing disabled signals. It now derives only enabled
  signals; enabled endpoint inheritance is preserved. This removes unnecessary
  URL string allocations and their possible bad_alloc when export is disabled.
- Added a fault-injection regression comparing construction against the same
  by-value options transfer. MSVC std::map moves allocate sentinels, so the test
  permits exactly the measured argument-transfer allocation count/bytes and
  rejects any additional exporter allocation. Caller configuration construction
  and telemetry_hub state are outside this test's scope.
- Verified the corrected fixture against temporarily restored old URL logic:
  it fails with exit 1. Restored the fix, rebuilt, and passed both complete
  disabled-overhead and OTLP exporter suites on Windows Release (5.01s) and
  Arch Clang 22 ASAN (4.43s). Windows transient LNK1104 resolved on retry.
- Changed-line format checks and the module boundary check pass. Whole-file
  format checking still flags pre-existing spacing around lines 2785/2828 in
  the allocation test; those unrelated lines were not changed. This is startup
  allocation evidence, not request throughput or full zero-overhead acceptance.
  No commit or push; the full objective remains open.

### OpenAI measurements through the unified sink

- Audit found that the existing OpenAI listener sent spans to OTLP but wrote
  metrics directly to the local registry. The existing metric/cost test asserted
  rendered OpenMetrics, not OTLP metric delivery. No new move/shutdown defect was
  established during the preliminary ownership inspection.
- Added an SDK-neutral metric-sink constructor and an internal destination
  adapter; Application injects the hub measurement sink instead of its registry.
  The registry constructor retains local-only semantics. Metric names are stripped
  of OpenMetrics suffixes before structured publication so hub local aggregation
  restores the same names without double suffixes or duplicate recording.
- New unit coverage validates measurement shape, token sum, duration buckets,
  privacy and span completion despite throwing metric sinks. A real loopback HTTP
  collector test constructs the Application OpenAI service, delivers model events,
  receives OTLP JSON, verifies 7 accepted metric measurements, token sum 125,
  duration count 1, no private event text, and local token counts exactly once.
  It settles telemetry and collector before event-loop teardown. This is event-to-
  collector evidence, not a real OpenAI API request or all GenAI metric scenarios.
- Windows Release and Arch Clang 22 ASAN rebuilt and passed all four suites:
  OpenAI, OTLP exporter, Application integrations and disabled overhead (7.54s
  and 6.85s respectively). An earlier receiver test build failed due to a missing
  service constructor argument; old-binary CTest output from that attempt is not
  counted. Final receiver-containing builds succeeded before these runs.
- Core adapter/new fixture format checks and named-module boundary check pass.
  No protocol logic was changed, no commit/push. Full performance, cross-platform
  acceptance and remaining real-service coverage stay open.

### Empty OpenAI listener destinations

- The new standalone sink constructor exposed a disabled-path gap: an empty
  metric sink and empty span sink still caused operation-table allocations and
  timestamp reads. A prebuilt-event fault-injection fixture reproduced failure
  before the fix. Destination availability now disables metric recording when
  there is no destination; with neither output, observe returns before event
  classification, operation tracking or clocks. Statistics remain zero, as
  documented on the public constructor. Registry-based metric recording remains
  available, and an installed span sink still records traces independently.
- Windows Release and Arch Clang 22 ASAN rebuilt and passed the four OpenAI,
  exporter, Application integration and disabled-overhead suites (7.57s/6.86s).
  The new test checks zero allocations, untouched allocation-failure injection,
  no exception, and no operation counters for start/end/error/retry events.
- This does not remove listener construction costs or event creation performed
  by a caller that explicitly installs a listener. Application still omits the
  listener entirely when observation is disabled. It is not a universal CPU
  performance proof. No commit/push; full acceptance remains open.

The entire target remains open until these requirements have direct evidence.
