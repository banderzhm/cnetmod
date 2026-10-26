# HttpArena 2026 Performance Plan

## Objective

Make cnetmod competitive for the absolute lead in the HttpArena HTTP/1.1,
HTTP/2, HTTP/3, and Gateway profiles without weakening protocol correctness,
production behavior, portability, or unrelated benchmark profiles.

The public result set updated on 2026-10-25 is the initial baseline. cnetmod is
classified as an engine, so comparisons use the full field rather than only the
default framework league.

Sources:

- <https://http-arena.com/>
- <https://github.com/MDA2AV/HttpArena/blob/main/site/data/results/cnetmod.json>

## Non-negotiable gates

An optimization is accepted only when all of the following hold:

1. The relevant protocol and integration tests pass in Release mode.
2. Protocol-visible behavior remains conformant; benchmark-only shortcuts are
   not added to the framework.
3. The official profile is run three times. Record every run, the median, the
   best result, CPU per request, memory, p99, and p99.9 when available.
4. The result is compared with a clean baseline built with the same compiler,
   CPU target, allocator, LTO setting, worker count, affinity, and kernel.
5. Directly affected profiles do not regress by more than 1% in the median.
   A larger regression blocks the change until explained and fixed.
6. Cross-protocol smoke tests cover HTTP/1.1, HTTP/2 cleartext and TLS, HTTP/3,
   WebSocket, gRPC, and Gateway before merging to `develop`.
7. Performance-specific defaults remain explicit and production-safe. No
   hidden environment-variable behavior or benchmark-path detection is allowed.

## Initial gap map

The percentages below compare the published cnetmod result with the absolute
leader of each profile. They are prioritization inputs, not promises of a fixed
gain from one change.

| Area | Profile | Initial position | Approximate gap | Priority |
|---|---|---:|---:|---:|
| Gateway | gateway-h3, 64 connections | 6 | 57.8% | P0 |
| Gateway | gateway-h3, 256 connections | 10 | 70.8% | P0 |
| HTTP/3 | baseline-h3 | 3 | 23.1% | P1 |
| HTTP/3 | static-h3 | 2 | 5.0% | P1 |
| HTTP/1.1 | baseline | 20 | 12.7% | P1 |
| HTTP/1.1 | limited-conn | 20 | 4.3% | P1 |
| HTTP/1.1 | pipelined, reference-only | 14 | 36.1% | P1 |
| HTTP/1.1 | json-tls | 12 | 14.8% | P1 |
| HTTP/1.1 | static-tls | 6 | 10.1% | P1 |
| HTTP/2 | baseline-h2, TLS | 2 | 6.3-6.6% | P1 |
| HTTP/2 | static-h2 | 2-3 | 10.9-12.3% | P1 |
| HTTP/2 | json-h2c | below leader | 44.6-45.9% | P1 |
| Gateway | gateway-64 | 2 | 0.2-1.9% | P2 |

HTTP/2 cleartext baseline already leads at 1024 and 4096 connections. That is
evidence against rewriting the complete HTTP/2 scheduler: TLS, response
construction, JSON, and static-body paths should be isolated first.

## Execution order

### G1 - Establish the Gateway-H3 edge baseline

**Status:** in progress

The published cnetmod adapter places Caddy in front of the cnetmod backend. Its
CPU breakdown shows the edge consuming far more CPU than the backend, so the
first task is to distinguish proxy saturation from cnetmod saturation.

Actions:

- Run the official gateway-h3 workload with identical backend, certificates,
  routes, CPU sets, and load generator against Caddy and Nginx QUIC.
- Test 64 and 256 connections with 1, 4, 8, 16, and 32 proxy workers where the
  implementation supports that choice.
- Record proxy and backend CPU independently.
- Confirm response status distribution and body correctness.
- Inspect `perf record` for the winning configuration.

Acceptance:

- Three stable runs at both connection counts.
- No failed requests or protocol errors.
- The selected edge configuration improves the median and does not merely move
  CPU time from the proxy to the load generator.

Current evidence:

- The initial single-run Nginx result could not be reproduced and is rejected.
- A controlled three-run comparison used the same cached cnetmod backend,
  database, URI mix, CPU sets, load generator, and five-second duration.
- At 64 connections, Caddy's median was approximately 7.40K requests per
  second; Nginx QUIC's median was approximately 2.01K.
- At 256 connections, Caddy's median was approximately 8.89K requests per
  second; Nginx QUIC's median was approximately 1.50K.
- Every recorded response was 2xx. The Nginx replacement is rejected and its
  adapter changes were removed. G1 now proceeds by profiling Caddy and the
  cnetmod backend independently rather than changing proxies speculatively.
- A 30-second Caddy run with isolated local CPU sets reproduced the published
  CPU imbalance. At 64 connections the proxy used approximately 369-392% CPU,
  while the cnetmod backend used 57-61% and PostgreSQL used 23-25%. At 256
  connections the proxy used approximately 417-419%, while the backend used
  63-65% and PostgreSQL used 24-26%. The edge exhausted roughly four cores;
  the backend remained substantially underutilized. This confirms that the
  existing external Gateway-H3 stack is edge-bound on this machine too.

### G2 - Implement a cnetmod-native Gateway-H3 path

**Status:** in progress

Code review finding:

- cnetmod currently has no first-class reverse-proxy component.
- `http::client_pool` is an idle-client cache rather than a bounded concurrent
  upstream pool. When no idle client is available, `acquire()` immediately
  constructs another client; it has no total-connection bound, waiter queue,
  lease deadline, or overload result.
- `application::rest_template` correctly selects a pool per event loop, but it
  inherits the same unbounded-burst behavior. It is suitable for ordinary
  service calls, not yet a complete high-throughput gateway transport.

Actions:

- Profile the current reverse-proxy request and response ownership path.
- Introduce a bounded per-event-loop upstream lease pool with explicit maximum
  active connections, FIFO waiters, acquisition deadlines, cancellation, and
  overload behavior. Keep the existing idle-cache API backward compatible.
- Reuse upstream connections per event loop; never share a mutable connection
  object across loops.
- Bound the upstream queue and propagate downstream cancellation and backpressure.
- Preserve request and response header views until mutation is required.
- Batch QUIC output without delaying small responses beyond the latency budget.

Acceptance:

- Match or exceed the selected external edge baseline.
- Stable memory at 64 and 256 connections.
- Disconnect, timeout, cancellation, and upstream failure tests pass.

Progress on 2026-10-26:

- Added an explicit `bounded_client_pool` while preserving the legacy idle
  cache API. The new pool is owned per event loop and provides a hard client
  limit, bounded FIFO waiters, acquisition timeout, cancellation, overload
  errors, RAII leases, discard-on-transport-failure, and orderly shutdown.
- Added deterministic tests for FIFO handoff, timeout cleanup, cancellation,
  overload rejection, and shutdown with checked-out leases. The pool test also
  passed 100 consecutive executions under Clang 23 Release.
- A two-million-iteration Release microbenchmark measured 32.88-33.67 ns per
  uncontended bounded acquire/release, or about 29.7-30.4 million leases per
  second. The legacy unbounded idle cache measured 2.88-2.93 ns per operation.
  The approximately 30 ns capacity-control cost is far below one network I/O
  operation and is acceptable for the gateway path.

### H3-1 - Reduce small-response QUIC submission cost

**Status:** pending

Actions:

- Measure ACK-only packet ratio, packets per response, send syscalls per response,
  and event-loop wakeups per response.
- Coalesce ACK and stream data when the ACK deadline permits.
- Batch ready datagrams once per event-loop turn with a strict packet/time budget.
- Keep pacing, congestion control, anti-amplification, and loss recovery semantics.

Acceptance:

- Improve baseline-h3 median without regressing static-h3, loss recovery, PMTU,
  migration, multipath, or interop tests.

### H3-2 - Remove QPACK and stream-lifecycle allocations

**Status:** pending H3-1

Actions:

- Add verified fixed-header fast paths for common pseudo-header sets.
- Decode directly into request header storage.
- Reuse stream state from a connection-local pool.
- Keep dynamic-table, blocked-stream, sensitive-header, and Huffman behavior intact.

Acceptance:

- Allocation and instruction counts per baseline request decrease.
- Full QPACK and HTTP/3 tests pass under sanitizers.

### H1-1 - Finish the fixed-response fast path

**Status:** partially complete

The router now has an explicit prepared-response path. The next work is to make
the transport consume it without repeated parsing, header construction, or
entity conversion while retaining middleware-visible response semantics.

Actions:

- Quantify `prepared_response` hits and fallback reasons.
- Avoid rebuilding standard headers for unchanged fixed responses.
- Preserve dynamic Date, Server, Cookie, and middleware modifications by falling
  back to normal serialization.
- Use one transport-owned output buffer for ordinary small responses.

Acceptance:

- Improve baseline and limited-connection medians.
- Router, middleware, 404/405, keep-alive, pipeline, and response tests pass.

### H1-2 - Batch parser and output work for pipelines

**Status:** pending H1-1

Actions:

- Parse every complete request already present in the receive buffer within a
  bounded fairness budget.
- Reuse request/header storage between sequential requests.
- Encode adjacent responses into one connection output batch.
- Submit the batch with the minimum safe number of kernel operations.

Acceptance:

- Improve the reference pipeline profile without reducing baseline fairness,
  async throughput, or limited-connection performance.

### H1-3 - Revisit SIMD only with end-to-end proof

**Status:** evidence required

The existing isolated CRLF scanner is faster, but production integration has not
yet demonstrated a representative end-to-end gain. Follow the reconsideration
gate in `docs/performance/http-parser-simd.md`; do not add architecture-specific
complexity based only on a microbenchmark.

### H2-1 - Optimize TLS record and DATA-frame submission

**Status:** pending

Actions:

- Compare H2C and H2 TLS profiles using identical route and response paths.
- Measure BIO copies, encryption calls, records, writes, and wakeups per response.
- Batch HEADERS and small DATA where protocol state permits.
- Preserve flow-control and END_STREAM semantics.

Acceptance:

- Close the H2 TLS gap without regressing the leading H2C profiles.

### J1 - Serialize JSON directly into transport-owned response storage

**Status:** pending

Actions:

- Use DTO serialization directly into the response buffer.
- Remove temporary `std::string` and response-to-server-response copies.
- Reuse compression state per event loop where the library permits safe reset.
- Measure compression ratio as well as throughput for `json-comp`.

Acceptance:

- Improve json-h2c, json-tls, and json-comp together.
- Serialized bytes remain identical to the specified contract.

### S1 - Consolidate the static TLS path

**Status:** pending

Actions:

- Reuse immutable metadata and opened-file state.
- Keep file contents governed by HttpArena consistency requirements.
- Minimize encrypted-copy count and TLS record fragmentation.
- Share only immutable state across event loops.

Acceptance:

- Improve static-tls and static-h2 without regressing static-h3.

### G3 - Close the remaining cleartext Gateway gap

**Status:** pending

The cleartext gateway is already within approximately 2% of the leader. Only
changes demonstrated by profiling are allowed after the higher-impact phases.

## Eight-CPU saturation gate

Run the fixed-rate profile with these server configurations:

1. Four workers pinned to one logical CPU from each physical core.
2. Four workers pinned to four physical cores while the process may observe all
   eight logical CPUs.
3. Eight workers pinned to the four cores and their SMT siblings.

Record load-generator scheduling lag separately from server queueing. A server
change is accepted only when it improves achieved rate and tail latency without
increasing generator lag enough to invalidate the comparison.

## Change log

| Date | Phase | Result | Decision |
|---|---|---|---|
| 2026-10-26 | G1 | Controlled three-run A/B showed Caddy decisively ahead of Nginx QUIC at both 64 and 256 connections | Reject Nginx replacement; profile the existing edge and backend |
| 2026-10-26 | G1 | Caddy consumed about 3.7-4.2 cores while the backend consumed 0.6 core in 30-second 64/256-connection samples | Existing stack is edge-bound; advance to the native gateway design |
