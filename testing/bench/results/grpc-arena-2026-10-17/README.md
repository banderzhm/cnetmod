# gRPC / HTTP2 performance — 2026-10-17

**Acceptance status updated:** this is an exploratory local comparison using
the custom harness. It is not an official HttpArena run and does not satisfy
the later requirement to use the official driver, statistics and competitors.
The official-suite comparison and merge acceptance remain pending. See
[official preparation](../../HTTPARENA_GRPC_OFFICIAL.md) for the locked suite,
differences and prerequisites. The historical measurements below are retained
as local profiling evidence, not as a leaderboard or official-suite claim.

The same-host median throughput improved **35.6%–68.9%** across all four
profiles. Median per-round p99 decreased in every profile. All 24 accepted
measurement rounds had zero HTTP/request/network failures and passed their
independent grpcio checks before and after load. The exclusive performance
token was released after profiling; all owned load and perf processes exited.

## Commits and merge scope

- Remote develop baseline: `5c969d4ac3b772963df80ff66c93fc8165901dc1`.
- Identical benchmark tooling on baseline: `d3bf57de58f39e5607babaaac5ee357d3101574f`.
- Measured production optimization: `eaac67735ae542de082765d2110212324adc2d40`.
- Branch: `codex/grpc-performance`; no push to main.

The production change is confined to the transport-neutral HTTP/2 session.
Consumed body credit accumulates in a short project-owned CAS-protected queue,
then one structured task on the connection loop returns the batch with one
connection WINDOW_UPDATE and transport flush. This retains cross-loop
notifications, actual-consumption accounting, stream retirement and structured
shutdown. Borrowed handlers now also avoid the automatic pre-consumption
connection refill that could double-count their credit. A 48 KiB, two-stream
regression checks no early credit and exactly 49,152 consumed bytes returned
in one connection update. Existing interleaved-body, flow-control and
single-dispatch checks remain green.

The earlier local-only merge recommendation is superseded by the official-suite
acceptance requirement above. Public callable APIs
are unchanged; consumers should rebuild their C++ module artifacts because
the session's private layout changed. Runtime verification here is Linux/WSL;
Windows and macOS runtime checks were not executed. The change adds no
platform-specific production calls. Skill documentation and generated
AGENTS.md were synchronized.

## Fixed conditions and limitations

Intel Core i9-14900K host, Arch Linux under WSL2 kernel
`6.18.33.2-microsoft-standard-WSL2`, 32 exposed logical CPUs. Server workers=4,
all server threads restricted to logical CPUs **0–3**, generator threads=4
restricted to **4–7**. WSL reports these as two separate pairs of SMT cores
per side; host physical scheduling and CPU frequency are not independently
controlled. These are local same-machine results, not HttpArena leaderboard
hardware results.

Clang 23.1.3, libc++/libc++abi 22.1.8, CMake 4.4.3, Ninja 1.13.2, mold 2.42.1,
liburing 2.15 and bundled mimalloc 3.5.3. Both binaries use Release
`-O3 -DNDEBUG -g -fno-omit-frame-pointer`, portable CPU target, no ThinLTO,
HTTP/gRPC enabled and ORM/YAML configuration disabled. Builds used at most
four jobs, with no owned compilation overlapping accepted measurement.

Frozen h2load nghttp2/1.68.0 SHA256:
`e1205b524cb3e048079f95cda068d98612780097c3c5561624426b1ad9fca9f6`.
Baseline binary SHA256:
`20aa07970b5fae3edafaad6c822bcbf7ce97b4b4cf0536de3e8cab6c9cf729f0`.
Candidate binary SHA256:
`aa8e82af1bde912f0d2a2f09ef0be1b5788ca5d72ba6a6c2f4f5542ffd2ea9bf`.

The workload matches the pinned [HttpArena unary contract](https://github.com/MDA2AV/HttpArena/blob/8977be2721a5aba7b320345cddfcd6f6b654e698/site/content/docs/test-profiles/grpc/unary/implementation.md):
9-byte framed SumRequest(1,2), 7-byte framed SumReply(3), 256/1024 connections,
100 concurrent streams per connection and three 5-second rounds. Each round
has a separate 2-second warmup invocation, followed by a new h2load process;
connections are persistent within that invocation. Compression is identity.
TLS uses the same local RSA-2048 certificate, TLSv1.3,
`TLS_AES_128_GCM_SHA256` and ALPN `h2` on both binaries. Plaintext is prior-
knowledge h2c. Per-request logging is enabled on both sides of the comparison.

An initial candidate run encountered concurrent cc1plus/rustc compilation.
The entire run was excluded and a full matrix was repeated after the host
became idle. `excluded-candidate.json` preserves those excluded values.
`clean-host-process-samples.jsonl` records the repeated matrix and profile at
one-second cadence; no active compiler process was observed in that series.
This does not imply bare-metal isolation or absence of all Windows host noise.

## Same-host comparison

Each RPS and p99 entry below is the **median of its three round values**;
the p99 entries are not an aggregate percentile over all three rounds.
CPU time is process CPU seconds divided by successful requests. Median process
CPU utilization was 393.1%–397.9% (100% is one logical CPU), so the gain
reflects less CPU work per request rather than extra server CPUs. Median
CPU time per request fell 26.7%–42.3%. Exact round values, best RPS and complete
load argv are in `measurements.json`.
In every profile, the candidate's slowest accepted round exceeded the
baseline's fastest round; the measured ranges do not overlap.

| Profile | Baseline RPS | Candidate RPS | Change | Baseline p99 ms | Candidate p99 ms | CPU us/request, baseline → candidate |
|---|---:|---:|---:|---:|---:|---:|
| clear-256 | 281,876.8 | 382,114.2 | +35.6% | 119.316 | 86.649 | 14.446 → 10.583 |
| clear-1024 | 210,823.8 | 314,842.2 | +49.3% | 600.726 | 376.958 | 19.538 → 13.046 |
| tls-256 | 304,981.2 | 422,747.6 | +38.6% | 124.064 | 88.263 | 13.253 → 9.547 |
| tls-1024 | 221,111.2 | 373,495.6 | +68.9% | 603.345 | 377.084 | 18.832 → 10.870 |

## Perf findings and verification

Separate diagnostic runs used `perf record -e cpu-clock -F 499 -g -p <pid>`
around an 8-second cleartext 256-connection load, followed by an independent
`perf stat` run with task-clock, context-switches, cpu-migrations and page-faults.
Profiling was performed before choosing the optimization. Profiled requests
are excluded from the throughput table. Hardware cycle counters are
unsupported on this WSL kernel; software sampling is the evidence here.
Baseline/candidate captured 15,838/15,884 samples, with zero lost samples.

| Area examined | Baseline observation | Decision / candidate observation |
|---|---|---|
| HTTP/2 and syscall work | `write_response` 22.40% inclusive; `io_uring_enter` 21.58% inclusive | Credit flushes share a write; `io_uring_enter` 19.95% inclusive |
| Flow credit lifecycle | `return_flow_credit` 17.36% inclusive, 1.23% self | **1.01% inclusive, 0.18% self** after batching |
| Task / stream lifecycle | task-group child variants 3.62% + 1.83% self; stream-map erase 1.38% self | Eliminate per-chunk credit children; retain the structured request task and stream map |
| Buffers / allocation | `operator new[]` 6.00% self; aligned allocation helper 3.91% self | Array allocation remains a hotspot; no allocation shortcut or unsafe shared payload cache introduced |
| HPACK | lookup 0.85%, encode 0.73%, Huffman helper 0.83% self | Leave the stateful codec unchanged for this optimization |
| Protobuf | `decode_message` 1.01% inclusive, 0.12% self | Leave wire decoding unchanged; candidate 1.16% inclusive |

Inclusive percentages overlap and must not be summed. Percentage shares
also change when other work is removed; for example candidate
`write_response` becomes 35.62% inclusive while throughput increases. The
primary evidence for total CPU efficiency is the per-request CPU column above.

The separate 8-second stat runs completed 2,382,496 baseline and 3,014,099
candidate requests without errors. Task-clock was 32,169.81 / 32,116.76 ms;
context switches 115,745 / 133,356; migrations 1,326 / 1,729; page faults
5,458 / 865. These raw counts are not normalized for the higher request count.
Exact commands, h2load output and flat self/inclusive perf tables are retained
under `baseline-profile/` and `candidate-profile/`.

## Correctness and reproduction

The final Release run passed **11/11 selected CTest suites, zero skipped**,
in 1.88 seconds: gRPC, protobuf, HPACK, HTTP/2 streams, three protobuf/codegen
oracles, both cross-process grpcio interop directions, harness parsing and
the new plaintext/TLS server smoke checks. The HTTP/2 suite includes the
new consumption-credit regression. `generate_agents.py --check` and
`git diff --check` passed. See `candidate-ctest.log` and the build commands
in [GRPC_ARENA.md](../../GRPC_ARENA.md).

Actual suite commands (run only while holding the exclusive performance token):

```bash
cd /mnt/c/Users/25007/.codex/worktrees/c315/cnetmod
PY=/opt/cnetmod-ci-venv/bin/python
# replay.py is a snapshot of the actual orchestration. Use a fresh label;
# existing directories are never overwritten. Frozen binaries live in /root.
$PY testing/bench/results/grpc-arena-2026-10-17/replay.py matrix baseline-replay \
  /root/cnetmod-grpc-artifacts/baseline-server d3bf57de58f39e5607babaaac5ee357d3101574f
$PY testing/bench/results/grpc-arena-2026-10-17/replay.py matrix candidate-replay \
  /root/cnetmod-grpc-artifacts/candidate-server eaac67735ae542de082765d2110212324adc2d40
$PY testing/bench/results/grpc-arena-2026-10-17/replay.py profile candidate-profile-replay \
  /root/cnetmod-grpc-artifacts/candidate-server eaac67735ae542de082765d2110212324adc2d40
```

Rebuild commands are in `GRPC_ARENA.md`; the measured candidate build directory
is `/root/cnetmod-grpc-candidate`, with `TMPDIR=/root/cnetmod-grpc-tmp` because
the shared `/tmp` tmpfs filled during other work. Baseline was built during
preparation in `/tmp/cnetmod-grpc-release` with the same settings.

`raw-manifest.json` records paths, byte counts and SHA256 of all accepted raw
request TSVs, original reports and perf data. Large raw TSVs and perf binaries
remain under `/root/cnetmod-grpc-artifacts` in the local `archlinux` WSL
distribution; they are not checked into Git. Small text transcripts are
checked in with trailing whitespace removed; the manifest hashes refer to
the original files under `/root`. `measurements.json` omits repeated cpuinfo text; the full originals
are retained and hashed, and `environment-cpuinfo.txt` is one captured snapshot.
The local TLS private key is not included in the repository.

## All accepted rounds

| Phase | Profile | Round | RPS | Mean ms | p50 ms | p95 ms | p99 ms | Server CPU % | Errors |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| baseline | clear-1024 | 1 | 214486.8 | 445.056 | 461.313 | 554.156 | 600.726 | 399.3 | 0 |
| baseline | clear-1024 | 2 | 210823.8 | 451.140 | 471.009 | 558.779 | 600.068 | 397.8 | 0 |
| baseline | clear-1024 | 3 | 208718.8 | 461.670 | 475.954 | 558.440 | 723.098 | 393.1 | 0 |
| baseline | clear-256 | 1 | 291594.4 | 86.633 | 86.594 | 102.407 | 111.096 | 397.9 | 0 |
| baseline | clear-256 | 2 | 281876.8 | 89.653 | 90.037 | 110.024 | 119.316 | 398.4 | 0 |
| baseline | clear-256 | 3 | 280517.2 | 89.220 | 88.882 | 120.320 | 137.451 | 397.4 | 0 |
| baseline | tls-1024 | 1 | 198056.4 | 481.358 | 504.947 | 588.407 | 603.345 | 394.5 | 0 |
| baseline | tls-1024 | 2 | 222089.0 | 424.618 | 427.500 | 582.965 | 625.082 | 395.3 | 0 |
| baseline | tls-1024 | 3 | 221111.2 | 429.763 | 437.813 | 524.796 | 599.425 | 399.8 | 0 |
| baseline | tls-256 | 1 | 304981.2 | 82.328 | 82.632 | 113.428 | 124.064 | 394.5 | 0 |
| baseline | tls-256 | 2 | 312647.6 | 80.413 | 79.612 | 108.717 | 119.605 | 397.9 | 0 |
| baseline | tls-256 | 3 | 298450.0 | 84.150 | 84.554 | 115.719 | 129.549 | 397.5 | 0 |
| candidate-clean | clear-1024 | 1 | 313090.6 | 312.933 | 323.246 | 377.384 | 393.885 | 397.1 | 0 |
| candidate-clean | clear-1024 | 2 | 314842.2 | 311.061 | 317.823 | 365.186 | 376.958 | 397.1 | 0 |
| candidate-clean | clear-1024 | 3 | 316580.4 | 310.140 | 319.079 | 359.448 | 366.929 | 396.2 | 0 |
| candidate-clean | clear-256 | 1 | 400832.6 | 63.075 | 64.720 | 72.558 | 76.739 | 395.6 | 0 |
| candidate-clean | clear-256 | 2 | 382114.2 | 66.055 | 66.338 | 79.514 | 86.649 | 395.6 | 0 |
| candidate-clean | clear-256 | 3 | 373619.6 | 67.567 | 66.509 | 88.100 | 95.263 | 395.5 | 0 |
| candidate-clean | tls-1024 | 1 | 380591.4 | 255.843 | 259.165 | 314.166 | 336.534 | 392.4 | 0 |
| candidate-clean | tls-1024 | 2 | 373495.6 | 256.381 | 249.368 | 342.773 | 377.084 | 393.1 | 0 |
| candidate-clean | tls-1024 | 3 | 339308.0 | 285.011 | 288.091 | 356.893 | 386.816 | 394.2 | 0 |
| candidate-clean | tls-256 | 1 | 395744.2 | 63.507 | 63.224 | 80.259 | 88.263 | 394.6 | 0 |
| candidate-clean | tls-256 | 2 | 422747.6 | 59.402 | 58.466 | 79.521 | 92.748 | 394.9 | 0 |
| candidate-clean | tls-256 | 3 | 426491.2 | 58.718 | 58.266 | 75.929 | 82.229 | 392.3 | 0 |
