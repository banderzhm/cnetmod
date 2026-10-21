# gRPC: same-machine acceptance, 2026-10-20

All four subscribed configurations exceed the fastest frozen peer by at least
10%, including the plaintext engine entry. These are local HttpArena comparisons
on an i9-14900K / 32-logical-CPU WSL2 host, not leaderboard-host results.

| Configuration | Matched cnetmod control RPS | Final cnetmod RPS | Fastest peer RPS | Required RPS | Gain vs peer |
|---|---:|---:|---:|---:|---:|
| Plain / 256 connections | 922,376 | 5,547,569 | vanilla 5,006,542 | 5,507,197 | 10.81% |
| Plain / 1024 connections | 587,387 | 5,243,930 | vanilla 4,720,019 | 5,192,021 | 11.10% |
| TLS / 256 connections | 1,035,955 | 5,287,944 | Cardigan 4,455,897 | 4,901,487 | 18.67% |
| TLS / 1024 connections | 600,501 | 4,896,284 | Cardigan 3,814,210 | 4,195,631 | 28.37% |

Each cell is the upstream best of three valid five-second rounds. Candidate
settings are identical across all four cases; exploratory peaks are not mixed.
The selected 78 rounds have zero failed, errored, timeout or non-2xx requests
and continue beyond the initial stream batch. See [summary](final-summary.json),
[all selected rounds](headline-rounds.json), [strict checker](final-checker.json)
and [raw driver logs](raw/).

## Frozen inputs and reproducibility

- Tested runtime source: `70975f8d1b1939cb21cc1a00115a0c276c298c0f`.
- Latest fetched develop: `80e40a01c2092e12c5b851a6ef87676e854a6540`, included
  through merge `4d3a4bb3b0e1b32b6af141ca07eae72533faa2f0`.
- Matched control: `036f369f387272f3689bda72468ccc454b6b3e2d` on the separate
  measurement branch `codex/grpc-control-20261020`; do not merge that branch.
- HttpArena: `8977be2721a5aba7b320345cddfcd6f6b654e698`.
- Candidate binary SHA256:
  `5602f5c35c24ec03feb27d7d6021afd0b47c4c16b706da84cc2de0b6220a01de`.
- Control binary SHA256:
  `a8173b1936057a14f6892f884a6994f33a10a9b942e6729107213aabc77d1a72`.

The control is stronger than bare develop: it includes receive-credit batching
and the duplex cancellation correctness repair needed to avoid first-batch
stalls, plus fixture dual-stack/affinity support. It retains the old asynchronous
unary/owned-protobuf path. Recreate it by applying [control.patch](control.patch)
to the reachable merge commit `4d3a4bb3b0e1b32b6af141ca07eae72533faa2f0`.
It is not claimed to pass the candidate's new zero-send-window oracle.

Both cnetmod binaries use Clang 23, native CPU targeting, ThinLTO, mimalloc,
`-O3 -DNDEBUG -g -fno-omit-frame-pointer`, and the same 16 workers individually
pinned to CPUs 0-15. Client CPUs are 16-31 with 64 client threads. This is an
explicit deployment configuration, not a claim about untuned library defaults.
The official small-host server budget remains 32 CPUs/all CPUs eligible; frozen
peers retain their official runtime configuration and may overlap client CPUs.
All Release builds used at most `-j4`; final LTO link pool is one and lld backend
threads four. See [environment](environment.json), [candidate configure](candidate-configure.json),
[control configure](control-configure.json), [images](images.json),
[peer source parity](peer-source-parity.json) and [toolchains](peer-toolchains.json).

Workload: `/benchmark.BenchmarkService/GetSum`, 256/1024 connections, 100 streams,
three 5-second runs, 2-second cooldown, no added warmup; plaintext 8080/TLS 8443
and official RSA certificate. The official Ubuntu h2load package is nghttp2
1.59.0. Download repairs use signed HTTPS mirrors; no replacement load generator.
Requests decode protobuf and compute the sum every time.

RPS uses successful 2xx divided by printed finished-in wall time, as upstream
does. The upstream field named `p99_latency` is actually the observed maximum;
these artifacts do not establish true p99. At duration cutoff HEADERS-counted
2xx may exceed completed requests; completed must equal succeeded and remain
below or equal to 2xx, with no failures and sustained progress.

Runner changes only scope lifecycle/cleanup to this task and use host-network
validation for the same measured topology. Exact patches are in each raw pass.
`SKIP_TUNE=true` preserves shared host settings. Linux build processes were
sampled every 0.5 seconds; Windows and other VM activity are outside that monitor.
The idle task-owned PostgreSQL startup sidecar was present for every final entry.

## Implementation and checks

The gRPC synchronous small-body adapter and ordinary asynchronous adapter share
protocol validation, message limits, metadata, deadlines, interception,
governance, compression and response formatting. Middleware, large/streamed
bodies and unsupported fast-path shapes fall back to the asynchronous path.
Borrowed protobuf input avoids owned intermediate fields; output remains
computed. Static protocol metadata is shared, while business/error/custom data
remains owned. This is an Adapter/Strategy split, with transport/session and
service policy retaining their separate responsibilities.

HTTP/2 now accounts for outbound stream/connection credit, handles SETTINGS
window shrink and reset, overlaps reading with a bounded independent write pump,
batches ready bytes without waiting for future work, and reuses completed task
slots. Header lookup and a bounded stateless HPACK memo reduce repeated work;
dynamic-table mutations invalidate the memo and sensitive fields are excluded.
TLS enables TCP_NODELAY consistently with plaintext HTTP/2.

The final change omits automatic Content-Length only for the raw gRPC adapter.
The public HTTP/2 default remains enabled and explicit Content-Length is kept.
Full computed message bytes, five-byte prefix, END_STREAM and grpc-status
trailers remain on the wire, following the gRPC HTTP/2 protocol. Body-source
length validation remains active.

- [20 Release suites](release-tests.log) and [the same 20 ASan+UBSan suites](asan-tests.log) pass.
- Independent grpcio checks cover seven nonconstant vectors, signed int32,
  defaults, duplicate/unknown fields, malformed input, unknown methods, and
  24-KiB plain/gzip fallback messages, in both interoperability directions.
- Independent h2 checks complete 300 persistent RPCs in three batches, up to
  100 concurrent streams, on plaintext and TLS. Initial send window is zero;
  seven bytes are granted only after HEADERS. Actual body, trailer status and
  absence of automatic Content-Length are checked.
- Deterministic tests cover send credit, SETTINGS shrink, reset before dispatch,
  cancellation, middleware fallback, static/owned metadata, HPACK ownership,
  stateful blocks, eviction and mutation invalidation. Generic HTTP/2 checks
  preserve default and explicit Content-Length behavior.
- Sanitizers use portable O1, mimalloc/ThinLTO off, mold on; exact options are in
  [configure](asan-configure.json) and [environment](sanitizer-environment.json).
  Leak detection covers normally exiting tests; the terminated smoke fixture
  does not establish clean-exit leak freedom for the benchmark server.

Six supplemental 30-second rounds at 1024 connections are valid with zero errors.
Plain RPS spans 4,604,800-5,271,008 (RSS 353-374 MiB); TLS spans
4,608,870-4,691,557 (RSS 379-438 MiB). They are [stability evidence](final-stability.json),
not replacements for the five-second headline or matched long-duration peers.

## Profiling and excluded experiments

[Matched final perf profiles](perf/profiles.json) use identical 16-worker CPU
placement and official plaintext client arguments, with 499-Hz cpu-clock/frame
pointer sampling. Control has 35,843 samples and candidate 20,932; both lose zero.
They are diagnostic single-server-process runs, separate from official two-port
container headline runs. Full self/inclusive reports are retained under [perf](perf/).
Control self time includes array allocation 6.50%, write_response 2.35% and
array deletion 1.86%; candidate array allocation is 1.73%, with string assignment
6.51%, header validation 3.64% and synchronous service dispatch 3.44%. Percentages
describe different total sampled work and are not standalone speedup estimates.

The [exclusions](exclusions.json) and [experiment records](experiments/) preserve
failed and rejected work. In particular, the earlier `accepted` recheck missed
plain/1024 despite a preceding passing exploratory matrix; only `accepted2`
is the final acceptance. Batch/CQ, worker counts, frame-pointer omission and
several micro-optimizations were tried and rejected; rejected code is absent.
The first Trillium matrix had an error and is excluded in full; a complete clean
12-round recheck of the same frozen image supplies its reference. An incorrect
new test expecting zero timeout to expire was corrected to the existing
zero-means-unlimited contract. Early incomplete perf attachment and a Windows
inventory-contaminated 24-worker exploration are also excluded.

[Artifact hashes](artifact-manifest.json) cover the copied evidence and identify
large binaries/perf.data retained outside Git under `/root/cnetmod-grpc-20261020`.
Raw evidence is stored without Git newline conversion via `.gitattributes`.

## Merge and resource status

The complete `codex/grpc-performance` branch (or a whole-branch squash) contains
the prerequisites and is ready for review. Do not cherry-pick only the final
Content-Length commit or merge the control branch. Linux targeted Release and
sanitizer validation pass; native Windows CI remains a merge prerequisite.
No push, pull request, or merge to develop has been performed by this report.

gRPC heavy work completed and its exclusive test token was released at
`2026-10-20T14:02:56.773269+00:00` (22:02:56 Asia/Shanghai). Own PostgreSQL,
containers and keepalive were cleaned; see [cleanup](cleanup.json). Any new
benchmark run requires the test token again. Database optimization is a new
measurement stage, with separately acquired resource ownership.
