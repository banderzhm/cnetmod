# Local WebSocket optimization and the +10% acceptance target

Follow-up: the short-connection read counters were traced to incorrect buffer-ring recycling in the pinned **load generator**. See the [separate corrected-client retest](../2026-10-20-zero-errors/README.md). All original logs and numbers below remain unchanged; corrected-client measurements must not be mixed with these frozen-client scores.

Acceptance here interprets the requested +10% conservatively as beating the fastest peer in each of the eight official configurations. That target is **not achieved**. Both fixed runtime configurations below pass only 3 of 8 throughput checks against the fastest of the three frozen local peers. Protocol checks pass. No per-case peaks from different runtime configurations are combined into a fictitious server score.

The branch now contains production changes, rather than only measurements: validated message batching, per-worker Linux accept listeners, smaller handshake/message state, fast general masking/framing, ownership transfer of parsed headers, preservation of bytes beyond HTTP Upgrade, and a multishot terminal-callback lifetime fix. The rejected I/O experiments are absent from production code.

## Changes and source identity

- `8f37145d89ab93b1802ad8f6a83968f045ec5273`: functional optimization and regression tests. The `cnetmod-ws-final` and `cnetmod-ws-final24-b64` binaries measure this implementation.
- `098d44a3`: moves the same two HTTP buffered-tail definitions into their own implementation unit. The `cnetmod-ws-reviewed16-b64` binary measures this source. Message processing and I/O behavior are unchanged by that relocation.
- `edfc4d4e`: documentation and native errno header in the regression test; no runtime behavior change.
- The generic opt-in `connection::async_process_messages()` uses a borrowed, synchronous message callback and a bounded writer. Ordinary asynchronous recv/send APIs retain their completion behavior. Replies remain individual WebSocket frames; batching adds no wait-for-more-data timer.
- The message loop validates mask direction, reserved bits/opcodes, continuation order, control frames, close codes/reasons, canonical lengths, message limits and UTF-8. It retains partial next-frame bytes, supports interleaved ping/pong, handles partial writes, and reuses buffers after writes complete.
- Linux multi-worker servers accept on separate SO_REUSEPORT listeners and dispatch on the owning loop. Stop cancels each listener on its owner and joins all listener loops. TCP_NODELAY is now the default; explicitly supplied socket options remain authoritative.
- Automatic worker affinity is opt-in and uses the caller’s eligible CPUs, including sparse cpusets. No default worker affinity policy is changed for other applications.
- HTTP request/response parsers can transfer unread bytes after a completed message. WS and WSS upgrade paths retain those bytes; both parser unit tests and coalesced upgrade/frame wire checks exercise this.
- A terminal multishot receive callback caches its continuations before resuming a consumer that can destroy the receiver. A regression destroys the receiver from that terminal continuation. The WS multishot experiment itself was withdrawn.

## Locked method and runtime tuning

The [original baseline/peer report](../2026-10-20-official/README.md) retains the frozen inputs, build patches, baseline data and full environment. HttpArena remains at `8977be2721a5aba7b320345cddfcd6f6b654e698`; gcannon remains at `11c802b2bdfe9af2fac4ac2571ae2de4c530cc62`. The official driver, workload, client placement, validation and statistics scripts are unchanged. No peer reference is lowered using a later, noisier remeasurement.

Every completed case retains all three raw 5-second rounds with the official 64 client threads, 2-second cooldown, 100 ms client warmup, text `hello`, cleartext WS, and no compression. Echo uses pipeline 1, pipeline uses 16, and limited retires after 10 messages. Headline throughput is received frames divided by the measured duration, selecting the official best of three. Latency and errors are retained independently.

Builds and workloads were serial; every Release or sanitizer build used at most `-j4`. Release remains `-O3 -DNDEBUG -g1 -fno-omit-frame-pointer`, portable CPU target, mimalloc enabled, ThinLTO disabled. The WSL guest exposes 32 CPUs. The official fallback keeps a 32-CPU server quota/eligible set and the client on CPUs 16–31. These are local comparisons with CPU overlap, not upstream leaderboard reproductions.

The two tuning configurations are explicitly different deployment settings:

| Entry | Workers | Submit batch | CQ tick limit | Worker CPU placement |
|---|---:|---:|---:|---|
| cnetmod-ws-final | 32 | 8 | 256 | 0–31 |
| cnetmod-ws-final24-b64 | 24 | 64 | 1024 | 0–23 |
| cnetmod-ws-reviewed16-b64 | 16 | 64 | 1024 | 0–15 |

The eligible CPU budget and client settings stay fixed; using fewer pinned workers changes actual scheduling and, with 16 workers, separates server workers from client CPUs. These tuning gains must not be presented as default-configuration or isolated library-only improvements. The [image IDs, executable hashes and Docker configurations](images.json) and [environment](environment.json) identify the measured binaries.

## Complete peer comparison

Units are million received frames/s. The reference is the fastest of GenHTTP Ioxide, Fulmine and Vanilla epoll for that case, including Vanilla’s upstream engine category. The target is the integer ceiling of 1.10 times that reference. Bold cells meet the throughput target; correctness is a separate gate.

| Profile | Connections | Fastest peer M/s | Required M/s | 24 workers M/s (gain) | 16 workers M/s (gain) |
|---|---:|---:|---:|---:|---:|
| echo-ws | 512 | genhttp-ioxide 2.960704 | 3.256775 | **3.469517 (+17.19%)** | **3.371858 (+13.89%)** |
| echo-ws | 4096 | vanilla-epoll 3.086384 | 3.395023 | **3.529773 (+14.37%)** | 2.669764 (-13.50%) |
| echo-ws | 16384 | vanilla-epoll 2.036104 | 2.239715 | 2.085817 (+2.44%) | 1.411670 (-30.67%) |
| echo-ws-limited | 512 | vanilla-epoll 1.062377 | 1.168615 | **1.220286 (+14.86%)** | **2.010571 (+89.25%)** |
| echo-ws-limited | 4096 | vanilla-epoll 1.731927 | 1.905120 | 1.396889 (-19.34%) | 1.773086 (+2.38%) |
| echo-ws-pipeline | 512 | vanilla-epoll 42.817280 | 47.099008 | 45.644668 (+6.60%) | **49.726300 (+16.14%)** |
| echo-ws-pipeline | 4096 | vanilla-epoll 45.813023 | 50.394326 | 43.190700 (-5.72%) | 31.719644 (-30.76%) |
| echo-ws-pipeline | 16384 | vanilla-epoll 31.930238 | 35.123262 | 26.315046 (-17.59%) | 18.169322 (-43.10%) |

The 32-worker end-to-end candidate reaches 43.512 / 44.490 / 25.197 M/s in the three pipeline cases, versus baseline 11.765 / 12.745 / 3.736 M/s. Those substantial baseline gains still do not establish the requested lead over peers. The 24-worker 4096-connection echo throughput gain also comes with a p99 of 22.6 ms versus the reference’s 20.4 ms.

All echo/pipeline cases in the two tuning matrices report zero transport error events. Limited-case read-error totals over all three rounds are material and are not hidden:

| Entry | Limited 512: read errors | Limited 4096: read errors |
|---|---:|---:|
| Original cnetmod baseline | 33 | 122 |
| Vanilla epoll reference | 66 | 802 |
| 24 workers, batch 64 | 83 | 235 |
| 16 workers, batch 64 | 382 | 655 |

Connect and timeout counters are zero for these rows. These are event counters, not counts of unique failed messages; no unsupported error percentage is inferred. Their cause has not been established by the wire correctness checks. A higher throughput number alone does not waive those observations.

The [24-worker target check](cnetmod-ws-final24-b64-target.json), [16-worker target check](cnetmod-ws-reviewed16-b64-target.json), [summary CSV](summary.csv), [all parsed rounds](rounds.json) and [raw logs](raw/) retain exact values, all rounds, latency, CPU, memory, reconnects and error counters. The checker exits unsuccessfully when any case misses the target. It rejects missing configurations/rounds.

## Profiling and withdrawn experiments

The original baseline profile guided removal of per-message coroutine forwarding, allocation and frame-copy work. The initial batch profile and the final high-fanout profile are retained. These profiles use different connection/runtime settings and are not an isolated causal percentage comparison.

The [final 16-worker, 16384-connection pipeline profile](final-perf-report.compact.txt) records 157,243 cpu-clock samples at 997 Hz, with zero lost samples. Kernel symbols are resolved using a saved map. Self samples include `_raw_spin_unlock_irqrestore` 19.39%, `fget` 3.14%, receive/send security hooks 3.07%/2.98%, and TCP receive/send paths. The message loop is 3.90%; parser 1.45%; mask 0.61%; framing/writer values remain in the report. An unlock symbol sample alone does not prove lock-wait contention. The remaining high-fanout limitation needs further network I/O/scheduling work, not removal of protocol checks.

Linked/duplex I/O, a coroutine-free exchange awaiter, multishot WS receive integration, optimistic socket writes, a larger CQ and an epoll selection option did not establish a stable general win and were removed. The [withdrawn duplex/awaiter patch](duplex-awaiter-rejected.patch), [earlier integration snapshot](batch-nodelay.patch) and raw measurements remain as experimental evidence, not recommended implementations. The first `cnetmod-ws-duplex` staging attempt used a stale executable after a failed build and is excluded as a candidate; incomplete/invalid profile attempts are identified by [raw-manifest.json](raw-manifest.json).

Binary perf data and kernel maps are kept locally outside Git, with sizes, paths and SHA256 in [perf-artifacts.json](perf-artifacts.json). The ELF files remain under `/root/cnetmod-ws-20261020/optimization/images/` and the matching runtime under `/root/cnetmod-ws-20261020/runtime-rootfs/`.

## Correctness and cleanup

- Release: `test_stream_write`, `test_http_parser` and `test_websocket_frame` pass. Coverage includes sparse eligible-CPU assignment, inline/deferred dispatch, observed exceptions, 2 MiB partial writes/backpressure/cancellation, closed peer without SIGPIPE, sharded listener stop/join, parser tail ownership, frame boundaries, append-prefix preservation and an unaligned mask oracle.
- The official WS validator passes 7/0. Extended WS and WSS checks cover text/binary size boundaries through 65536 bytes, fragmentation, ping/pong, pipeline ordering and close. The adversarial suite passes 22 malformed cases, valid split UTF-8, partial-tail flushing, an 8192-byte upgrade header and coalesced upgrade/frame bytes.
- ASan+UBSan runs the three C++ test executables plus WS/WSS wire checks successfully, with leak detection enabled for normal unit exits. See [sanitizer unit log](asan/tests.log), [WS wire log](asan/protocol.log) and [WSS wire log](asan/protocol-tls.log). The sanitizer build is O1 with mimalloc disabled and is not used for performance scores.
- Clang 23 initially had compiler-rt 22; the linker failure and mirror 404 are retained. The exact compiler-rt 23.1.3-2 package was fetched from the official Arch archive, checked by pacman, and installed before rerunning sanitizers. See [runtime repair](asan/runtime-repair.log) and [reproduction script](reproduce/install_rt.sh). This happened after all performance measurements and does not change their binaries.
- All 13 Python workload/analyzer/acceptance tests pass. Generated AGENTS.md is synchronized with its skill sources. No Windows-native build/performance or Autobahn certification is claimed.
- The test token was released immediately after final validation. Dedicated containers, Docker/containerd services and the network namespace were removed; the own keepalive was stopped. The host network matches its original snapshot. [Cleanup proof](verification/cleanup.json) records the checks. No additional benchmark was run after releasing the token.

The branch is reviewable as a protocol-preserving optimization with measured tradeoffs. It must not be labeled as completing the +10% requirement under this acceptance criterion.

Reproduction helpers record this workstation’s absolute paths and task keepalive PID; adapt these values to another environment.
