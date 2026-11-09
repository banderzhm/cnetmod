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

## 2026-11-03 - Q1 - reuse inline HTTP/3 request-header storage

### Source identity

- cnetmod worktree: `new/httparena-gap-closure`;
- baseline revision: `2f1ff804`;
- baseline binary SHA-256:
  `1d863e6179a1ca35f024148c4443b273f82090a3e1e7be7fec888ecbdbfcfd50`;
- candidate revision: uncommitted Q1 candidate based on `2f1ff804`;
- HttpArena load generator: nghttp2 `h2load` 1.69.0 with HTTP/3.

### Environment

- Arch Linux on WSL2;
- Clang 23.1.3, libc++, C++23, Release `-O3`, frame pointers, mimalloc;
- server: CPUs 0-15, 16 workers;
- load generator: CPUs 16-31, 64 threads, 64 connections, 64 streams;
- request: `/baseline2?a=1&b=1` over HTTP/3;
- each run used a two-second warm-up and a five-second measurement window.

### Profile finding

The frame-pointer profile placed a repeated allocation under
`flat_map::insert_or_assign()` while QPACK decoded ordinary request headers.
The synchronous inline HTTP/3 handler completed before another inline request
could run on the same session, but its `header_map` vector was destroyed after
every request.

The retained change moves the two request maps into session-local scratch
storage after the handler returns. The next inline parse moves that storage
into the new request and clears its elements while preserving vector capacity.
Coroutine handlers and incomplete requests keep their existing owning paths.

### Rejected candidates

Three smaller QUIC layout candidates were implemented, tested, measured, and
then removed:

- a second inline retransmittable-frame slot reduced the six-run throughput
  median by about 5.0% and increased CPU/request by about 13.8%;
- cache-contiguous retired-stream slot indexing improved its first sample but
  the combined twelve-run median fell about 5.5%, while CPU/request rose about
  5.6%;
- removing the retired-stream slot's explicit used flag reduced its storage
  from about 32KiB to 24KiB per active connection, but combined throughput
  fell about 3.1% and CPU/request rose about 8.2%.

No source from these rejected candidates remains.

### Performance rounds

Two interleaved six-run rotations produced six measurements for each binary.
All twelve runs completed with zero failed requests.

| Variant | RPS rounds (millions) | Combined median | CPU seconds/request median | Change |
|---|---|---:|---:|---:|
| baseline | 5.112 / 6.079 / 6.062 / 5.783 / 5.302 / 5.950 | 5.867M/s | 1.791us | - |
| reused header storage | 4.588 / 6.261 / 6.141 / 5.290 / 5.784 / 6.105 | 5.944M/s | 1.756us | +1.32% RPS, -1.95% CPU/request |

The individual WSL runs were noisy, including one low sample for each side.
The combined median and CPU/request moved in the same favorable direction, so
the candidate passed the local retention gate.

### Correctness

- `test_http3_qpack`: 23/23 passed;
- `test_http3_frame`: 31/31 passed;
- complete QUIC/HTTP3 CTest selection: 41/41 passed after all fixture targets
  were built;
- complete configured Release suite: 100/100 passed;
- the official-parameter load runs returned zero failed requests.

### Decision

Retain session-local request-header and trailer capacity reuse. Q1 remains the
active queue item because this local improvement does not by itself close the
published baseline-H3 gap.

## 2026-11-03 - Q1 - reject value parameters and split owned STREAM headers

### Source identity

- cnetmod revision: `3bfa8df06840e0aab7cdd6221026f87fbd0a63b4`;
- cnetmod worktree: `new/httparena-gap-closure`;
- compiler: Clang 23.1.3, libc++, Release, ThinLTO, mimalloc;
- server CPUs: `0-15`; load-generator CPUs: `16-31`;
- load generator: nghttp2 `h2load` 1.69.0 with HTTP/3.

### Recycled-header value parameters

The candidate passed the two focused HTTP/3 tests. Six interleaved runs per
binary completed without failed requests.

| Variant | RPS rounds (millions) | Median | CPU/request median |
|---|---|---:|---:|
| pointer parameters | 6.023 / 5.691 / 5.671 / 5.843 / 5.930 / 5.963 | 5.886M/s | 3.3102us |
| value parameters | 5.848 / 4.973 / 6.046 / 6.071 / 5.571 / 5.928 | 5.888M/s | 3.2485us |

Throughput moved only 0.03%. CPU/request moved 1.86% in the favorable
direction, but paired throughput direction was unstable. The candidate did
not pass the throughput retention gate and was removed.

### Split owned STREAM header

The candidate stored the STREAM header in `quic_send_frame` and retained the
owned HTTP/3 payload separately. This removed the payload shift performed by
`vector::insert(begin, ...)`. Tests `test_quic_frame`,
`test_quic_loss_detection`, `test_http3_qpack`, and `test_http3_frame` passed.

The host entered a lower and unstable power state during the twelve-run
rotation. The interleaved comparison still moved both guarded metrics in the
wrong direction:

| Variant | RPS rounds (millions) | Median | CPU/request median |
|---|---|---:|---:|
| contiguous frame | 2.875 / 4.219 / 4.971 / 2.040 / 3.192 / 4.757 | 3.705M/s | 4.7219us |
| split header | 3.998 / 5.069 / 3.457 / 2.528 / 1.669 / 2.717 | 3.087M/s | 5.6320us |

The split representation reduced the median by 16.68% and increased
CPU/request by 19.27%. It was removed completely. The worktree returned to the
committed retained Q1 implementation.

### Exact continuation point

Q1 remains active. The next candidate must target a measured cost larger than
the rejected parameter and prefix-layout changes. Do not reintroduce either
candidate without a new profile showing a changed bottleneck.

## 2026-11-03 - Q1 - reject matching-field recycling

The candidate kept the previous request's regular headers separate, then
moved only matching names into the new request while decoding. This preserved
correct behavior when consecutive requests used different header sets and
reused string capacity for stable fields. `test_http3_qpack` and
`test_http3_frame` passed.

Three interleaved runs per binary completed with zero failed requests:

| Variant | RPS rounds (millions) | Median | CPU/request median |
|---|---|---:|---:|
| retained baseline | 5.517 / 5.205 / 5.945 | 5.517M/s | 3.5939us |
| matching-field recycling | 2.287 / 4.875 / 5.139 | 4.875M/s | 3.9269us |

Median throughput fell 11.64%, while CPU/request increased 9.27%. Repeated
ordered-map lookup, erase, element shifting, and reinsertion cost more than
the reused string capacity. The candidate and its temporary benchmark script
were removed completely.

## 2026-11-03 - Q1 - retain generic request-header storage slots

The retained candidate does not search the previous map by name. For each new
regular header, it takes the last available pair from the recycled map,
overwrites both strings, and inserts the pair into the new request. Removing
the last element does not shift the remaining map. Fields absent from the new
request stay outside it and are discarded, so no stale header is exposed.

Three interleaved runs per binary completed with zero failed requests:

| Variant | RPS rounds (millions) | Median | CPU/request median |
|---|---|---:|---:|
| retained baseline | 4.497 / 5.290 / 5.108 | 5.108M/s | 3.6873us |
| recycled storage slots | 4.872 / 5.311 / 5.491 | 5.311M/s | 3.5221us |

Median throughput improved 3.98%, while CPU/request fell 4.48%. All six runs
completed without an HTTP or transport error.

Correctness gates passed:

- focused QPACK and HTTP/3 frame tests: 2/2;
- QUIC-labelled suite: 24/24;
- HTTP/3-labelled suite, including lifecycle, interop, WebTransport,
  migration, multipath, PMTU, loss recovery, and client end-to-end: 33/33.
- complete configured Release suite: 100/100.

The temporary benchmark script was removed. The source candidate remains
because throughput and CPU/request passed in the same direction.

## 2026-11-03 - Q1 - reject complete request recycling

The candidate widened the retained storage from the two header maps to the
complete synchronous `http3_request`. It reset request state before decoding
and kept stable pseudo-field strings and body capacity. Focused QPACK and
HTTP/3 frame tests passed, and all six performance runs completed without an
error.

| Variant | RPS rounds (millions) | Median | CPU/request median |
|---|---|---:|---:|
| header-slot baseline | 4.712 / 5.615 / 5.750 | 5.615M/s | 3.3006us |
| complete request recycling | 3.999 / 5.628 / 5.573 | 5.573M/s | 3.4879us |

Median throughput fell 0.75%, while CPU/request increased 5.67%. Moving and
resetting the full request costs more than preserving its short pseudo-field
strings. The candidate was removed; generic header storage-slot reuse remains.

## 2026-11-03 - Q1 - retain one lookup per recycled header

The retained storage-slot path originally called `find()` before
`insert_or_assign()`. The second operation already handles duplicate fields,
so the first ordered lookup was redundant. Removing it leaves duplicate-header
semantics unchanged and saves one comparison walk per regular header.

Two interleaved rotations produced six runs per binary. All twelve runs
completed without an HTTP or transport error.

| Variant | RPS rounds (millions) | Combined median | CPU/request median |
|---|---|---:|---:|
| two lookups | 5.453 / 4.837 / 5.129 / 5.469 / 5.035 / 5.742 | 5.291M/s | 3.4567us |
| one lookup | 4.517 / 6.142 / 5.716 / 5.381 / 5.758 / 4.771 | 5.549M/s | 3.3766us |

Combined median throughput improved 4.87%, while CPU/request fell 2.32%.
The complete configured Release suite passed 100/100. The temporary benchmark
script and generated interop result were removed.

## 2026-11-03 - Q1 - reconcile request-header reuse

A profile of the slot-reuse candidate still attributed 2.17% of sampled CPU
to destruction of the request `header_map` storage. The slot path reused each
string but released the vector allocation passed into `request_from_frames`.
The existing implementation instead moves the complete map storage into the
new request and clears its elements, retaining vector capacity.

The first direct comparison used six interleaved runs per binary:

| Variant | RPS median | CPU/request median |
|---|---:|---:|
| string slots, one lookup | 5.942M/s | 3.206us |
| whole-map capacity with indexed fields | 6.110M/s | 3.124us |

Whole-map capacity improved throughput 2.81% and reduced CPU/request 2.56%.
All twelve runs completed with zero HTTP and transport errors.

Retaining header names and values across requests required a parallel seen
index so removed and duplicate headers stayed correct. In a second six-run
comparison, that candidate reduced median throughput 5.52% and increased
CPU/request 4.04%. It was removed.

The seen index was unnecessary once only vector capacity remained. A final
six-run direct comparison removed that index and its duplicate lookup:

| Variant | RPS median | CPU/request median |
|---|---:|---:|
| indexed whole-map capacity | 5.763M/s | 3.265us |
| simple whole-map capacity | 5.842M/s | 3.163us |

The simple path improved throughput 1.38% and reduced CPU/request 3.13%. All
twelve runs completed without errors. The working source was restored to the
original simple whole-map capacity implementation. The earlier string-slot
entries remain in this record as experiment history, but they are superseded
and are not part of the retained code or cumulative gain.

The next profile attributed 2.92% of samples to the benchmark adapter's
`std::to_string` call. Replacing it with stack `to_chars` plus
`response.body.assign()` reduced the six-run median throughput by 4.74% in a
direct interleaved comparison. The adapter change was removed. HttpArena's
production adapter has a different response path and already uses `to_chars`,
so this rejected local rewrite does not change its implementation.

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

## 2026-11-03 - Q1 - reject automatic worker affinity

The local i9-14900K exposes 32 WSL virtual CPUs as 16 SMT core pairs. WSL does
not expose the host P/E classification, so the bounded curve used only 4, 8,
and 16 workers on server CPUs `0-15`; h2load stayed on CPUs `16-31`.

The unpinned medians were 2.816M/s, 5.078M/s, and 5.734M/s for 4, 8, and 16
workers. A second interleaved comparison enabled `server_context` automatic
worker affinity. Its 4-worker median fell 11.35%, its 8-worker median fell
1.45%, and its 16-worker median rose 2.20%. The 16-worker CPU/request estimate
rose about 0.7%, so that isolated throughput gain did not offset the lower
worker-count regressions.

The affinity change was removed. The measurements also showed a 3.8x spread
between the busiest and least busy active worker at 16 workers. Linux
`SO_REUSEPORT` connection distribution is therefore a larger Q1 concern than
thread migration, but any replacement must preserve stable QUIC connection
ownership.

## 2026-11-03 - Q1 - reject borrowed static response headers

A non-owning static-header view removed the per-request `shared_ptr` reference
count from the local H3 adapter while preserving the existing owning field.
The focused frame test passed. Three interleaved rounds then ran at 4, 8, and
16 workers.

The 8- and 16-worker medians moved about 4% upward, but their paired rounds did
not keep the same direction. More importantly, the 4-worker median fell about
13.6%. The machine slowed during the sequence, and the candidate did not pass
the cross-worker gate under that noise. The API and adapter changes were
removed rather than exposing a lifetime-sensitive fast path without a stable
gain.

## 2026-11-03 - Q1 - retain owned prepared responses

The next candidate encoded the complete static QPACK HEADERS frame and DATA
frame once. The first implementation sent the wire image through QUIC shared
storage. Its 4-worker median fell from 3.30M/s to 2.92M/s, so that form was
removed. For a response of only a few bytes, the shared owner and pending-range
scheduler cost more than a small copy.

The retained form copies the prepared bytes into the existing owned inline-send
buffer. It still removes per-request status conversion, QPACK encoding, and
HTTP/3 frame assembly. A protocol check with curl returned the same status,
content type, and body through both paths. `test_http3_frame` and
`test_quic_frame` also passed.

Three interleaved rounds used server CPUs `0-15`, client CPUs `16-31`, 64
connections, 64 streams per connection, a two-second warm-up, and a five-second
measurement:

| Workers | Baseline median | Prepared median | Paired median change | CPU/request change |
|---:|---:|---:|---:|---:|
| 4 | 3.021M/s | 3.326M/s | +10.1% | -9.1% |
| 8 | 4.899M/s | 5.134M/s | +4.8% | -4.8% |
| 16 | 6.518M/s | 6.294M/s | +2.8% | -8.7% |

The 16-worker absolute medians crossed because the middle pair moved sharply
with host scheduling. Two of its three matched pairs favored the candidate,
and the paired median remained positive. CPU/request also improved, so the
candidate passed the local hybrid-CPU gate. The official separated load
generator remains the final throughput measurement.

## 2026-11-03 - Q2 - reject cached content-length inline send

Cached static responses keep `content-length` inside their immutable QPACK
block. A candidate retained the parsed length beside that block, which allowed
the synchronous server path to commit the response without creating a send
coroutine.

The official twenty-file URI list and Brotli preference were used for three
interleaved rounds at 4, 8, and 16 workers. Throughput medians moved about
-0.7%, -0.7%, and +6.9%. At 16 workers, however, the three matched pairs moved
-8.9%, +6.9%, and +0.02%, so the apparent absolute-median gain was not stable.
The lower worker counts also failed the retention gate.

The QPACK metadata and inline-send changes were removed. Static-H3 already
reuses QPACK fields and immutable file bodies; its remaining cost is in QUIC
fragment scheduling, packet protection, and UDP submission rather than the one
send coroutine removed by this experiment.

## 2026-11-03 - Q2 - reject cached-path validation deferral

The static handler previously built and traversed a `filesystem::path` before
looking up a cache entry. A candidate moved that work after the fresh-cache
return. This remained safe because entries are inserted only after the same
boundary check succeeds, and stale entries still repeat the check before disk
access.

The 4-worker median improved only 0.6%, below the retention threshold. The
8-worker matched median fell 1.7% during a sustained host-frequency decline.
At 16 workers throughput improved about 3.4% by matched median, but median
CPU/request regressed about 4.5%. The candidate therefore did not improve all
required dimensions and was removed.

## 2026-11-03 - H1 - reject shared prepared integer responses

The HttpArena adapter was changed to prepare every integer response from zero
through 2047 at startup. Values outside that range retained the normal
conversion path, so randomized validation and negative inputs remained valid.
This reused the framework's existing `prepared_response` API and removed
response serialization for the official baseline values.

Three interleaved rounds ran at 4, 8, and 16 workers. The 4-worker median
improved about 2.2%, but the 8-worker median fell about 4.3%. Two of the three
8-worker pairs regressed, and their server CPU per response increased by about
4%. The 16-worker result was unstable and included a large third-round
regression.

The prepared object stores both its body and encoded HTTP/1 response in
`shared_ptr`. Every request therefore increments and decrements two reference
counts. The official workload concentrates those operations on responses 55
and 75, which creates shared cache-line contention as worker count rises. The
adapter change was removed. A later response fast path must avoid shared
ownership traffic without weakening the public lifetime contract.

A follow-up placed 256 prepared responses in thread-local storage. This
removed cross-worker reference-count sharing, but the 4-worker median fell
about 2.5%. The later worker counts were not run because the first required
tier had already failed. That candidate was also removed.

## 2026-11-03 - H1 - reject single-pass query summation

The adapter's generic query summation was changed from two bounded searches
per field to one character scan. Random values, malformed fields, signed
values, and the normal GET/POST workload were checked before each run.

At 4 workers the median was effectively unchanged. The 8-worker absolute
median improved about 1.3%. The 16-worker absolute median improved about 1.4%,
but two of its three matched pairs regressed and its paired median fell about
1.3%. The scan rewrite was removed because the cross-worker direction was not
stable.

## 2026-11-03 - W4 - reject direct single borrowed frame access

The ordinary echo path already borrows the masked receive frame, rewrites its
header in place, and sends that storage without copying the payload. A focused
profile showed remaining samples in the small vector used to expose borrowed
frames to the gather writer. A candidate bypassed that vector when exactly one
frame was queued.

`test_websocket_frame` passed. Three interleaved ordinary-echo rounds then ran
at 4, 8, and 16 workers. The median changes were about +0.5%, -1.6%, and +0.3%.
CPU per response improved at 4 workers, regressed at 8 workers, and improved
slightly at 16 workers. The change was removed because the 8-worker tier
regressed and neither positive tier passed the throughput threshold.

## 2026-11-03 - W5 - reject basic masked-frame parser specialization

A specialized branch decoded complete, unfragmented, masked text and binary
headers directly before falling back to the full RFC parser. The focused frame
test passed. Ordinary echo medians moved about +0.7%, -2.7%, and approximately
zero at 4, 8, and 16 workers. The branch was removed because its extra dispatch
cost exceeded the saved general parsing work at 8 workers.

## 2026-11-03 - W6 - reject larger low-latency submission batches

Raising the io_uring low-latency write threshold from two SQEs to four improved
ordinary echo throughput by about 4.5%, 3.1%, and 3.9% at 4, 8, and 16 workers.
Server CPU per response fell by roughly 4% in all three tiers. Pipeline echo
was neutral to positive. Short-lived echo, however, regressed about 2.4% at 8
workers and 2.0% at 16 workers, with corresponding CPU increases. The global
threshold change was removed.

A follow-up retained threshold two for the first ten messages and selected
threshold four only for sustained connections. Its independent rerun reduced
the 16-worker ordinary-echo median about 3.4% and increased CPU per response
about 3.6%. That adaptive policy and its new public enum value were removed as
well. The existing threshold-two contract remains unchanged.

## 2026-11-03 - J1 - reject gzip level one

The JSON compression adapter was changed from Brotli quality two to gzip level
one. Three interleaved rounds at 4, 8, and 16 workers raised raw throughput by
about 2.0-7.5%. The encoded responses were substantially larger, however. The
three official request shapes grew from 989, 1349, and 1600 bytes to 1126,
1627, and 1958 bytes.

HttpArena scores this profile with the square of the bytes-per-response ratio.
The larger gzip bodies therefore reduce the effective score by roughly 25%
even after the raw throughput gain. The candidate was removed, and Brotli
quality two remains enabled.

## 2026-11-03 - T1 - reject the last HPACK encode memo

The connection encoder saved its most recent header block only when encoding
it did not modify the dynamic table. Fixed two-byte responses improved about
3.9% at 4 workers and 1.9% at 8 workers. At 16 workers throughput was flat,
while CPU per request improved about 1.8%.

The required rotating JSON-H2C gate exposed the cost on cache misses. Its first
two matched 4-worker rounds regressed about 1.4% in both throughput and CPU per
request. The candidate was stopped at that tier and removed. Fixed responses
need a path selected before generic header construction; adding a memo lookup
inside the generic HPACK encoder penalizes dynamic responses.

## 2026-11-03 - T1 - reject the text response HPACK cache

A narrower follow-up cached only consecutive `200 text/plain` response blocks
with the same body length. It bypassed generic field comparison for JSON and
other response types. The focused HTTP/2 stream tests passed.

The first 4/8/16 sweep was distorted by sustained host-frequency decline. An
ABBA rerun at 8 workers added five-second cooling intervals. Its four matched
pairs moved -3.5%, -0.3%, +1.5%, and -4.0%; the paired median was about -1.9%,
and CPU per request moved in the same direction. The cache and its session
state were removed.

## 2026-11-03 - T1 - reject borrowed static HPACK headers

A third candidate added an explicit non-owning HPACK block. Unlike the two
automatic caches, it added no lookup to unrelated responses and avoided
shared-pointer reference counts. The adapter selected a prepared block by the
integer body's digit count.

At 4 workers the three matched rounds moved about -0.9%, -0.8%, and -3.4%,
with worse CPU per request. The 8-worker rounds were approximately flat. A
static-table-only block is larger than the connection encoder's steady-state
dynamic references, so the saved encoding work did not offset the additional
wire bytes. The API and adapter use were removed.

## 2026-11-03 - G3 - retain the stock nginx HTTP/3 edge

The published cnetmod Gateway-H3 rows spend 2654% proxy CPU versus 343% server
CPU at 64 connections, and 2063% versus 251% at 256 connections. The leading
four entries use the stock nginx HTTP/3 module, while the Caddy-backed entries
form a separate 59-92K/s cluster. This comparison isolated the edge before any
adapter change was made.

The local A/B held the cnetmod server image, database, request list, duration,
and CPU split constant. The proxy received CPUs 0-7, the server received 8-15,
and the load generator received 16-31. Each connection count ran three times.
At 64 connections, the best Caddy and nginx results were 11,038 and 16,950
RPS. At 256 connections, they were 7,077 and 15,934 RPS. Every response was
2xx.

The retained HttpArena adapter now uses nginx 1.27 with HTTP/3 `reuseport`, one
worker per cpuset CPU, a 2048-connection keep-alive upstream pool, and the same
two-service topology. The existing bounded stable-response compression cache
remains enabled on the cnetmod backend.

A follow-up changed only the proxy cpuset and tested 4, 8, and 16 workers. The
64-connection medians were 5.30K, 10.65K, and 10.37K RPS. The 256-connection
medians were 6.14K, 6.98K, and 13.19K RPS. Individual rounds were noisy on the
hybrid local CPU, but four workers clearly under-provisioned the edge and the
largest tier produced the best combined result. The official 32/32 proxy and
server split therefore remains unchanged.

## 2026-11-03 - G4 - retain nginx QUIC GSO

The nginx HTTP/3 listener was rebuilt with `quic_gso on`. The comparison used
the same server image, Postgres instance, twenty-URI request list, and CPU
partitions. Each candidate ran three times at 4, 8, and 16 proxy workers for
both 64 and 256 connections. Baseline and candidate runs were interleaved.

At 4 workers, the 64- and 256-connection medians improved from 9.54K and 6.71K
to 11.46K and 10.39K RPS. At 8 workers, they improved from 10.00K and 11.79K to
16.95K and 15.92K. At 16 workers, they improved from 10.04K and 10.18K to
21.68K and 20.79K. The gains were 20.07%, 54.94%, 69.54%, 35.01%, 115.98%,
and 104.16%. Every measured response was 2xx.

The candidate is retained because every required tier improved. The target
tracker applies only the smallest 20.07% gain so that local hybrid-CPU scaling
is not presented as an official-host prediction.

## 2026-11-03 - G5 - reject larger QUIC socket buffers

The QUIC listeners were given explicit 1 MiB receive and send buffers. At four
workers, the 64-connection median moved from 11.43K to 11.43K RPS, while the
256-connection median moved from 10.36K to 10.37K. Both changes were about
0.1%, below the retention threshold. The remaining worker tiers were stopped,
and the buffer directives were removed to avoid multiplying idle kernel memory
across every reuseport socket.

## 2026-11-03 - G6 - reject disabling proxy buffering

The dynamic upstream location was tested with `proxy_buffering off`. At four
workers and 64 connections, the baseline and candidate medians were 11.43K and
11.50K RPS, a 0.56% change. The first 256-connection pair moved from 10.36K to
10.30K RPS. The experiment stopped at that tier because it did not cross the
1% retention threshold and had already produced a negative pair. Default
nginx buffering was restored.

## 2026-11-04 - H3 - align async pressure with the official profile

The earlier timer experiments used only 1,024 connections. This rerun held
512 connections per worker, matching the approximate per-worker pressure of
the official 32,000-connection and 64-worker profile.

Raising `CNETMOD_IOURING_CQE_TICK_LIMIT` from 256 to 1024 moved the 4-worker
three-round response-count median from 1,954,889 to 1,954,696, about -0.01%.
Raising `CNETMOD_IOURING_SUBMIT_BATCH` from 16 to 64 moved a separate baseline
median from 1,958,176 to 1,954,199, about -0.20%. Both configurations were
rejected at the first worker tier.

The baseline scaling curve used 2,048, 4,096, and 8,192 connections at 4, 8,
and 16 workers. Ten-second response counts were 1,962,093, 3,791,440, and
7,212,847 in the recorded rounds, corresponding to roughly 95.5%, 92.6%, and
88.0% of the ten-millisecond theoretical limits.

A second curve fixed the server at 16 CPUs and 8,192 connections. Four, eight,
and sixteen workers produced about 318K, 607K, and 718K RPS. Reducing the
worker count therefore does not recover the scaling loss. The async path keeps
the default CQE limit, submit batch, and one worker per eligible CPU.

## 2026-11-04 - H1 - compare kron and zix hot paths

The source comparison locked kron to `02f662f` and zix to `8b849c6`, matching
their current HttpArena integrations. Ring depth, `NO_SQARRAY`, adaptive
completion waits, busy polling, asynchronous close, a thread-local receive
buffer, and a sharded connection registry were tested separately. None passed
the 1% all-tier gate. The full mapping is recorded in
`httparena-leader-source-comparison.md`.

The profile was first mis-symbolized because a PIE file offset was used as a
virtual address. Correcting the executable segment delta placed the samples in
the H1 parser, multishot receive completion, response reset, and io_uring run
loop. No tracing or WebSocket code executes in the baseline path.

## 2026-11-04 - H1 - reject borrowed request input

The request parser borrowed an io_uring provided buffer through request
handling and copied only when another receive had to extend fragmented input.
The official mixed request set remained correct. At four workers the median
throughput improved about 2.8%, but P99 worsened about 6.5%. At sixteen workers
throughput fell about 4.2%. The implementation was removed because it delayed
buffer-ring returns under concurrent load.

## 2026-11-04 - H1 - reject compact parser fields

Request-line and header offsets were narrowed from 64 to 32 bits. This reduced
each header field from 40 to 20 bytes. A no-LTO diagnostic build looked
positive, but the required Clang 23 Release + ThinLTO run moved about -2.3%,
-1.7%, and +0.7% at 4, 8, and 16 workers. The layout change was removed.

## 2026-11-04 - H1 - reject moving the initial probe string

The initial 8 KiB protocol probe was received into a string and moved into the
H1 parser, removing the first array-to-string copy. The first version also
zero-filled the string and was immediately corrected with C++23
`resize_and_overwrite`. The corrected Release + ThinLTO candidate still moved
the 4-worker median down and the 8-worker median by about -6.5%. The original
uninitialized array plus connection-local reusable parser storage was restored.

## 2026-11-04 - H1 - reject a separate borrowed request-view route API

An exact synchronous route API received a borrowed request view and the
response builder directly. Without middleware, HTTP/1 cleartext, HTTP/1 TLS,
and HTTP/2 skipped `request_context`; middleware retained the ordinary adapted
path. A follow-up also bypassed generic matching for exact view routes.

The first three-tier run was noisy and produced paired median changes of about
+3.5%, +3.7%, and +0.8% at 4, 8, and 16 workers. The exact-match follow-up
moved the 4-worker paired median about -3.4%. The extra route field, branch, and
handler indirection did not produce a stable end-to-end reduction. The API,
transport branches, and adapter use were removed.

## 2026-11-04 - H1 - reject response-body capacity reuse in convenience APIs

`request_context::text/json/html` passed their views directly to the reusable
response body instead of constructing a temporary string and moving it. The
compressed JSON workload moved +0.1%, -0.7%, and +0.4% in three paired
4-worker rounds. The median was below the 1% gate. Small baseline responses
also showed no benefit, so the change was removed.

## 2026-11-04 - H1 - reject a two-entry connection-local route cache

Each HTTP/1 connection cached its two most recent exact method/path matches.
This avoided the shared exact-route hash for the official GET/POST rotation
without sharing mutable state between workers. Three 4-worker pairs moved
about +0.28%, +0.01%, and +6.23%; CPU per request had a median improvement of
only about 0.44%. The last pair coincided with severe host tail-latency noise,
so it was treated as an outlier. The cache was removed before the 8/16-worker
tiers.

## 2026-11-04 - H1 - reject a GET/POST request-line shortcut

The parser recognized GET/POST over HTTP/1.1 before its generic method and
version chains. All other methods and versions retained the original fallback.
Three Clang 23 Release + ThinLTO pairs at four workers moved about -3.6%,
-0.1%, and -3.9%. CPU per request regressed in the same direction. The larger
hot function and its extra dispatch outweighed the removed comparisons, so the
shortcut was removed.

## 2026-11-04 - H1 - reject bitmap-only response header presence

The response builder used its common-header bitmap as the only presence state
and retained inactive string values across reset. At four workers the three
paired throughput changes were about +4.8%, +3.0%, and +1.3%, while median
CPU/request improved about 3.5%. The result did not scale: eight-worker pairs
were about +3.2%, -1.1%, and -3.4%, and sixteen-worker pairs were about -0.1%,
-6.3%, and -6.6%. The change was removed.

## 2026-11-04 - H1 - reject a direct multishot receive awaitable

`io_uring_multishot_recv::async_next()` returned a direct awaitable instead of
creating a nested `task` coroutine. The public call expression and result type
at the `co_await` site stayed unchanged. Four-worker paired throughput had a
median near +0.9%, below the gate. Eight-worker pairs were about -5.6%, +0.8%,
and -6.9%. CPU/request fell while completed requests also fell, which indicates
that the changed suspension layout reduced useful wakeup progress. The task
implementation was restored.

## 2026-11-04 - H1 - reject combined parser metadata compression

The common-header slots were narrowed from 64 to 16 bits, then combined with
32-bit header offsets and lengths. The protocol already caps request headers at
16 KiB, so both representations were range-safe. The combined four-worker
pairs moved about +0.3%, -3.3%, and +1.9%; median throughput improved only
about 0.3%, and CPU/request did not improve consistently. The representation
change was removed before higher tiers because it failed the first-tier gate.

## 2026-11-06 - H1 - reject contiguous provided-buffer storage

The buffer ring used one contiguous byte allocation instead of a vector of
individually allocated blocks. The candidate built with Clang 23.1.3,
Release and ThinLTO. The initial 4,096-connection wrk diagnostic reported
connection errors for both binaries and was discarded. At 1,024 connections,
three error-free four-worker pairs produced these results.

| Round | Baseline RPS | Candidate RPS | Baseline CPU us/request | Candidate CPU us/request |
|---|---:|---:|---:|---:|
| 1 | 407,442 | 406,182 | 7.4366 | 7.3028 |
| 2 | 364,111 | 338,993 | 7.8376 | 8.4515 |
| 3 | 336,295 | 357,486 | 8.4896 | 7.9374 |

The paired throughput median was about -0.31%. The candidate was removed.
The local result does not establish a general advantage for either layout.

## 2026-11-06 - H2 - reject thread-local receive-service lookup cache

Each connection currently acquires the process-wide registry latch when
looking up its loop's shared receive service. A thread-local weak reference,
keyed by the context instance id, bypassed that registry on cache hits.
The official gcannon tool was built at
`11c802b2bdfe9af2fac4ac2571ae2de4c530cc62` and used the adapter repository's
three raw GET/POST templates with `-r 10`, 512 connections, 16 generator
threads, a two-second warm-up and eight-second measured rounds. Server CPU
sets were 0-3 or 0-7, and the generator used 16-31. These are local scaled
diagnostics, with fewer connections and a shorter duration than the full
official run.

| Workers | Round | Baseline responses | Candidate responses | Baseline CPU ticks | Candidate CPU ticks |
|---|---:|---:|---:|---:|---:|
| 4 | 1 | 1,528,828 | 1,521,595 | 2,127 | 2,149 |
| 4 | 2 | 1,452,579 | 1,457,698 | 2,160 | 2,139 |
| 4 | 3 | 1,446,164 | 1,433,126 | 2,165 | 2,209 |
| 8 | 1 | 1,475,078 | 1,509,844 | 2,871 | 2,769 |
| 8 | 2 | 1,567,563 | 1,470,609 | 2,768 | 2,666 |
| 8 | 3 | 1,594,414 | 1,599,094 | 2,734 | 2,685 |

Every completed response was 2xx. Paired throughput medians were about
-0.47% at four workers and +0.29% at eight workers. The cache failed the
retention gate and was removed. Registry contention remains a possible cost,
but this experiment did not demonstrate a stable end-to-end gain.

## 2026-11-09 - J2 - reject direct JSON response generation in the adapter

The adapter's `json_response()` reused its existing `respond_json()` helper.
This replaced a temporary `json::write()` result with `generate_body()` and
`json::write_to()`. Both binaries used Clang 23.1.3 Release + ThinLTO and the
same dataset. Baseline and candidate alternated in AB/BA/AB order. The server
used CPUs 0-3 or 0-7, with the generator on CPUs 16-31.

The H2C diagnostic used all seven official count/multiplier pairs, 256
connections, 32 streams per connection, 16 generator threads, two seconds of
warm-up and eight measured seconds. This scales down the official connection
count and duration. All rounds reported zero failed, errored or timed-out
requests. A 50-item response with multiplier three compared byte-identically.

| Workers | Round | Baseline RPS | Candidate RPS | Baseline CPU ticks | Candidate CPU ticks |
|---|---:|---:|---:|---:|---:|
| 4 | 1 | 661,232 | 706,520 | 3,985 | 3,992 |
| 4 | 2 | 688,716 | 647,167 | 3,999 | 3,994 |
| 4 | 3 | 670,831 | 679,059 | 3,994 | 3,999 |
| 8 | 1 | 964,395 | 938,410 | 7,981 | 7,993 |
| 8 | 2 | 993,792 | 991,230 | 7,969 | 7,980 |
| 8 | 3 | 972,444 | 945,276 | 7,976 | 7,984 |

Ticks include the warm-up interval, so they are reported as raw process CPU
counts rather than measured-window CPU/request. Paired throughput medians
were about +1.23% at four workers and -2.69% at eight workers.

The TLS diagnostic used the official `json-tls-rotate.lua` with wrk, four
server workers, 512 connections, 16 generator threads and eight-second runs.
No socket or non-2xx errors were reported. Baseline RPS was
222,950.98 / 217,826.82 / 226,433.11. Candidate RPS was
197,286.00 / 222,157.32 / 205,413.12. The paired median was about -9.28%.

The generic candidate was removed. The result does not identify allocation as
the cause of the regression. No sixteen-worker run was needed after the
eight-worker H2C regression. The existing H3 candidates remain separate.

Test setup also exposed proxy variables routing curl's localhost health check
through port 7897, and CRLF in the Windows URI file rejected by h2load. Health
checks used explicit proxy bypass and bounded timeouts. H2C passed the seven
URIs as arguments. Setup failures were excluded from measured results.

## 2026-11-09 - M1 - same-binary calibration and HTTP/1 network path

### Reproduction

Added `testing/bench/http1_calibration.sh` and
`testing/bench/http1_calibration_report.py`. Each tier runs three adjacent A/A
pairs of the identical executable, with a separate server lifecycle per run.
The test uses the official gcannon mixed raw templates, 512 connections,
pipeline one, 16 generator threads, two seconds of warm-up and eight measured
seconds. Server CPUs are 0-3/0-7/0-15 and generator CPUs are 16-31.
The script samples process CPU ticks only around the measured generator call.
The report uses successful response counts and the generator's duration,
rejects non-2xx responses and reported errors, and calculates CPU/request.
The generator prints duration at two-decimal precision, so small rounding error
remains in calculated RPS. Raw logs are retained.

Executable SHA-256 before and after the first complete run was
`df8b070eac503d72cb5a7173628e605da980b0b7bfb157c70e2990114d38041d`.
Generator SHA-256 was
`48d5fa257ca5559f2da88138ff2016adb3191914e3a685ca7e9be0a355eddf2e`.
All reported calibration rounds passed the successful-response/error check.
Health checks occasionally failed while the new listener was starting, then
passed before warm-up. Those probes are excluded from measured counts.

### Initial A/A results

| Workers | A/B RPS, pair 1 | A/B RPS, pair 2 | A/B RPS, pair 3 | Largest absolute paired change |
|---|---|---|---|---:|
| 4 | 421,068 / 436,882 | 443,806 / 442,712 | 433,722 / 403,901 | 6.88% |
| 8 | 414,617 / 371,862 | 349,413 / 346,614 | 413,608 / 381,746 | 10.31% |
| 16 | 332,240 / 360,437 | 330,161 / 313,370 | 362,193 / 253,516 | 30.01% |

These runs cannot support a one-percent retention threshold. The eight-worker
paired median was -7.70% even though both labels executed the same file.
Windows CPU samples during testing reached 99.6-100%. Available physical RAM
was roughly 4.2-4.4 GiB. A short process sample also found active
`quark_cloud_drive` and `dllhost`; it does not quantify their contribution to
the measured drift. WSL exposes neither usable frequency readings nor thermal
sensors here. Guest CPU topology does not establish physical P/E-core placement.

### Mirrored-loopback discovery

`ip route get 127.0.0.1` returned a next hop of `169.254.73.249`, interface
`loopback0`, table 127. The first profile contained Hyper-V `netvsc` calls,
66.12% kernel samples and 26.38% executable samples. Major executable sample
addresses symbolized to the H1 request coroutine, `async_write`, response reset,
parser consume and multishot receive. These are sampling shares, not measured
allocation counts or exact coroutine-resume counts.

Changing only the destination to 127.0.0.2 selected Linux `lo`, but its default
source remained 127.0.0.1. Return traffic therefore still traversed mirrored
loopback. The second complete 4/8/16-worker calibration retained large paired
changes of 8.20%, 16.68% and 11.35%. Its profile still contained netvsc calls.

A temporary exact local route then selected source 127.0.0.2 as well:

```bash
ip route add local 127.0.0.2/32 dev lo src 127.0.0.2 table local
```

`ss -tn` confirmed both client and server addresses were 127.0.0.2. Three
eight-worker pairs produced these results with the same executable.

| Pair | A RPS | B RPS | A CPU us/request | B CPU us/request |
|---|---:|---:|---:|---:|
| 1 | 1,443,551 | 1,585,814 | 5.453 | 4.947 |
| 2 | 1,447,901 | 1,440,782 | 5.433 | 5.427 |
| 3 | 1,037,632 | 1,196,442 | 7.410 | 6.461 |

The route correction materially changes local test cost, but the remaining
15.31% maximum paired difference still fails a precision gate. The profile
contained 84.60% kernel and 11.14% executable samples, with
`_raw_spin_unlock_irqrestore` at 26.02%. The higher kernel share accompanies a
different workload rate and does not by itself mean higher absolute kernel
cost/request. No perf samples were lost. Profiles were collected separately
from throughput rounds at 199 Hz with 2 KiB DWARF stacks. They total about
40 MB. The exact temporary route was deleted after testing, restoring the
original source-selection behavior.

### Decision and evidence locations

No runtime change is retained from calibration. Previous one-to-three-percent
local movements need independent confirmation under a calibrated setup before
being used as leaderboard projections. This calibration does not invalidate
published independent results. It also does not isolate a cause for every
earlier candidate regression.

Raw logs, CPU ticks, topology, hashes, memory snapshots and profiles are in:

- `/home/cnetmod-http1-calibration-20261109`;
- `/home/cnetmod-http1-calibration-lo-20261109`;
- `/home/cnetmod-http1-calibration-local-source-20261109`.

Use `HTTP1_HOST` to choose a target and `HTTP1_WORKER_COUNTS` to select tiers.
The original run did not change routes automatically. A same-host Linux-loopback experiment must
verify source and destination routing as well as connected peer addresses.
The report accepts full or single-tier runs. The measurements above establish
the current noise range, not a statistical confidence bound from three pairs.

## 2026-11-09 - M2 - controlled repeatability review

The review tested process reuse, fewer generator threads, Windows boost mode,
an isolated network namespace, longer warm-up and a single-SMT generator mask.
Every row below uses the unchanged M1 executable hash. These are same-binary
controls, so their differences cannot be credited to a source optimization.

| Control | Workers | Pairs | Largest absolute paired RPS difference | Result |
|---|---:|---:|---:|---|
| Exact-source Linux loopback, persistent server, four generator threads | 4 | 3 | 7.71% | Precision gate failed |
| Exact-source loopback, persistent server, eight generator threads, boost disabled | 4 | 3 | 14.98% | Diagnostic only, footer failed |
| Isolated network namespace, persistent server, eight generator threads | 4 | 3 | 6.73% | Precision gate failed |
| Network namespace, persistent server, eight generator threads, boost disabled, 20-second preheat | 4 | 3 | 26.76% | Precision gate failed |
| Same controls | 8 | 3 | 53.81% | Precision gate failed |
| Same controls | 16 | 3 | 4.57% | Precision gate failed |
| Same controls, generator mask 16,18,20,22,24,26,28,30 | 8 | 3 | 36.46% | Precision gate failed |

The measured window was 12 seconds per round, with a separate two-second
warm-up. The 20-second preheat ran once per worker tier where indicated.
All completed rounds passed the report's status/error checks. CPU/request
and complete throughput values remain available in the raw logs through the
report script. No control was selected by dropping an unfavorable pair.

The namespace control used `unshare --net` and enabled only that namespace's
loopback device. It had its own local routes, removing mirrored-loopback and
existing namespace firewall rules as explanations for those runs. The large
remaining differences were not assigned to a specific hardware or scheduler
cause. A Windows sample reported performance-limit flags zero and a 100%
performance limit; this does not establish the absence of every possible
frequency or virtualization effect. The generator source does not explicitly
pin each worker. Restricting its mask to one guest-reported sibling per core
also failed to make the paired results repeatable.

### Temporary host changes and restoration

The active plan was Balanced, GUID `381b4222-f694-41f0-9685-ff5bb260df2e`.
Its AC `PERFBOOSTMODE` was temporarily changed from 2 to 0 for the indicated
experiments, then restored to 2 in PowerShell `finally` blocks. The final read
confirmed AC=2 and DC=2. No process was stopped or reprioritized to clear host
load. Exact-source loopback routes were removed, and temporary network
namespaces ended with their processes. Final checks found no benchmark server
or generator still running.

One early boost diagnostic encountered an EOF syntax error in its footer
because the shell script was edited while its existing process was still
reading the file. Its six raw rounds are retained as diagnostic evidence, but
the run is excluded from formal completion claims. The exit cleanup restored
the route, and PowerShell restored the boost setting. Subsequent controlled
runs used an unedited script throughout and completed successfully.

### Review outcome

The requested one-percent local precision has not been achieved. Historical
small gains and small regressions therefore remain unconfirmed, including
previous local ranking projections. The tracker now labels those columns as
historical extrapolations rather than verified current results. Published
independent measurements remain separate. Existing experimental runtime
changes have not been promoted or removed based on these inconclusive controls.

Evidence directories under `/home` are:

- `cnetmod-http1-steady-aa-20261109`;
- `cnetmod-http1-fixed-power-aa-20261109` (diagnostic footer failure);
- `cnetmod-http1-netns-aa-20261109`;
- `cnetmod-http1-controlled-aa-20261109`;
- `cnetmod-http1-single-sibling-aa-20261109`.

The reusable script now supports `HTTP1_NETNS=1`, `HTTP1_THREADS`,
`HTTP1_GENERATOR_CPUS`, `HTTP1_REUSE_SERVER=1`, `HTTP1_PREHEAT`,
`HTTP1_DURATION` and `HTTP1_PROFILE=0`. `HTTP1_LOCAL_SOURCE=1` is an explicit
option for the exact 127.0.0.2 source route, with cleanup on exit. The script
does not change Windows power settings. Defaults retain the original M1
parameters so that experiments can be reproduced by their recorded settings.

## 2026-11-09 - S1 - HTTP/1 send coroutine experiment

An experimental io_uring send awaitable stored its operation in the HTTP/1
connection coroutine and bypassed the `async_write()` task on ordinary writes.
Partial writes kept the existing remainder/cancellation path. The candidate
built with Clang 23 Release + ThinLTO. Focused stream-write validation passed,
including direct sends, zero-length sends, a closed peer, partial sends,
backpressure and cancellation. Router/parser/response tests passed 91 cases.
HttpArena `validate-frag.py` passed all nine shapes and 1,047 split offsets
with its 200ms pause in an isolated network namespace.

The A/B diagnostic used namespace-local loopback, four server workers on 0-3,
eight generator threads on 16,18,20,22,24,26,28,30, 512 connections, the three
official raw templates, a two-second warm-up and 12 measured seconds. Pair
order was AB/BA/AB. Power settings were left unchanged.

| Pair | Baseline RPS | Candidate RPS | Baseline CPU us/request | Candidate CPU us/request |
|---|---:|---:|---:|---:|
| 1 | 1,040,523 | 1,046,248 | 3.875 | 3.851 |
| 2 | 966,404 | 938,269 | 4.168 | 4.281 |
| 3 | 482,200 | 1,001,127 | 8.318 | 4.021 |

The +107.62% third-pair difference is within the kind of disturbance already
observed in same-binary controls. The median was only +0.55%. There is no
stable throughput evidence supporting retention. The awaitable API, transport
branch and experimental test additions were removed. The reduced child-task
count in the source is not presented as a measured allocation-count result.

The initial partial run under `cnetmod-http1-direct-send-ab-20261109` was
aborted during validation setup and excluded. The complete result is in
`/home/cnetmod-http1-direct-send-clean-ab-20261109`. Temporary validation
targets needed explicit ThinLTO/mold link options; after correction, the tests
passed. No focused test targets remain in the adapter's CMake file.

## 2026-11-09 - S2 - immediate send retested on isolated Linux loopback

The non-cancellable io_uring `async_write()` tried a nonblocking
`send(MSG_DONTWAIT | MSG_NOSIGNAL)` on its owner loop. EAGAIN/EWOULDBLOCK/EINTR
fell back to the ordinary queued operation. Partial writes still returned the
exact written count. The existing partial-write/backpressure/cancellation
suite passed, and official fragmentation validation again passed 9/9 shapes
and all 1,047 offsets. The experiment added no API and changed no power setting.

Using the same diagnostic parameters as S1 gave these three clean pairs.

| Pair | Baseline RPS | Candidate RPS | Baseline CPU us/request | Candidate CPU us/request |
|---|---:|---:|---:|---:|
| 1 | 978,468 | 825,569 | 4.116 | 4.880 |
| 2 | 1,092,393 | 890,160 | 3.689 | 4.529 |
| 3 | 1,079,919 | 957,520 | 3.725 | 4.202 |

Every pair declined, with a paired median near -15.63%. The fast path was
removed at the first tier. This supplies isolated-loopback evidence against
this particular implementation in addition to the earlier mirrored-network
diagnostic. It does not establish a general ranking between syscall/reactor
designs. Raw logs are in `/home/cnetmod-http1-immediate-netns-ab-20261109`.

## 2026-11-09 - J3 - JSON view scratch storage and same-profile A/A

Inspection confirmed the direct JSON string encoder already uses eight-byte
plain-ASCII scans. No duplicate encoder was introduced. The adapter's
`json_response()` instead reused a thread-local array of 50 borrowed item
views, avoiding per-request initialization of all unused slots. Serialization
completed synchronously before reuse. The array occupied 5,200 TLS bytes in
the candidate executable. Each field of each emitted view was still populated
and every response still serialized afresh.

The H2C test used the seven official count/multiplier pairs, 256 connections,
32 streams per connection, eight generator threads on one guest-reported SMT
sibling per core, four server workers, two seconds of warm-up and eight
measured seconds. Both processes ran in a network namespace with only Linux
loopback. Seven response bodies compared byte-identically after every pair.
All rounds reported zero failed/errored/timed-out requests.

| Pair | Baseline RPS | Candidate RPS | Baseline CPU ticks | Candidate CPU ticks |
|---|---:|---:|---:|---:|
| 1 | 908,540 | 931,996 | 3,899 | 3,985 |
| 2 | 885,912 | 923,668 | 3,989 | 3,984 |
| 3 | 886,348 | 902,984 | 3,985 | 3,988 |

CPU ticks include warm-up. The paired throughput median was about +2.58%.
The same-profile A/A used the baseline executable for both labels.

| Pair | A RPS | B RPS |
|---|---:|---:|
| 1 | 763,568 | 696,422 |
| 2 | 741,328 | 528,380 |
| 3 | 672,952 | 790,396 |

Its paired changes were approximately -8.79%, -28.73% and +17.45%, exceeding
the candidate's measured movement. The result therefore remains unconfirmed,
and the scratch-storage candidate was removed. Higher worker tiers and TLS
were not used to rescue a candidate without a valid first-tier noise gate.

Baseline SHA-256 was the M1 hash beginning `df8b070e`; candidate SHA-256 was
`d70768857d330b3fa4f27abe922a00cee522c8cff66a0d1df71b447b465748ee`.
Raw logs are in `/home/cnetmod-json-view-4-h2c-20261109` and
`/home/cnetmod-json-view-aa-4-h2c-20261109`.

All three candidates were reverted. Temporary test source, targets and JSON
scripts were removed. The reusable HTTP harness gained an alternate-binary
option with separate process lifecycles and balanced AB/BA/AB ordering, which
is recorded here so later calibration can distinguish historical AA ordering.

## Q3 — ACK ownership and allocation bounds, 2026-11-09

`decode_ack_frame()` copied its local range vector into a pair, and ordinary
`decode_frame()` copied it again into the frame variant. For a nonempty range
array, the ordinary ACK path therefore needed three vector allocations instead
of one. PATH_ACK already moved into its variant, but still inherited the first
copy. Both return boundaries now transfer ownership.

The decoder also reserved memory from the peer's range count before checking
whether the packet could contain those ranges. Each extra range requires at
least two bytes. The decoder now checks that lower bound before allocation,
using division to avoid multiplication overflow. Zero additional ranges skip
both the allocation preparation and the range loop. ECN values still use the
existing checked varint decoder.

The first candidate checked the allocation bound even for zero ranges. Its
long microbenchmark increased zero-range decode time from 8.02 to 10.64 ns.
That version was superseded by the explicit empty-range path.

The final comparison used Clang 23 Release, ThinLTO, mold and bundled mimalloc,
with one process pinned to guest CPU 0. Three alternating AB/BA/AB process
pairs each measured five samples of five million decodes per range count.
Both executables used identical benchmark source. Medians over 15 samples
per executable follow.

| Additional ACK ranges | Original ns/decode | Final ns/decode | Time change |
|---:|---:|---:|---:|
| 0 | 10.65 | 10.39 | -2.49% |
| 1 | 22.65 | 20.44 | -9.77% |
| 8 | 57.07 | 57.00 | -0.12% |
| 64 | 345.91 | 350.09 | +1.21% |

HTTP/3 throughput and leaderboard gains remain unmeasured in this experiment.
The 64-range timing is slightly worse in this run. Earlier same-host timing
variation prevents treating a one-percent movement as a confirmed regression
or improvement. Retention is based on removing two mandatory vector copies
and rejecting impossible allocation sizes. End-to-end performance remains
unconfirmed, and the target tracker receives no projected score increase.

All 175 tests passed across `test_quic_frame` (53), `test_quic_packet` (34),
`test_quic_loss_detection` (33), `test_http3_qpack` (23) and `test_http3_frame`
(32). Three new frame tests cover ACK/PATH_ACK counts of 2^62-1, truncated
range/ECN bytes, and 0/1/8/256 ranges followed by another frame. They verify
errors, range values and consumed length.
Both `cnetmod-httparena` and `cnetmod-httparena-h3` also rebuilt successfully
through their normal CMake targets after removal of the diagnostic targets.

Raw binaries, benchmark source, scripts and logs are retained under
`/home/cnetmod-ack-decode-20261109`. `long/` contains the superseded guard,
and `empty-fast/` contains the final comparison. Host power and proxy settings
stayed at their pre-run values. The decoder benchmark runs entirely in memory. Temporary adapter
CMake targets and host-side diagnostic files were removed after verification.

## Q4 — Literal header copies, 2026-11-09

QPACK decoded raw values and two kinds of raw names using `reserve()` followed
by a per-byte `push_back()` loop. The three sites now construct or assign the
string from the checked byte range. They cover literal field values, names in
field sections, and names inserted through the encoder stream. Existing size
checks precede the copies. Huffman decoding uses its existing path.

HPACK's raw-string encoder now appends the byte span with one vector insertion.
Huffman selection still runs first, so the encoded representation and its size
follow the existing choice. The production change adds no public API or cache.

A broader candidate also changed three QPACK name-encoding loops. Its first
local run had mixed results, including slower encoding with long values. Those
three edits were removed before the final comparison. A later control using
the original QPACK encoder source also varied substantially, so the earlier
slowdown cannot be attributed conclusively to the encoding edits.

### Local codec experiment

Both binaries used Clang 23 Release, bundled mimalloc, ThinLTO and mold. One
process ran at a time on guest CPU 0. AB/BA/AB process ordering supplied 15
samples per case, with 200,000 operations per sample. The input contained one
36-byte literal extension-header name and 0/16/128/1024 bytes of value. Values
used `0xff` bytes so the encoders selected raw strings. Tables were disabled.

The initial sink read only lengths. Its raw logs remain under `final/`, while
the broader candidate remains under `mixed/`. The final experiment under
`hash/` uses a non-inlined sink that hashes every name and value byte. All 180
paired sample checksums matched. The following medians come from that final
experiment.

| Value bytes | QPACK decode original ns | Final ns | Time change | HPACK encode original ns | Final ns | Time change |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | 98.35 | 28.13 | -71.40% | 98.06 | 74.63 | -23.89% |
| 16 | 133.43 | 28.54 | -78.61% | 133.40 | 112.70 | -15.52% |
| 128 | 304.49 | 36.83 | -87.90% | 489.12 | 343.52 | -29.77% |
| 1024 | 1653.23 | 84.16 | -94.91% | 2722.88 | 2024.20 | -25.66% |

These measurements concern raw literal fields. Indexed fields and Huffman
values have different work. The unmodified QPACK encoder control moved from
-12.74% to +19.37% across sizes. That variation remains a limit on local timing
precision. Full HTTP throughput, CPU/request and leaderboard gains remain
unconfirmed, and the target tracker receives no projected score increase.

### Correctness and entry-point checks

- All 79 tests passed across HPACK (9), QPACK (25), HTTP/2 stream (13) and
  HTTP/3 frame (32).
- Four new tests verify exact HPACK/QPACK wire bytes, embedded zero/high bytes,
  empty values, SSO/length-prefix boundaries, 1024-byte names or values,
  dynamic-table insertion and truncated literal rejection.
- On an isolated Linux loopback namespace, h2c, H2 TLS and H3 each served
  10,000 requests at 4/8/16 workers. All 90,000 returned 2xx, with zero failed,
  errored or timed-out requests. Each run used 64 connections, 32 streams,
  four h2load threads and an extra 128-byte extension header. These are smoke
  checks, and their RPS is excluded from performance claims.
- The official HttpArena `validate-ws.py` passed 7/7 checks at each worker
  count. Six gRPC checks across plaintext/TLS and the same worker counts
  returned the expected framed sum and `grpc-status: 0`.
- Both ordinary adapter targets rebuilt successfully. `git diff --check` and
  `generate_agents.py --check` passed.

Raw binaries, source, scripts and logs are under
`/home/cnetmod-header-codec-20261109`. Traffic ran inside `unshare --net`, and
curl also used `--noproxy '*'`. Host power and proxy settings stayed at their
pre-run values. Temporary diagnostic targets and host source files were
removed after verification. Retain the three QPACK decode replacements and
the HPACK raw-string append in the local working tree.

## Q5 — H1 sampling tool and HTTPS method reuse, 2026-11-09

The official gcannon mixed templates ran inside an isolated Linux loopback
namespace. A same-binary four-worker calibration produced paired differences
of -0.95%, -7.92% and -3.11%, with a six-run range of 12.82%. Raw evidence is
under `/home/cnetmod-http1-profile-20261109-q5`.

The separate eight-worker CPU-clock capture collected 7,793 samples with none
lost. About 82.45% were in the kernel. TCP receive/send, ACK processing, skb
allocation/release and socket locks dominated that part of the capture.

The installed `perf` reported version 6.18.54.1 despite package ownership by
the installed 7.2.10 package. Its build options disabled both libelf and DWARF
unwinding. Consequently the initial userspace report split samples across raw
addresses. The locally cached official package was extracted into
`/home/benchmark-tools/perf-full`, leaving the system executable in place.
The complete executable successfully symbolized the existing capture. Main
executable self-sample shares included the H1 serving coroutine (1.98%), its
receive helper (1.01%), parser consume (0.90%), response reset (0.89%), header
serialization (0.71%), multishot receive (0.71%) and async_write (0.63%).

The calibration harness now accepts `PERF_BINARY`, records its build options,
and requires libelf plus DWARF unwinding when profiling is enabled. A negative
check rejected the incomplete executable before starting a server. A complete
positive run under `/home/cnetmod-http1-profile-20261109-q6` captured 7,932
samples with none lost and emitted symbolized output. That newer perf emitted
an unavailable `/proc/schedstat` metadata warning. The CPU-clock capture and
report succeeded, and this experiment makes no scheduler-statistics claim.

### Rejected HTTPS route candidate

The TLS H1 loop reparsed `parser.method()` during routing although the parser
already stored `method_enum()`. A candidate used the enum with the existing
string fallback. Both executables used Clang 23 Release, ThinLTO, mold and
bundled mimalloc. They ran from `/tmp`, serially inside a network namespace,
with a fixed server affinity of CPUs 0–15 and generator affinity of 16–31.
The official `json-tls-rotate.lua` drove wrk with eight threads and 4,096
connections. Each AB/BA/AB pair used a two-second warmup and six-second load.

| Workers | Paired throughput changes | Paired median | CPU/request median change |
|---:|---|---:|---:|
| 4 | -25.89%, +5.35%, -28.22% | -25.89% | +34.83% |
| 8 | +0.96%, +1.72%, +2.30% | +1.72% | -0.85% |
| 16 | +1.52%, +8.79%, +15.66% | +8.79% | -5.28% |

All loads completed without reported socket errors or non-2xx responses.
Six method/status/body comparisons and all seven JSON shapes matched between
executables. The four-worker result failed the retention gate, so the route
edit was removed. The large A/A variation also prevents attributing the
observed differences solely to a small method-parsing change. Raw binaries,
the script and every round are under `/home/cnetmod-h1-tls-routing-20261109`.

## Q6 — Buffered chunked response integrity and writes, 2026-11-09

### Reproduced protocol defect

Both H1 senders used `std::move(resp.body())` and then `set_body("")` before
serializing headers. `body()` returns a string_view, so moving it retained a
borrowed view. The setter could clear or release its storage and also installed
`Content-Length: 0` beside the selected `Transfer-Encoding: chunked`.

Two real-network regression tests failed on the original implementation. They
now pass on plaintext and TLS. Each test pipelines six buffered responses
covering owned/shared bodies of 12, 8,192 and 16,385 bytes, then a normal
Content-Length response on the same connection. The payloads include zero and
high bytes. The tests verify exact bodies, exclusive chunked framing and the
final response boundary.

The senders now retain response ownership and use the existing
`append_serialized_headers_to()` API. The HTTP Server skill documents the
borrowed-body lifetime and header-only entry point, and AGENTS.md was generated.

### Send consolidation experiment

The correct reference implementation retained three cancellable writes for
each plaintext chunk. The candidate assembles size, payload and CRLF into one
record and performs one cancellable `async_write_all()`, matching the existing
TLS framing strategy. Partial-write and cancellation handling stay in that
existing primitive. For a 16KiB buffered body, the sender makes four write-all
calls per response instead of eight, including headers and the terminator.

The diagnostic server used a shared 16KiB body with Content-Length removed.
Both comparison binaries included the ownership/header fix, so the performance
reference sent valid responses. An initial diagnostic listener omitted the
official adapter's TCP_NODELAY option and stalled near 11K req/s. That partial
run was stopped and archived under `nagle-limited/`. It is excluded from all
following comparisons. The listener was corrected before rebuilding both
executables.

The final runs used Clang 23 Release, ThinLTO, mold and bundled mimalloc, with
executables in `/tmp`. One load generator ran at a time inside `unshare --net`.
The server used CPUs 0–15, with fixed worker placement; wrk used CPUs 16–31,
eight threads and 512 connections. Every run had a one-second warmup, five-second
measurement and a separate server process. All decoded body checks passed,
and wrk reported no socket errors or non-2xx responses.

| Workers | First paired throughput changes | Median throughput change | Median CPU/request change |
|---:|---|---:|---:|
| 4 | +52.86%, +73.53%, +61.22% | +61.22% | -38.37% |
| 8 | +54.15%, +65.69%, -23.13% | +54.15% | -35.09% |
| 16 | +693.11%, +16.53%, +34.25% | +34.25% | -29.04% |

All rounds remain in the evidence. The negative eight-worker pair and the
99.7K/s sixteen-worker baseline outlier triggered independent reruns.

| Workers | Independent paired throughput changes | Median throughput change | Median CPU/request change |
|---:|---|---:|---:|
| 8 | +55.24%, +58.38%, +42.57% | +55.24% | -35.95% |
| 16 | +27.65%, +32.05%, +19.23% | +27.65% | -26.37% |

Latency and memory medians use the first four-worker set and the independent
eight/sixteen-worker sets. wrk supplied mean latency and p99. It did not supply
p99.9 in this run.

| Workers | Mean latency original → candidate | p99 original → candidate | Peak RSS original → candidate |
|---:|---|---|---|
| 4 | 4,170 → 2,590 us | 7,710 → 5,360 us | 61,740 → 61,856 KiB |
| 8 | 1,350 → 814.5 us | 2,850 → 2,020 us | 72,084 → 76,456 KiB |
| 16 | 548.24 → 356.87 us | 1,250 → 807 us | 105,192 → 108,884 KiB |

The record adds temporary storage while a chunk is being sent. The independent
eight-worker set increased median peak RSS about 4.3MiB. The initial set's
increase was about 6.2MiB. This memory cost is retained alongside the CPU and
latency reductions.

A separate strace check sent 100 sequential requests through one connection.
`io_uring_enter` calls fell from 904 to 504 (44.25%). Independent checks repeated
these counts. Trace timings are excluded from throughput comparisons.

### Regression and disposition

All 106 framework tests passed across SSE/streaming (15), response (30), parser
(30) and router (31). The separate stream-write executable passed partial-write,
backpressure, cancellation and closed-peer checks. The 4/8/16-worker h2c/H2 TLS/H3
smoke matrix served 90,000 successful requests, the official WebSocket checker
passed 21 checks, and all six plaintext/TLS gRPC checks passed.

Both normal adapters rebuilt successfully. Retain the body/framing fix and
plaintext chunk consolidation locally. These gains concern buffered chunked
responses; the ordinary HttpArena Content-Length profiles receive no projected
score increase from this experiment. Full leaderboard changes remain unmeasured.

Raw binaries, scripts, CPU/RSS/latency logs, failed-before and passed-after tests,
and syscall summaries are under `/home/cnetmod-chunked-response-20261109`.
`repeat8/` and `repeat16/` hold the independent comparisons. The scripts and
baseline are sufficient to reconstruct the correct three-write reference.
Temporary test targets and host diagnostic files were removed after verification.
Host power and proxy settings stayed at their pre-run values.
