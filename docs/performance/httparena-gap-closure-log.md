# HttpArena Gap Closure Execution Record

## Record rules

This file is append-only. It prevents benchmark work from being repeated,
mixed across machines, or attributed to the wrong source revision.

Each work session must record:

1. date and queue item;
2. cnetmod branch, commit, and worktree state before editing;
3. HttpArena commit and result snapshot;
4. hypothesis and the exact path being changed;
5. build identity, compiler, flags, allocator, worker count, affinity, kernel,
   and load-generator identity;
6. commands and output locations;
7. every baseline and candidate round;
8. focused tests and cross-profile gates;
9. retained, reverted, or inconclusive decision;
10. the one exact continuation point.

Do not combine results from different machines in one comparison. Do not call
a diagnostic harness an official run. Failed and rejected experiments remain
in this record, with their reason, so they are not repeated later.

## Current handoff

| Field | Value |
|---|---|
| Active item | H2 - Reduce HTTP/1 connection churn |
| cnetmod worktree | `E:\github\cnetmod-httparena-gap-closure` |
| cnetmod branch | `new/httparena-gap-closure` |
| Starting revision | `f60ac87fef42aa92217d1d537427bd400b8653f4` |
| Upstream base | `origin/develop` |
| HttpArena inspection worktree | `E:\github\HttpArena-cnetmod-current` |
| HttpArena branch | `update/cnetmod-current` |
| Result revision | `2802675ec` |
| Uncommitted cnetmod change | W1 eager multishot receive, W2 borrowed echo writes, W3 direct upgrade handoff, skill sync, and these performance records |
| Current code change | W1-W3 are retained; both H1 candidates tested so far were removed |
| Continuation point | Reprofile H1 on the benchmark host; local parser reset, immediate send, and provided-buffer borrowing candidates were all rejected |

## 2026-10-30 - Queue initialization

### Inputs inspected

- HttpArena result file: `site/data/results/cnetmod.json` at `2802675ec`.
- HttpArena scoring catalog: `scripts/gen_leaderboard_data.py`.
- cnetmod performance history:
  `docs/performance/httparena-2026-optimization-plan.md`.
- cnetmod HTTP, HTTP/3, and WebSocket project skills.
- Published cnetmod Gateway adapters and the leading Gateway and WebSocket
  adapters in the current HttpArena checkout.

### Baseline captured

The scored-row averages and absolute published leaders were copied into
`httparena-gap-closure.md`. The largest current profile gap is `gateway-h3`:
cnetmod averages 70,978 requests per second and the fastest published entry
averages 198,798, a 64.30% difference.

The current published cnetmod H3 Gateway uses Caddy as the HTTP/3 edge. Its
dynamic requests travel over loopback HTTP/1.1 to cnetmod. A previous local
experiment used a native edge, but it changed the measured stack and ran on
another host. It does not explain the published gap and is not the G1 path.

The cnetmod and vanilla Caddy configurations are equivalent except for their
backend port. Both use the same edge image, CPU split, HTTP/1.1 transport,
five-minute keepalive, and 2048 idle upstream connections. At 64 connections,
cnetmod records 81,481 requests per second with 343% backend CPU; vanilla
records 92,115 with 228% backend CPU. At 256 connections, cnetmod records
60,476 with 251% backend CPU; vanilla records 62,102 with 140% backend CPU.
The same-edge comparison supersedes the earlier native-edge direction.

### Decision

G1 is the only active item. No protocol hot-path source has been changed in
this branch. Keep Caddy unchanged and profile the ordinary cnetmod HTTP/1.1
backend before selecting a source change.

### Required G1 evidence

- adapter validation output;
- three matched cnetmod and vanilla `gateway-h3` rounds at 64 connections;
- three matched cnetmod and vanilla `gateway-h3` rounds at 256 connections;
- separate edge and backend CPU/memory;
- backend connections per worker, requests per connection, sends, and wakeups;
- response and error counts;
- `baseline`, `limited-conn`, `gateway-64`, and `production-stack` regression
  results;
- final retain or revert decision.

## 2026-10-30 - G1 direction correction

### Observation

The leading comparable adapters also use stock Caddy. Their higher scores show
that Caddy alone does not explain cnetmod's result. The same-edge CPU breakdown
shows more work in the cnetmod backend, especially at 64 connections.

### Decision

Do not replace Caddy. The native-edge adapter remains outside the active path.
G1 now profiles cnetmod behind the unchanged official edge and accepts only a
backend change supported by matched measurements.

### Exact continuation point

Run the unchanged official cnetmod and vanilla Gateway-H3 adapters serially on
the local Arch WSL environment. Capture three rounds at 64 and 256 connections,
then attach `perf` to cnetmod's backend during a separate 64-connection run.

## 2026-10-31 - G1 - same-edge local baseline and first candidate

### Source identity

- cnetmod revision: `613d053a1bdd6537dd9c6f35bf35fb9cede20cc6`
  for the adapter binary; the gap-closure worktree still starts at
  `f60ac87fef42aa92217d1d537427bd400b8653f4`.
- cnetmod worktree state: no framework source change; the plan and this record
  are untracked.
- HttpArena revision: `2802675ec6a52cb256fd4c6e47f2869e31ecfe04`.
- baseline binary SHA-256:
  `ef423a6666d275f5d006e1d165e32dc8ce697f64f5b2e15a6b0aacb5841853b7`.
- candidate binary SHA-256: not available because WSL stopped accepting new
  processes before the candidate build.

### Environment

- host allocation: 32 logical CPUs and 15 GiB WSL memory.
- OS and kernel: Arch Linux under WSL2,
  `6.18.54.1-microsoft-standard-WSL2`.
- compiler and standard library: Clang 23.1.4 with libc++.
- build: Release, ThinLTO, `x86-64-v3`, bundled mimalloc 3.5.3 and BoringSSL.
- local affinity: Caddy CPUs `0-7`, backend CPUs `8-15`, load generator CPUs
  `16-31`; H3 load-generator threads: 16.
- load generator: the HttpArena `h2load-h3:local` image at the revision above.
- duration and rounds: five seconds, three rounds, one profile at a time.

This machine is smaller than the official host. These numbers are paired local
diagnostics and are not official leaderboard results.

### Correctness results

The official Gateway-H3 validator completed 12 checks with 12 passes. It
covered HTTP/2 negotiation on the shared Caddy listener, static MIME and size,
404 handling, JSON shape and totals, database rows and empty ranges, and two
baseline query cases.

### Performance rounds

The table reports the harness parser's 2xx count divided by elapsed time. The
load generator's headline includes responses outside that exact interval and
is not used here.

| Variant | Connections | Round 1 | Round 2 | Round 3 | Median | Backend CPU | Backend memory | Errors |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| cnetmod | 64 | 23,463 | 25,263 | 22,958 | 23,463 | 120% / 152% / 111% | 211 / 214 / 221 MiB | 0 / 0 / 0 |
| vanilla-epoll diagnostic image | 64 | 24,811 | 28,374 | 27,473 | 27,473 | 71% / 80% / 71% | 54 / 58 / 57 MiB | 0 / 0 / 0 |
| cnetmod | 256 | 12,845 | 14,357 | 11,571 | invalid | 126% / 104% / 93% | 388 / 390 / 393 MiB | 1,282 / 1,095 / 4,108 HTTP 5xx |

The vanilla image came from the existing local October 20 build. It is not the
current adapter revision, so it is useful for locating the cost but cannot be
used as a release gate. Its 256-connection run was interrupted before a full
three-round result was captured.

### Diagnosis

Both adapters use stock Caddy with equivalent forwarding settings. The local
64-connection run again shows materially more backend CPU in cnetmod.

The Gateway workload sends seven fixed `/json` URIs, which account for 35% of
requests. The cnetmod adapter constructs a compression middleware with
`cache_capacity = 0`, so every request serializes the JSON and performs Brotli
compression. The framework already provides a bounded compression cache with
an application-defined semantic key. The vanilla adapter description and
source show that it reuses complete compressed JSON responses for this
low-cardinality workload.

### Candidate change

`E:\github\HttpArena-cnetmod-current\frameworks\cnetmod\server.cpp` now sets
the existing compression middleware's cache capacity to seven and keys entries
by the complete request URI. The middleware adds the negotiated content coding
to its internal key and compares the current source body before reuse. The
change does not bypass the handler, alter response bytes, or add a framework
API.

### Interruption

After the benchmark process was interrupted, WSL retained unresponsive client
processes. New `wsl` commands returned `-1`, and the current session lacks the
privilege needed to restart `WslService`. No candidate number is recorded.

### Decision

Keep Caddy unchanged. Keep the adapter candidate only as an uncommitted test
change until it compiles, passes validation, and improves the matched median.
Do not change the HTTP parser, router, or transport before this existing
framework capability is measured.

### Exact continuation point

After the Arch WSL runtime accepts processes again, rebuild only
`cnetmod-httparena`, create the same runtime image, rerun validation, then run
the 64-connection Gateway-H3 profile for three rounds with the affinity above.
If the median improves, run 256 connections after finding the source of its
HTTP 5xx responses; otherwise revert the adapter candidate.

## 2026-10-31 - G1 - explicit Gateway compression cache retained

### Source identity

- cnetmod adapter revision:
  `613d053a1bdd6537dd9c6f35bf35fb9cede20cc6`.
- cnetmod gap-closure worktree base:
  `f60ac87fef42aa92217d1d537427bd400b8653f4`.
- HttpArena revision:
  `2802675ec6a52cb256fd4c6e47f2869e31ecfe04`.
- baseline binary SHA-256:
  `ef423a6666d275f5d006e1d165e32dc8ce697f64f5b2e15a6b0aacb5841853b7`.
- final candidate binary SHA-256:
  `a3832dfb66441585a73a97d5e4276e33fea801a0c93111ac9a016af00407e2a1`.
- HttpArena worktree changes are uncommitted in `server.cpp`,
  `compose.gateway.yml`, and `compose.gateway-h3.yml`.

### Environment

- Arch Linux under WSL2, kernel `6.18.54.1-microsoft-standard-WSL2`.
- 32 logical CPUs and 15 GiB memory.
- Clang 23.1.4, libc++, Release, ThinLTO, `x86-64-v3`.
- bundled mimalloc 3.5.3 and BoringSSL.
- edge CPUs `0-7`, backend CPUs `8-15`, load-generator CPUs `16-31`.
- 16 load-generator threads, five-second rounds, one benchmark at a time.
- stock Nginx for Gateway-H2 and stock Caddy for Gateway-H3. Neither proxy
  configuration was changed.

These are paired local diagnostics. They are not leaderboard measurements.

### Hypothesis

The Gateway workload rotates across seven fixed JSON paths. The backend still
serialized every response, then ran Brotli again for every request. cnetmod
already has a bounded compression cache that executes the handler first,
compares the current source body, and reuses only the compressed bytes.

### Rejected intermediate forms

Enabling the cache for every `/json` request reduced CPU but lowered the
standalone persistent-connection JSON throughput. A request-time query check
also left cache selection on the common path. Both forms were rejected.

The retained form is explicit at startup. `server.cpp` keeps the original
compression options unless `CNETMOD_HTTP_STABLE_COMPRESSION_CACHE` is present.
Only the two Gateway compose files set this switch. The ordinary JSON,
HTTP/2 JSON, TLS JSON, and production-stack containers keep the original
middleware path without a request-time branch.

The cache capacity is seven and the semantic key is the request path. The
middleware still invokes JSON serialization on every request and compares the
new source body before reusing compressed bytes. This satisfies HttpArena's
live-serialization rule and does not create a pre-serialized response cache.

### Correctness results

- focused JSON validation: 5 passed, 0 failed;
- Gateway-H2 and Gateway-H3 validation: 24 passed, 0 failed;
- full isolated protocol and Gateway validation: 140 passed before the
  production-stack sidecar build;
- the first production-stack attempt stopped before startup because the
  Alpine package mirror refused the auth-sidecar build;
- rerunning production-stack with the already-built identical auth-sidecar
  image: 15 passed, 0 failed.

Combined code-path result: 155 checks passed and no application or protocol
check failed.

### Gateway-H2 performance

At 512 local connections the eight-CPU edge emitted many 5xx responses. The
official host assigns 40 logical CPUs to that edge, so the local 512 result is
invalid. The paired local gate therefore used 64 connections, where every
round completed without errors.

| Variant | Raw round 1 | Raw round 2 | Raw round 3 | Raw median | Harness best | Total CPU on winning round | Backend CPU | Peak memory | Errors |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| baseline | 94,156 | 104,922 | 101,736 | 101,736 | 103,678 | 1109.4% | 366% | 524 MiB | 0 / 0 / 0 |
| candidate | 103,118 | 107,845 | 103,412 | 103,412 | 106,990 | 987.8% | 243% | 434 MiB | 0 / 0 / 0 |

The raw median increased 1.65%; the harness result increased 3.19%. Total CPU
fell 10.96%, backend CPU fell 33.61%, and peak memory fell 17.18%.

### Gateway-H3 performance

The paired local gate used 64 connections. Every round completed without an
HTTP error, timeout, or lost QUIC packet.

| Variant | Raw round 1 | Raw round 2 | Raw round 3 | Raw median | Harness best | Median total CPU | Median backend CPU | Median memory | Errors |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| baseline | 32,398 | 35,665 | 36,625 | 35,665 | 32,095 | 820.4% | 134% | 479 MiB | 0 / 0 / 0 |
| candidate | 34,820 | 36,408 | 37,233 | 36,408 | 32,683 | 781.6% | 77% | 366 MiB | 0 / 0 / 0 |

The raw median increased 2.08%; the harness result increased 1.83%. Median
total CPU fell 4.73%, backend CPU fell 42.54%, and memory fell 23.59%.

### Standalone JSON regression gate

The retained configuration leaves the switch unset for the standalone image.
The official JSON validator passed all five checks. A three-round diagnostic
without connection retirement produced a median of 153,701 responses for the
baseline and 177,274 for the candidate. Local scheduling varied materially
between rounds, so this result is used only as a non-regression signal, not as
an improvement claim. The official `json-comp` run still belongs on the
benchmark host because this WSL instance exposes only 4,096 ephemeral ports
in its stable network configuration and cannot sustain the profile's
25-request connection retirement rate.

### Decision

Retain the adapter change. It uses an existing framework capability, is
explicitly enabled by the two Gateway stacks, leaves both edge proxies intact,
passes the complete local correctness matrix, and improves both Gateway
profiles while reducing backend CPU.

The local 256-connection H3 and 512-connection H2 trials are excluded: the
smaller host saturated the edge allocation and produced thousands of 5xx
responses in both variants. They do not show a candidate regression.

### Exact continuation point

Use the official benchmark host to run Gateway-H3 at 64/256 connections and
Gateway-H2 at 512/1024 connections from this retained adapter revision. Keep
the change only if those runs preserve the local CPU direction and complete
with zero errors. No additional local source change is needed before that
measurement.

## 2026-10-31 - W1 - eager shared multishot receive retained

### Source identity

- cnetmod worktree base:
  `f60ac87fef42aa92217d1d537427bd400b8653f4`.
- HttpArena revision:
  `2802675ec6a52cb256fd4c6e47f2869e31ecfe04`.
- baseline binary SHA-256:
  `1c3b226799c9b9f251d3e94525cd240c9d8328bfa8b06bbee3e95b069b7413c3`.
- retained candidate binary SHA-256:
  `32026e38ece26d9bd1ab447d20bddf0c58b801f1a759beec7a1fdaa983a6ca07`.

### Environment

- Arch Linux under WSL2, kernel `6.18.54.1-microsoft-standard-WSL2`.
- Clang 23.1.3, libc++, Release, ThinLTO, and `x86-64-v3`.
- bundled mimalloc 3.5.3 and BoringSSL.
- service CPUs `0-15`; load-generator CPUs `16-31`.
- 16 gcannon threads and 512 WebSocket connections.
- one benchmark process ran at a time.

These are paired local diagnostics. They are not leaderboard measurements.

### Profile evidence

With the original adaptive receive policy, pipeline depth one normally keeps
using one-shot receive submissions because a read rarely contains more than
one complete frame. The retained change starts the existing shared io_uring
multishot receiver when the synchronous message loop begins. TLS, non-io_uring,
cancellation, and unsupported-kernel paths retain their existing fallbacks.

At 16 workers, the matched ordinary-echo 20-second runs were:

| Variant | Throughput | p50 | p99 | p99.9 | Errors |
|---|---:|---:|---:|---:|---:|
| adaptive baseline | 422.42K/s | 1.05 ms | 3.85 ms | 8.36 ms | 0 |
| eager multishot | 424.44K/s | 1.05 ms | 3.74 ms | 7.86 ms | 0 |

Throughput increased 0.48%, p99 decreased 2.86%, and p99.9 decreased 5.98%.
The separate three-round five-second series produced medians of 418.38K/s
and 419.66K/s. Its warm-up round varied materially, so the longer matched
run is the primary ordinary-echo comparison.

Related 512-connection profile checks were:

| Variant | Pipeline 16 | Short-lived, 10 frames | Short-lived p99 | Read errors at shutdown |
|---|---:|---:|---:|---:|
| adaptive baseline | 6.60M/s | 199.18K/s | 6.71 ms | 19 |
| eager multishot | 6.65M/s | 203.33K/s | 5.85 ms | 21 |

Pipeline throughput increased 0.74%. Short-lived throughput increased 2.08%
and p99 decreased 12.82%. The load generator reported two additional read
errors while retiring about 102,000 connections; neither run reported connect
or timeout errors. This counter remains a required check in the official
three-round gate.

### Worker-count diagnostic

The unchanged adaptive binary produced 396.73K/s with four workers, 436.82K/s
with eight workers, and 422.42K/s with sixteen workers. Eight workers also
reduced p99 from 3.85 ms to 2.57 ms compared with sixteen workers. This WSL
host exposes sixteen service-side logical CPUs. The result does not justify a
global worker-default change because the same runtime serves every HTTP,
HTTP/2, gRPC, database, and WebSocket profile.

### Profiling

At eight workers, 20 seconds of symbolized `perf` sampling captured 18,350
samples with none lost. Kernel work dominated: `_raw_spin_unlock_irqrestore`
was 12.33%, `handle_softirqs` was 5.32%, and
`__pv_queued_spin_lock_slowpath` was 4.04%. The top cnetmod function was the
WebSocket parse-and-dispatch lambda at 0.68%. The WSL virtual network therefore
limits how much a user-space-only change can demonstrate on this host.

### Rejected candidate

Replacing the small unmasked frame path's two `push_back` calls and `insert`
with one `resize` and `memcpy` looked simpler, but two 20-second runs reached
430.18K/s and 431.72K/s at the otherwise faster eight-worker setting. The
original implementation reached 434.40K/s in the long profiling run and
436.82K/s in the ten-second worker curve. The rewrite was removed completely.

### Correctness and build results

- the candidate adapter compiled in Release mode with Clang 23.1.3;
- focused `test_websocket_frame`: 1 passed, 0 failed;
- ordinary, pipelined, and short-lived end-to-end echo loads all completed;
- no source change was made to frame parsing, masking, control-frame handling,
  TLS behavior, or fallback receive paths.

### Decision

Retain the eager start of the existing shared multishot receiver. It removes
one-shot receive resubmission from ordinary long-lived WebSocket connections,
passes the focused protocol test, improves all three local WebSocket profiles,
and leaves unsupported transports on their prior path.

### Exact continuation point

W2 starts from this candidate. Profile vectored response writes and borrowed
payload lifetime before changing frame storage. The official benchmark host
must repeat all three connection matrices and verify the short-lived read-error
counter before integration.

## 2026-10-31 - W2 - borrowed contiguous WebSocket echoes retained

### Source identity

- cnetmod worktree base:
  `f60ac87fef42aa92217d1d537427bd400b8653f4`.
- HttpArena revision:
  `2802675ec6a52cb256fd4c6e47f2869e31ecfe04`.
- W1 baseline binary SHA-256:
  `73e82dfbb700c304c6ce20c2df2f737fb6dc05bca1eedcda3474d24ec5f576c4`.
- W2 candidate binary SHA-256:
  `cc8fcb72022c2f696785e62bf8bca56a39354ea233d8d8387d4e4b39a9323fb5`.

### Environment

- Arch Linux under WSL2 with 32 logical CPUs.
- Clang 23.1.3, libc++, Release, bundled mimalloc, and mold.
- service CPUs `0-15`; load-generator CPUs `16-31`.
- eight cnetmod workers; 16 gcannon threads; 512 connections.
- one load process ran at a time.

### Change

The parser already validates the incoming client frame and unmasks its payload
in place. A masked client header is four bytes larger than the corresponding
server header. The synchronous echo path now writes the server header into that
available prefix and borrows one contiguous span through completion of the
socket write. A receive burst submits one span per response through
`async_write_gather_all`.

The borrowed path is limited to exact, unfragmented handler input on a
cleartext server connection without a cancellation token. A handler that
changes the payload, TLS, fragmented messages, ping, close, errors, and
cancellable writes keep the previous owning buffer. An io_uring provided
buffer is returned to its ring only after the borrowed write completes.

### Correctness

- Clang 23.1.3 Release adapter build completed.
- focused `test_websocket_frame`: 1 passed, 0 failed.
- the focused test now sends two masked frames in one write and receives both
  echoes through the synchronous message loop.
- ordinary and pipelined runs reported zero connect, read, and timeout errors.
- the generated `AGENTS.md` check and `git diff --check` passed.

### Performance

Three ten-second rounds produced these paired medians:

| Profile | W1 median | W2 median | Change |
|---|---:|---:|---:|
| ordinary echo | 433.29K/s | 461.00K/s | +6.4% |
| pipeline 16 | 6.98M/s | 7.21M/s | +3.3% |
| ten-frame connections | 204.75K/s | 205.04K/s | +0.1% |

Ordinary candidate rounds were 456.14K/s, 461.75K/s, and 461.00K/s.
Pipeline candidate rounds were 7.24M/s, 7.16M/s, and 7.21M/s. The short-lived
read-error counter remained small but noisy: the median was 18 for W1 and 24
for W2 while each round retired roughly 200,000 connections. Neither variant
reported connect or timeout errors. The official host must check that counter
again before integration.

### Decision

Retain W2. It removes the payload copy from the normal echo path, reduces a
pipeline burst to one buffer per response, preserves the fallback behavior,
and improves both scored long-lived profiles without reducing short-lived
throughput.

### Exact continuation point

W3 starts from this candidate and targets upgrade plus connection retirement.
The first profile should separate handshake parsing, accept-key generation,
socket close, and reconnect submissions before changing code.

## 2026-10-31 - W3 - direct HTTP upgrade handoff retained

### Profile

A 20-second ten-frame connection run captured 66,139 samples with none lost.
The WSL network kernel accounted for 71.55% of samples. Kernel spin and wakeup
paths led the report. In user space, mimalloc allocation and release routines
formed the largest cluster; WebSocket accept-key work did not lead the sample.

Code inspection then found two type-erased callbacks in one upgrade. The route
stored `connection_upgrade_handler_with_headers`. Later,
`take_connection_upgrade()` moved that callback and the header snapshot into a
second lambda, then stored the lambda in another `std::function`.

### Change

`request_context` now returns one internal `connection_upgrade_state`. It owns
the existing callback and header snapshot without another wrapper. The HTTP
server invokes the callback with the stored snapshot after writing the 101
response. Both public `upgrade_connection` overloads keep the same signatures.

### Correctness

- Clang 23.1.3 Release adapter build completed.
- `test_http_router`: passed.
- `test_websocket_frame`: passed, including handshake plus first-frame transfer
  and the two-frame pipeline case.
- ordinary and pipeline smoke runs completed with zero errors.

### Performance

The W2 ten-frame rounds were 204.38K/s, 205.04K/s, and 209.53K/s. W3 reached
210.36K/s, 210.84K/s, and 210.22K/s. The median increased from 205.04K/s to
210.36K/s, or 2.6%. A ten-second ordinary run reached 460.14K/s and pipeline
16 reached 7.32M/s, so the other two WebSocket profiles retained W2 performance.
The final binary SHA-256 was
`6473015122b405ab64ec225da90e64a85fac5db3c098bdf33b9c1095dda91e4f`.

### Decision

Retain W3. It removes one allocation from every protocol upgrade without
changing the public API, improves the connection-retirement profile, and does
not reduce the long-lived profiles.

### Exact continuation point

H1 follows W3. Start with the fixed HTTP/1 response path and preserve all W1,
W2, W3, and Gateway checks in the cross-profile gate.

## 2026-10-31 - H1 - parser reset and immediate send rejected

### Environment

- Arch Linux under WSL2 with 32 logical CPUs;
- Clang 23.1.3, libc++, Release, ThinLTO, and `x86-64-v3`;
- bundled mimalloc;
- HTTP service CPUs `0-15`, eight workers; gcannon CPUs `16-31`, 16 threads;
- 4,096 persistent connections, pipeline depth one, ten-second rounds;
- one load process ran at a time.

The WSL ephemeral port range was expanded from its local 4,096-port value
before measurement. A run that reported 82,377 connection failures was
discarded. Later, an unrelated Nexus test temporarily left loopback connects
in `SYN-SENT`; all zero-throughput attempts from that interval were also
discarded.

### Parser reset experiment

The first candidate tracked which of the 18 common request-header slots were
used and cleared only those slots during parser reset. The existing
`bench_http_parser` executable ran seven alternating rounds on CPU 0.

The reused-parser medians were 438.58 ns/op for the existing array fill and
438.94 ns/op for sparse clearing. The candidate was 0.08% slower. Its mask
updates and bit walk cost as much as the avoided stores, so the code and its
temporary regression test were removed.

### Immediate send experiment

The second candidate sent each cleartext HTTP/1 response through
`async_write_gather_all`. On Linux that function attempts non-blocking
`sendmsg` before falling back to the asynchronous write loop. This approach
improved WebSocket echo because one receive burst can carry several response
frames. H1 normally has one outstanding request per connection, so it does
not obtain that batching benefit.

| Variant | Round 1 | Round 2 | Round 3 | Median | HTTP errors |
|---|---:|---:|---:|---:|---:|
| existing one-SQE io_uring write | 414.89K/s | 415.52K/s | 392.83K/s | 414.89K/s | 0 |
| synchronous sendmsg then fallback | 425.68K/s | 374.00K/s | 273.98K/s | 374.00K/s | 0 |

The candidate median was 9.9% lower and became less stable as socket pressure
increased. It was removed. H1 keeps the existing single io_uring SEND and its
partial-write continuation.

### Correctness

- Clang 23.1.3 Release adapter build completed for both candidates;
- `test_http_parser`, `test_http_response`, and `test_http_router` passed;
- all retained benchmark rounds returned only 2xx responses;
- no parser or HTTP send candidate remains in the worktree.

### Exact continuation point

Do not retry sparse slot clearing or a synchronous send before the H1 async
write unless a later batching design changes the one-response-per-read shape.

### Provided-buffer borrowing experiment

A third candidate let a complete request borrow the io_uring provided buffer
through handler execution. Fragmented input converted to parser-owned storage
before the next receive. Pipeline bytes stayed in the same provided block.
The parser, response, router, and WebSocket upgrade tests all passed.

Holding each block through the response write reduced the first valid round to
387.52K/s. Releasing a fully consumed block immediately before SEND recovered
406.76K/s and 405.55K/s. Valid paired baseline rounds ranged from 408.53K/s to
415.52K/s. The small copy was cheaper than the extra lifetime state and buffer
ring pressure, so the parser and server changes were removed completely.

### Final H1 local decision

All three H1 candidates were rejected and the original parser plus one-SQE
response write were restored. The local profile spends most samples in the
WSL network kernel, and the eight-worker run outperformed the sixteen-worker
run. No global worker default changed because the official host has a
different CPU topology and also runs HTTP/2, HTTP/3, gRPC, database, and
Gateway profiles.

### Exact continuation point

Reprofile the unchanged H1 path on the benchmark host with eight physical
cores versus eight SMT pairs before selecting another source change. Preserve
the current parser copy and one-SQE SEND until that comparison identifies a
userspace cost above local noise.

## 2026-11-01 - H1/H2 - parser allocation and connection sharding rejected

### Source identity

- cnetmod worktree base:
  `f60ac87fef42aa92217d1d537427bd400b8653f4`;
- parser baseline binary SHA-256:
  `4b21f796e1884a0802a33e44804dd8d78be4f9d44300ef0728673b407c6d950c`;
- parser candidate binary SHA-256:
  `8088e49010dd82eea217eb9ffe9aedbb8406c0f55bb44c362258b5330c1ccb16`;
- sharded lifecycle candidate binary SHA-256:
  `d3850f59d0c2d1cc2360020d4c80b7580db780f20601a8fca3c246cec246833e`.

### Environment

- Arch Linux under WSL2, kernel 6.18.54.1;
- Clang 23.1.3, libc++, Release `-O3 -DNDEBUG`, mold, bundled mimalloc;
- gcannon 0.6.1, one load process at a time;
- parser A/B used eight workers and 512 connections;
- lifecycle A/B used sixteen workers, 512 connections, and ten requests per
  connection.

### Lazy extension-header index

The candidate removed the eager `unordered_map::reserve(32)` from every
request parser. Common request headers use fixed slots, so the extension map
normally remains empty. Seven matched parser rounds gave these medians:

| Variant | Complete request | Common fixed headers | Fragmented at byte 31 |
|---|---:|---:|---:|
| eager reserve | 441.37 ns | 110.12 ns | 934.97 ns |
| lazy allocation | 443.90 ns | 107.86 ns | 940.79 ns |

The targeted common-header case improved 2.05%. Complete and fragmented cases
moved by less than the 1% local noise gate. End-to-end checks did not carry the
microbenchmark gain:

| Variant | Persistent rounds | Median | Short-lived rounds | Median |
|---|---|---:|---|---:|
| eager reserve | 441.09 / 457.29 / 458.12 K/s | 457.29 K/s | 200.13 / 197.33 / 195.60 K/s | 197.33 K/s |
| lazy allocation | 456.54 / 450.19 / 453.56 K/s | 453.56 K/s | 197.38 / 197.33 / 196.53 K/s | 197.33 K/s |

The persistent median moved down 0.82%, while the short-lived median was
identical. The source change and temporary benchmark case were removed.

### io_uring submission threshold

The unchanged server ran three rotated rounds with submission thresholds 8,
16, and 32. Medians were 456.08K/s, 458.06K/s, and 456.97K/s respectively.
The existing threshold of 16 remained fastest and no configuration changed.

### Connection lifecycle sharding

The H2 candidate replaced the server-wide connection-registration latch with
one latch per event loop. It also used a per-loop active counter when no
connection limit was configured. A configured connection limit retained the
single atomic counter, so admission remained strict. `abort_connections()`
set one terminal flag and then visited every shard under its own latch.

At eight workers, sharding the registration list alone moved the short-lived
median from 219.41K/s to 219.59K/s. The 0.08% difference was below noise. The
complete candidate then ran three alternating rounds at sixteen workers:

| Variant | Round 1 | Round 2 | Round 3 | Median | Median p99 |
|---|---:|---:|---:|---:|---:|
| global latch and counter | 189.76K/s | 175.48K/s | 202.47K/s | 189.76K/s | 7.09 ms |
| per-loop latch and counter | 191.46K/s | 171.95K/s | 202.18K/s | 191.46K/s | 7.21 ms |

Throughput increased 0.90%, which did not pass the required 1% gate. Median
p99 also moved upward by 1.69%. The registry and counter changes were removed
completely.

### WSL high-connection exclusion

Runs with 4,096 connections and only eight local workers depleted or stalled
the local receive path after pressure. The listener remained visible while
new loopback connects stopped advancing. Each worker owns a shared ring with
256 receive blocks, whereas that local matrix assigned 512 connections to a
worker. The official benchmark uses many more server workers. Every zero-RPS
run and the partial 641-RPS run from this mismatched topology was excluded.

### Correctness

- the candidate built with Clang 23.1.3 in Release mode;
- `test_http_parser`, `test_http_response`, `test_http_router`,
  `test_http2_stream`, and `test_websocket_frame` passed before pressure;
- every retained load round returned only 2xx responses;
- no parser, submission-threshold, registry, counter, or temporary benchmark
  change remains in the worktree.

### Decision

H1 is closed locally without another retained source change. H2 remains active,
but the global registration latch and unlimited-mode connection counter must
not be revisited without a benchmark host that demonstrates a gain above 1%.

### Exact continuation point

Profile H2 accept, initial protocol detection, multishot-receive startup, and
connection retirement on a topology where receive-ring capacity exceeds the
connection count per worker. Preserve the current global registration list
until that profile identifies a larger lifecycle cost.

## 2026-11-01 - H2 - receive lifecycle closure

### Source identity

- cnetmod revision: `f60ac87fef42aa92217d1d537427bd400b8653f4`
- cnetmod worktree: `new/httparena-gap-closure`
- compiler: Clang 23.1.3 with libc++, Release, x86-64-v3, ThinLTO,
  mimalloc, and mold

### Experiments

The first candidate carried one multishot receive object from protocol
detection into HTTP/1 instead of creating a second receiver. It removed a
lifecycle boundary, but persistent throughput fell by about 1.8%. The source
change was removed.

The second candidate collapsed multishot cancellation into one suspension.
Both binaries came from the same configured build and differed only in that
change. The strict ten-request connection profile produced these rounds:

| Variant | Round 1 | Round 2 | Round 3 | Median |
|---|---:|---:|---:|---:|
| existing cancellation | 197.07K/s | 188.02K/s | 196.63K/s | 196.63K/s |
| single suspension | 189.34K/s | 192.82K/s | 190.16K/s | 190.16K/s |

The candidate regressed the median by 3.29%. It was removed completely.
`test_http_parser`, `test_http_response`, `test_http_router`,
`test_http2_stream`, and `test_websocket_frame` passed in the Release build.
HTTP/1 and h2c smoke requests also returned the expected body.

### Decision

H2 is closed without another retained change. The earlier WebSocket upgrade
handoff remains; neither receive ownership nor cancellation was changed.

## 2026-11-01 - H3 - delayed-request timer closure

### Environment

- server and load generator ran inside Arch Linux on WSL2;
- the same HttpArena adapter binary selected baseline or candidate through a
  temporary diagnostic switch;
- server workers used CPUs 0-7 and gcannon used CPUs 8-15;
- the matched diagnostic used 1,024 held connections, eight generator threads,
  a ten-second run, and the official `/delay/10` request template;
- every retained round completed without connect, read, or timeout errors.

An attempted 8,192-connection run exceeded the local eight-worker accept and
receive capacity. It returned only connection errors and was excluded before
any comparison was made.

### Timer aggregation experiments

A 250us deadline bucket added visible closed-loop delay. The first matched
round moved from 98.70K/s to 97.46K/s, so the remaining rounds were stopped.
A 10us bucket reduced that delay but still moved the median from 98.80K/s to
98.65K/s, a 0.15% regression.

The next candidate kept exact deadlines. Each event loop held one active
kernel timeout and a user-space minimum heap of sleepers. Its rounds were:

| Variant | Round 1 | Round 2 | Round 3 | Median |
|---|---:|---:|---:|---:|
| dedicated io_uring timeout | 99.02K/s | 98.82K/s | 98.94K/s | 98.94K/s |
| exact shared queue | 98.65K/s | 98.54K/s | 98.85K/s | 98.65K/s |

The heap and timeout-preemption work cost 0.28% at the median. The complete
queue implementation was removed.

### Coroutine-frame experiment

The final candidate let `async_sleep()` await an io_uring timeout directly,
removing the intermediate `async_timer_wait()` task frame without changing
the requested duration or kernel operation. Its matched rounds were:

| Variant | Round 1 | Round 2 | Round 3 | Median |
|---|---:|---:|---:|---:|
| existing sleep path | 99.04K/s | 99.15K/s | 98.71K/s | 99.04K/s |
| direct awaitable | 98.91K/s | 98.93K/s | 98.89K/s | 98.91K/s |

The 0.13% movement did not pass the one-percent retention gate. The awaitable,
diagnostic switch, added test, and local benchmark script were removed.

### Decision

H3 is closed without a retained timer change. Native per-request io_uring
timeouts remain faster than all three user-space aggregation designs on the
available machine. The worktree contains none of the rejected code.

## 2026-11-01 - T1 - TLS small-response write closure

### Environment

- server and load generator ran inside Arch Linux on WSL2;
- Clang 23.1.3, libc++, Release, x86-64-v3, ThinLTO, mimalloc, and mold;
- server workers used CPUs 0-7 and the load generator used CPUs 8-15;
- `h2load` 1.68.0 and `wrk` 4.2.0 were built from their upstream sources;
- every measured run had a two-second warm-up and returned without request
  errors.

### HTTP/2 batch-age experiment

The existing TLS HTTP/2 path already places small HEADERS and DATA frames in
one outbound buffer. Reducing its maximum batch age from 250us to 50us
produced the following matched rounds at 256 connections:

| Variant | Round 1 | Round 2 | Round 3 | Median |
|---|---:|---:|---:|---:|
| 250us | 6.321M/s | 6.372M/s | 6.227M/s | 6.321M/s |
| 50us | 6.334M/s | 6.373M/s | 6.325M/s | 6.334M/s |

The 0.21% median movement did not pass the one-percent retention gate. The
250us value was restored.

### TLS BIO direct-send experiments

The queue BIO sends ciphertext records of at least 8KiB directly and queues
smaller records for the asynchronous socket flush. Two opposite thresholds
were tested with the official JSON-TLS rotation script at 4,096 connections:

| Threshold | Baseline median | Candidate median | Change |
|---|---:|---:|---:|
| 64KiB | 214.25K/s | 212.46K/s | -0.83% |
| 4KiB | 219.30K/s | 213.42K/s | -2.68% |

Both candidates were removed. The existing 8KiB threshold balances the
ciphertext copy and asynchronous submission costs better on this path.

### Decision

T1 is closed without another retained source change. The existing HTTP/1.1
TLS path already emits a small response through one SSL write and one socket
submission. The HTTP/2 path already combines small response frames before its
TLS writer. T2 is now active.

## 2026-11-01 - T2 - HTTP/2 static copy boundary

### Source identity

- cnetmod worktree: `new/httparena-gap-closure`;
- compiler: Clang 23.1.3 with libc++, Release, x86-64-v3, ThinLTO,
  mimalloc, and mold;
- server workers used CPUs 0-7 and load generators used CPUs 8-15.

### Retained change

The static-file path already caches immutable bodies and self-contained HPACK
blocks. Its remaining per-response split came from the 4KiB borrowed-body
boundary. A borrowed response makes the TLS gather writer seal frame metadata
and the body with separate SSL writes. Copying a small body into the connection
batch costs one memory copy but removes that split.

Raising the boundary to 16KiB produced these official static-H2 rotations at
256 connections after a two-second warm-up:

| Variant | Round 1 | Round 2 | Round 3 | Median |
|---|---:|---:|---:|---:|
| 4KiB | 355.07K/s | 349.76K/s | 350.55K/s | 350.55K/s |
| 16KiB | 365.17K/s | 368.34K/s | 362.91K/s | 365.17K/s |

The median improved 4.17%. The candidate returned the same twenty resources
and retained disk revalidation, range handling, flow control, and END_STREAM
behavior.

### Cross-profile gate

| Profile | Baseline median | Candidate median | Change |
|---|---:|---:|---:|
| baseline-H2 | 6.181M/s | 6.317M/s | +2.21% |
| JSON-H2C | 1.064M/s | 1.076M/s | +1.14% |

The 32KiB boundary reduced the median by 0.56% relative to 16KiB and was
removed. Storing one body owner per queued response instead of one per
borrowed frame reduced the median by 0.87% and was also removed.

### Correctness

The HTTP/2 wire-image test now covers both sides of the retained boundary: a
20KiB body emits one borrowed 16KiB DATA payload and a copied 4KiB remainder;
a 1KiB body remains copied. The gather and copied paths still produce
identical bytes. Release tests `test_http2_stream`, `test_http_response`,
`test_http_parser`, and `test_http_router` passed 4/4.

### Decision

Retain the 16KiB boundary and close T2. No rejected threshold, ownership, or
temporary benchmark-script change remains in the worktree. Q1 is active.

## Entry template

Copy this section for every later session. Fill every field or write `not
available` with the reason.

```markdown
## YYYY-MM-DD - ITEM - short title

### Source identity

- cnetmod revision:
- cnetmod worktree state:
- HttpArena revision:
- baseline binary SHA-256:
- candidate binary SHA-256:

### Environment

- host and CPU:
- OS and kernel:
- compiler and standard library:
- Release flags and LTO:
- allocator:
- server workers and affinity:
- client threads and affinity:
- load generator revision:

### Hypothesis

### Code change

### Commands and artifacts

### Correctness results

### Performance rounds

| Variant | Profile | Connections | Round 1 | Round 2 | Round 3 | Median | CPU/request | Memory | p99 | Errors |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|

### Cross-profile gate

### Decision

### Exact continuation point
```
