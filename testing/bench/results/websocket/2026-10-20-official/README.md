# Local official WebSocket comparison, 2026-10-20

Follow-up: a buffer-ring recycling bug was found in the pinned load generator. The [corrected-client short-connection retest](../2026-10-20-zero-errors/README.md) is a separate series; original evidence here remains unchanged.

The cnetmod baseline is competitive for one outstanding message per connection,
but all three completed peers outperform it in every pipeline-16 configuration.
Vanilla epoll is fastest among these measured entries (its upstream category is
engine). cnetmod uses less sampled memory than GenHTTP and Fulmine, while Vanilla
uses less than cnetmod in most configurations. The allocation/forwarding
experiment did not establish a general improvement and was reverted; its exact
patch and all measurements remain here. No protocol validation was removed.

These are local WSL measurements using the frozen official driver and workload,
not a reproduction of the upstream benchmark machine or its leaderboard scores.
There is substantial run-to-run variability: use the retained ranges and rounds,
not small differences in best scores, when deciding what to optimize next.

## Inputs and execution

- cnetmod baseline core: `5c969d4ac3b772963df80ff66c93fc8165901dc1`;
  harness: `cd973a1864cba50fe234ca50f57b8a50a3da7dd9`.
- [HttpArena snapshot](https://github.com/MDA2AV/HttpArena/tree/8977be2721a5aba7b320345cddfcd6f6b654e698):
  `8977be2721a5aba7b320345cddfcd6f6b654e698`.
- Official gcannon: `11c802b2bdfe9af2fac4ac2571ae2de4c530cc62`, v0.6.1,
  SHA256 `48d5fa257ca5559f2da88138ff2016adb3191914e3a685ca7e9be0a355eddf2e`.
- Intel i9-14900K, WSL2 32 logical CPUs, 15.46 GiB RAM, Arch Linux,
  kernel 6.18.33.2; clang 23.1.3, libc++ 22.1.8, liburing 2.15.
- cnetmod Release `-O3 -DNDEBUG -g1 -fno-omit-frame-pointer`, portable CPU target,
  mimalloc enabled, ThinLTO disabled. Every build and workload ran serially;
  compilation used at most four jobs. Peer Dockerfiles retain upstream runtime
  options; any build-only changes are recorded in `*-build.patch`.
- Unmodified `scripts/benchmark.sh ENTRY PROFILE --save`, 64 client threads,
  five seconds, three rounds, two-second cooldown, default text `hello` (5 B),
  official 100 ms client warmup. Throughput uses **received** frames / duration.
  The official headline selects the best of three; p99/CPU/memory below are
  from that same selected round. Errors are totals over all three rounds.
- Stock `scripts/validate-ws.py localhost 8080 /ws` gates each measured peer.
  Broad peer registrations keep the official unused PostgreSQL sidecar.

The official server CPU set `0-31,64-95` does not fit this 32-CPU guest. Its
unmodified fallback uses `--cpus 32`; the client's official CPUs 16-31 overlap.
Thus client/server contend for CPUs. There is no dedicated 64-core server or
isolated client here, and the official short duration has noticeable variance.

WSL mirrored localhost initially traversed Windows `loopback0`, with a small
ephemeral-port range. Those preflight failures are excluded from measured runs.
All published runs use a true Linux loopback in the same network namespace for
the client and a dedicated Docker/containerd daemon (`cnetmod-ws-20261020`).
Loopback MTU 1500, port range 1024–65535, tcp_tw_reuse=1, somaxconn=65535,
SYN backlog=65535. `SKIP_TUNE=true` avoids changing the host namespace;
the namespace was configured before testing. Some global net.core settings
cannot be set in that namespace (rmem/wmem maxima remain 4 MiB).
Host settings were restored before measurements; the namespace is removed
after testing. This is an explicitly documented local deviation.

`LOADGEN_DOCKER=false` runs the frozen official gcannon binary.
`ZRK_CMD=/usr/bin/false` avoids building an unused HTTP generator for broad
peer registrations; no measured WebSocket command uses it. Benchmark driver,
profile, validation, statistics and WS generator scripts are unchanged.
See [environment.json](environment.json), [the frozen plan](../../../websocket-official-plan.md)
and [the exact orchestration](reproduce/) for details. The helper scripts contain
this machine's absolute paths; adjust paths when reproducing elsewhere.

## Results

Units: million received frames/s; p99 milliseconds. Memory is sampled server
container memory, not total system/client memory. CPU percentages in
[summary.csv](summary.csv) may exceed 100% because multiple cores are used.

| Profile | Connections | cnetmod M/s | GenHTTP M/s | cnetmod p99 ms | GenHTTP p99 ms | cnetmod memory | GenHTTP memory | Error events cnetmod / GenHTTP |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| echo | 512 | 3.118 | 2.961 | 2.17 | 2.43 | 64 MiB | 1.5 GiB | 0 / 0 |
| echo | 4096 | 2.393 | 2.752 | 14.1 | 11.8 | 178 MiB | 1.8 GiB | 0 / 1 |
| echo | 16384 | 1.642 | 1.430 | 28.2 | 39.3 | 519 MiB | 5.5 GiB | 6 / 0 |
| pipeline 16 | 512 | 11.765 | 30.996 | 16.2 | 4.22 | 63 MiB | 1.4 GiB | 0 / 0 |
| pipeline 16 | 4096 | 12.745 | 20.771 | 32.9 | 21.1 | 178 MiB | 1.7 GiB | 0 / 0 |
| pipeline 16 | 16384 | 3.736 | 16.320 | 258.5 | 56.8 | 555 MiB | 5.4 GiB | 2 / 0 |
| retire after 10 | 512 | 0.783 | 0.930 | 6.45 | 4.98 | 78 MiB | 1.5 GiB | 33 / 46 |
| retire after 10 | 4096 | 0.986 | 1.130 | 38.1 | 36.0 | 197 MiB | 3.3 GiB | 122 / 225 |

Error events are gcannon's connect/read/timeout counters, not an inferred
percentage of failed application messages. Reconnects are expected for the
retire-after-10 profile and are recorded separately. The send/receive count
difference includes in-flight messages at shutdown and is not relabeled as
protocol errors. High-load errors are retained and need investigation; these
results do not establish zero-error operation at all tested loads.

The baseline, rejected candidate and three completed peers account for 120 timed
rounds (eight configurations × three rounds × five entries).
All original round stdout/stderr is under [raw](raw/).
[rounds.json](rounds.json) retains every round's latency percentiles, CPU, memory,
counts, reconnects and errors for every configuration. [summary.json](summary.json)
also includes minimum-to-best throughput information. The parser rejects
incomplete rounds and zero received traffic.

Fulmine.js also completed the same eight configurations and three rounds each:

| Profile | Connections | Fulmine M/s | p99 ms | Memory | Error events, 3 rounds |
|---|---:|---:|---:|---:|---:|
| echo | 512 | 2.556 | 3.52 | 1.1 GiB | 0 |
| echo | 4096 | 2.490 | 10.2 | 1.1 GiB | 0 |
| echo | 16384 | 1.647 | 34.4 | 1.2 GiB | 1 |
| pipeline 16 | 512 | 15.693 | 4.54 | 1.1 GiB | 0 |
| pipeline 16 | 4096 | 13.970 | 14.1 | 1.2 GiB | 0 |
| pipeline 16 | 16384 | 14.637 | 50.2 | 1.3 GiB | 0 |
| retire after 10 | 512 | 0.879 | 4.90 | 1.1 GiB | 37 |
| retire after 10 | 4096 | 1.033 | 28.2 | 1.1 GiB | 135 |

At 16,384 connections / pipeline 16, both peers sustain roughly four times
cnetmod's best throughput, with considerably lower p99. The local memory
tradeoff is substantial. The results warrant investigating batching and
scheduling at high concurrency; they do not establish which internal mechanism
causes the gap.

Vanilla epoll, in the upstream engine category, completed the same matrix:

| Profile | Connections | Vanilla M/s | p99 ms | Memory | Error events, 3 rounds |
|---|---:|---:|---:|---:|---:|
| echo | 512 | 2.933 | 1.78 | 34 MiB | 0 |
| echo | 4096 | 3.086 | 20.4 | 95 MiB | 0 |
| echo | 16384 | 2.036 | 36.3 | 314 MiB | 1 |
| pipeline 16 | 512 | 42.817 | 2.09 | 34 MiB | 0 |
| pipeline 16 | 4096 | 45.813 | 20.4 | 95 MiB | 0 |
| pipeline 16 | 16384 | 31.930 | 33.3 | 304 MiB | 0 |
| retire after 10 | 512 | 1.062 | 4.69 | 86 MiB | 66 |
| retire after 10 | 4096 | 1.732 | 20.1 | 185 MiB | 802 |

Its pipeline throughput is 3.6–8.5 times the cnetmod baseline. Its 4,096-connection
short-lived profile also has more transport error events, so throughput alone
is insufficient to rank connection churn reliability.

## perf and rejected optimization

Software `cpu-clock` at 997 Hz with frame-pointer call chains collected 158,370
samples, zero lost, against the baseline at 512 connections / pipeline 16 for
10 seconds. Hardware PMU events were unavailable in WSL. Kernel addresses cannot
be resolved here, so no exact kernel-function attribution is claimed.
The compact [perf report](baseline-perf-report.compact.txt) resolves user symbols
against the matching baseline runtime rootfs. Raw perf data is retained locally
under `build-ws-evidence/baseline-perf.data`, with size and SHA256 in
[sha256.json](sha256.json).

Flat self costs include aligned_alloc 2.09%, delete[] 1.83%, two mimalloc aligned
allocation paths 1.80% and 1.78%; WebSocket/core forwarding resume functions are
individually around 0.8–1.3%. Frame header parsing is 0.83%, building 0.56%,
masking 0.43%. These are sampling observations, not additive exclusive subsystem
totals or proof that allocation alone explains the peer gap.

[rejected-candidate.patch](rejected-candidate.patch) removes forwarding coroutine
frames from text/binary send, ws_context send/recv, and private I/O dispatch.
Framing, masking, UTF-8, control frames, fragmentation and error checks remain
unchanged. It passed the unit test, the official WS validator, and extended
WS/WSS boundary/fragment/ping/pipeline/close checks before the full retest.

| Profile | Connections | Baseline M/s | Candidate M/s | Change in best |
|---|---:|---:|---:|---:|
| echo | 512 | 3.118 | 2.697 | -13.5% |
| echo | 4096 | 2.393 | 2.388 | -0.2% |
| echo | 16384 | 1.642 | 1.193 | -27.3% |
| pipeline 16 | 512 | 11.765 | 13.356 | +13.5% |
| pipeline 16 | 4096 | 12.745 | 12.330 | -3.3% |
| pipeline 16 | 16384 | 3.736 | 4.367 | +16.9% |
| retire after 10 | 512 | 0.783 | 0.916 | +17.1% |
| retire after 10 | 4096 | 0.986 | 0.906 | -8.1% |

This run did not establish a safe general performance improvement. The candidate
is an unsuccessful experiment, not a shipped optimization. Production source is
restored to baseline; the new disconnected-operation regression test and parser tests
remain. A next experiment should isolate read/write batching and allocation
lifetime, then use balanced repeated ordering to separate effects from variance.
No fast path should bypass RFC checks or expose mutable buffer lifetimes.

## Other selected peers

The frozen official scoring code selected three distinct standard frameworks
(GenHTTP Ioxide, WTX, Fulmine.js) and three standard engines (Zix, Mongoose,
Vanilla epoll). Directory names matter: `wtx-ws`, `zix-ws` and `fulmine` are
the selected WebSocket entries, not similarly named HTTP/gRPC entries.

WTX's pinned Rust base image pull stalled. Zix's official Zig fetch retried
Codeberg and GitHub and failed with `HttpConnectionClosing`. Mongoose failed
Ubuntu package retrieval through the local proxy (HTTP 502); a retry over HTTPS
with the host public CA bundle failed TLS connection closure, and direct
connections also failed. Signature and certificate checks stayed enabled.
Build logs retain the failures; these peers have no comparable local result.
GenHTTP, Fulmine and Vanilla completed builds and all formal measurements.
Vanilla's initial validation-only preflight omitted the PostgreSQL sidecar and
failed startup. The validation helper was corrected to use the official
`postgres_start` lifecycle, after which validation passed. The unmodified timed
driver already starts that sidecar; no failed preflight is included in results.

## Verification and integrity

The official gate reports 7 passed / 0 failed for each measured entry. Extended
cnetmod WS and WSS wire checks cover frame-size boundaries through 65,536 bytes,
partial headers, binary/text, fragmented text with interleaved ping, ordered
pipeline 16, and close. WSS is a correctness check only; throughput is plaintext.
Release `test_websocket_frame` and all ten Python diagnostic/official analyzer
tests pass. Both the candidate and restored baseline pass extended WS/WSS checks;
logs are under [verification](verification/). No Windows-native performance or
TLS performance claim is made.
The restored Release server's SHA256 exactly matches the measured baseline
binary; [server-binaries.json](verification/server-binaries.json) records both
hashes and the distinct candidate hash.

SHA256 checksums cover the captured artifacts. No certificate private key or
dependency source tree is included. Upstream registration/result outputs are
allowed to change; tracked driver inputs were checked separately for changes.
The test token was released after final validation. Dedicated containers,
Docker/containerd services and network namespace were stopped. The host network
matches its original snapshot; [cleanup.json](verification/cleanup.json) records
the checks. No benchmark or high-load process remains from this task.
