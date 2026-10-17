# Official WebSocket test plan — prepared, not executed

This plan supersedes the diagnostic runner for performance conclusions. No
benchmark is authorized until the shared host's explicit test token arrives.
Only one framework/profile may generate load at a time; Release compilation
is limited to `-j4`. No measured result or optimization claim exists yet.

## Frozen upstream inputs

HttpArena snapshot:
[`8977be2721a5aba7b320345cddfcd6f6b654e698`](https://github.com/MDA2AV/HttpArena/tree/8977be2721a5aba7b320345cddfcd6f6b654e698).
The initial checkout is the sibling `HttpArena` directory; the prepared native
Linux clone is `/tmp/cnetmod-ws-HttpArena-official`. Use a native Linux checkout
for execution so Windows CRLF conversion cannot affect scripts or hashes. Preserve its tracked
files unchanged. `prepare_ws_official.py` verifies its revision and clean
tracked state of the relevant inputs, hashes the official driver, validation, configuration,
statistics and selected peer implementations, and lists the commands. It
only reads upstream files and writes a manifest; it does not build or test.

```bash
python testing/bench/prepare_ws_official.py --arena ../HttpArena \
  --output /absolute/path/new-official-ws-manifest.json
```

The official source of each part is:

| Part | File in HttpArena |
| --- | --- |
| Lifecycle, three rounds, result selection | `scripts/benchmark.sh` |
| Connections, pipeline, connection retirement, CPU contract | `scripts/lib/profiles.sh` |
| Generator options and received-frame statistics | `scripts/lib/tools/gcannon.sh` |
| Default 64 generator threads, five seconds, three runs | `scripts/lib/common.sh` |
| Container CPU/memory sampling and cgroup accounting | `scripts/lib/stats.sh` |
| Mounts, CPU fallback, readiness, image lifecycle | `scripts/lib/framework.sh` |
| Host tuning and restoration behavior | `scripts/lib/system.sh` |
| Official correctness gate | `scripts/validate.sh`, `scripts/validate-ws.py` |
| Generator build | `docker/gcannon.Dockerfile`, upstream `MDA2AV/gcannon` |
| Workload contract and category rules | `site/content/docs/test-profiles/ws/*/implementation.md` |
| Ranking and peer inputs | `scripts/gen_leaderboard_data.py`, `site/data/results`, `frameworks` |

The WS profiles do not consume a JSON or static-file dataset. Their actual
payload is the generator's default text `hello` (five bytes). Preserve it
exactly; do not substitute `aaaaa`, larger payloads or another client. The
official container still receives the standard dataset/static/cert mounts.
All three profiles use cleartext HTTP/1.1 upgrade at `http://localhost:8080/ws`;
TLS, QUIC and compression are absent. These results cannot establish WSS
performance. Client frames are masked, server frames unmasked; each received
echo is one response, and an upgrade is not counted as a response.

| Official profile | Connections | Pipeline | Messages/connection |
| --- | --- | --- | --- |
| `echo-ws` | 512, 4096, 16384 | 1 | unlimited |
| `echo-ws-pipeline` | 512, 4096, 16384 | 16 | unlimited |
| `echo-ws-limited` | 512, 4096 | 1 | 10 |

Use `benchmark.sh`, not `benchmark-lite.sh`. Retain the high-load 16384
connection rows and every short-connection row. Duration is five seconds,
64 generator threads, three rounds, two seconds between rounds. The driver
retains one server container across the three rounds of a profile/connection
pair. It selects the highest received-frame throughput for the official
headline; retain every round, CPU, memory, mean and p99 rather than silently
discarding the other observations. A median/range may be supplementary only.

The gcannon Dockerfile follows a moving branch by default. Freeze the resolved
gcannon commit, compiler, image ID, liburing revision and binary hash before
execution and use the same generator image for every entry. The already
inspected gcannon source snapshot is `11c802b2bdfe9af2fac4ac2571ae2de4c530cc62`;
it has a 100 ms internal warmup. Verify the chosen official image actually
contains this revision; do not infer that from a tag. No additional two-second
client warmup or server restart per round is part of the official profile.
Alternatively, use the same pinned upstream gcannon native binary through the
official driver's `GCANNON` override with `LOADGEN_DOCKER=false`; this preserves
the official client and all its default workload flags. The prepared binary is
`/tmp/cnetmod-ws-gcannon-src/gcannon` (SHA-256
`48d5fa257ca5559f2da88138ff2016adb3191914e3a685ca7e9be0a355eddf2e`).

## Framework adapter and peers

`httparena-ws/` contains a normal HttpArena framework registration template.
Copy it to two new untracked directories in the frozen checkout:
`frameworks/cnetmod-ws-baseline` and `frameworks/cnetmod-ws-candidate`, with
distinct display names. Its build hook packages a previously compiled Release
`bench_ws_server` artifact in an explicitly supplied immutable runtime image.
The runtime must contain the same ABI-compatible libc++, libc++abi, glibc,
liburing, zlib and libatomic dependencies used to build the artifact, plus
`/bin/sh` and `nproc`. Use the same runtime image for both variants and verify
the binary inside each image. The entrypoint uses port 8080 and one worker
per available logical CPU.

The server adapter uses the public `ws::server` API with ordinary buffers and
an exact echo handler. It computes the upgrade response for every connection.
The workload, validation, client, profiles, driver and statistics remain the
official implementations. The custom Python load runner and codec microbench
are excluded from the final comparison.

The preparation manifest obtains the three leading standard framework entries
and the three leading engine entries by invoking the pinned official
`badge_aggregate` and `badge_composite` functions for the WS family. These are
separate leagues; do not mix them into a single rank. It hashes their actual
framework directories and lists their official commands. Reuse the peer
implementations, dependency pins and Release settings as supplied, recording
resolved compiler/dependency/image versions where Dockerfiles use moving refs.
Do not replace a peer with a custom echo implementation.

The locked snapshot resolves to the following candidates (published snapshot
scores, not measurements on this host):

| League | Rank | Published name | Actual WS framework directory |
| --- | --- | --- | --- |
| Framework | 1 | genhttp-ioxide | `genhttp-ioxide` |
| Framework | 2 | wtx | `wtx-ws` |
| Framework | 3 | fulmine.js | `fulmine` |
| Engine | 1 | zix | `zix-ws` |
| Engine | 2 | mongoose | `mongoose` |
| Engine | 3 | vanilla-epoll | `vanilla-epoll` |

Resolve shared display names through the registration's WS subscriptions.
For example, the website metadata map associates `wtx` with `wtx-grpc`, which
does not implement these profiles. The preparation script refuses ambiguous
WS registrations. Before any peer build, inspect its build parallelism and
limit it to four jobs through the supported build-system mechanism. Record
any necessary build-only concurrency adjustment without changing Release
code-generation flags, source behavior, client or profile parameters.

After the token arrives, run the official validation followed by the three
profiles for each selected entry serially:

```bash
cd /absolute/path/HttpArena
bash scripts/validate.sh ENTRY
bash scripts/benchmark.sh ENTRY echo-ws --save
bash scripts/benchmark.sh ENTRY echo-ws-pipeline --save
bash scripts/benchmark.sh ENTRY echo-ws-limited --save
```

For cnetmod, supply `CNETMOD_WS_BINARY`, `CNETMOD_WS_REVISION` and the immutable
`CNETMOD_WS_RUNTIME` to each official invocation. Peer invocations use their
unchanged build recipes. Capture complete stdout/stderr per command because
the saved headline JSON alone does not preserve all three rounds or all
failure counters. Keep original generated results as well as a comparison
table. No command above has been executed during preparation.

## Host limits and evidence to retain

The current local guest is Arch Linux on WSL2, kernel
`6.18.33.2-microsoft-standard-WSL2`, i9-14900K, 32 logical CPUs, about 15 GiB
guest RAM plus 4 GiB swap, Docker 29.9.1, Clang 23.1.3. Guest available memory
is variable and must be captured at execution. The official published host
snapshot is a Threadripper PRO 3995WX, 64 cores/128 threads, 251 GiB RAM and
Ubuntu 24.04.4. Local results are same-host retests, not a reproduction of
that hardware or an implied leaderboard placement.

The profile requests server CPUs `0-31,64-95`. On this 32-logical-CPU guest,
the unchanged official `framework_start` explicitly falls back to all
available CPUs via `--cpus=32`. The client defaults to CPUs `16-31`; this
overlaps the server allocation. Preserve and report that fallback equally for
all entries. Do not silently substitute the diagnostic runner's separate
four-core sets or change the 64 client threads. A strict non-overlapping
64-core hardware reproduction requires a suitable dedicated host.

Before measurements, record CPU model/topology/affinity, memory and swap,
OS/kernel, governor/virtualization, container CPU/memory settings, Docker/image
IDs, complete compiler and libc++ versions, compile/link flags, source and
dependency SHAs, allocator, TLS/QUIC/compression state, nofile/memlock limits,
port range/reserved ports, TCP settings and loopback MTU. Baseline core source
is remote develop `5c969d4ac3b772963df80ff66c93fc8165901dc1`; the diagnostic
Release artifacts used `-O3 -DNDEBUG -g1 -fno-omit-frame-pointer`, portable CPU
target and no ThinLTO. Reverify all artifacts before official execution.

The official driver stops containers named `httparena-*`, prunes dangling
images/volumes, may restart Docker, changes network sysctls and drops caches.
Its restoration only resets loopback MTU and deliberately leaves several
settings changed. Do not run it while another task holds the host token.
Snapshot affected state before the window and restore the task's network
changes afterward. Keep source scripts unchanged; if shared-host lifecycle
side effects cannot be isolated safely, report the blocker instead of
substituting another runner and labeling it official.

Official CPU is the driver's docker-stats mean; memory is sampled peak.
The driver also captures exact cgroup CPU delta. Preserve those fields and
their sampling window. The official WS parser exposes mean/p99 latency and
received-frame counts; keep the raw client output for reconnects,
connect/read/timeout/upgrade failures, exit status and other available latency
percentiles. Report numerator and denominator for any supplementary error or
success rate. Received echoes alone do not prove payload integrity; require
the official validation to pass first. Do not count frames still in flight
at timer shutdown automatically as failed responses.

The official five-second profiles are short-load observations. Examine all
rounds for failures, crashes, memory growth and latency excursions; a longer
stability run, if needed, must be separately labeled and use the same official
client/configuration for every entry. It cannot replace the required rows.
Use perf only after official timing runs, as a separate diagnostic sample;
never compare instrumented numbers to uninstrumented peers. Hardware PMU
events are unavailable in the currently checked WSL environment, while
`cpu-clock` sampling works.
