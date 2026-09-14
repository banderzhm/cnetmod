# Disabled HTTP observation benchmark

## Paired evidence analysis

For fresh process-level sampling, build the Release executable first and run:

```text
python testing/bench/run_http_observation.py PATH_TO_EXECUTABLE --processes 8 --output NEW_RESULTS.json
```

The runner owns an ephemeral loopback Python peer, starts a fresh client process
for every sample, enforces a 60-second timeout per process, validates complete
rounds, prints progress, and closes its peer in `finally`. It refuses to overwrite
evidence. The output retains all rounds and the executable SHA-256; the bootstrap
unit is each process's geometric mean, not each request. The runner saves valid
evidence before returning the assessment: 0 within margin, 1 regression, 2
inconclusive. Invalid runs raise an error without writing a result file. A zero
exit applies only to the measured scope, not universal zero overhead.
Optional client event-loop affinity is available with `--cpu N` on Windows/Linux.
The driver calls the existing framework affinity API before warmup and reports
success; the runner requires exactly one matching acknowledgement per process.
Invalid or unsupported binding aborts measurement. macOS advisory affinity is
not accepted as fixed-CPU evidence. The Python peer and other threads are not
pinned, and this does not isolate a core or verify CPU topology/frequency.
A separate historical executable can now
be supplied using `--baseline PATH_TO_RAW_ONLY_EXECUTABLE`. The runner alternates
baseline/candidate process order, keeps both executable hashes and all raw rows,
and compares historical raw throughput with candidate disabled throughput. Each
baseline process must emit eight unique raw rounds. A candidate may emit eight
raw/disabled pairs, or eight disabled-only rounds when a baseline is supplied.
Use `bench_http_disabled` for equal-workload historical comparisons. Without a
baseline, paired raw/disabled output remains mandatory. Missing, duplicate, or
mismatched samples fail.
The supplied binary's history and matching build configuration must still be
verified independently; a hash is identity evidence, not provenance evidence.

Fresh Windows Release evidence in
`http-observation-windows-processes-20260913.json` contains eight processes and
128 batch summaries. The disabled/raw throughput ratio is 1.00334 with interval
[0.99148, 1.01461], again inconclusive at zero margin. Both paths link the same
current framework; this is not a comparison with the pre-instrumentation code.
The configured build requests `CNETMOD_USE_MIMALLOC=ON`, but subsequent CMake
configuration explicitly reports mimalloc unavailable and selects the system
allocator. The option alone is not evidence of the linked allocator. The archived
process run lacks an independently recorded allocator identity.

## Raw-only historical driver

`bench_http_baseline` compiles the same measurement source with
`CNETMOD_BENCH_RAW_ONLY=1`: it imports neither the observation wrapper nor tracing
middleware and emits eight raw rounds. Current Windows Release compilation and
a loopback run validated all eight rounds. The observation target continues to
emit sixteen alternating raw/disabled batches.

The wrapper was introduced in commit `fe556ed`; its parent
`85fbba254fd2b5c69e17c53676d3e9a971bd0881` is a candidate pre-wrapper baseline,
not a proven pre-instrumentation baseline for every subsystem.
Windows historical compilation has now been performed in the detached worktree
`E:/github/cnetmod-baseline-85fbba2`; tracked framework files remain unchanged.
An external CMake measurement target links the same raw-only driver against that
revision, without the current observation layer. The current counterpart is built
in `cmake-build-otel-parity` with HTTP and ORM enabled, TLS/other protocols disabled,
MSVC 19.51.36256.0, Windows SDK 10.0.26100.0, IOCP and the system allocator.
The baseline's ORM-disabled configuration exposed a missing pugixml include, so
both successful builds enable ORM. This is not evidence for all-protocol parity.

JSON, stdexec and pugixml use the exact submodule commits recorded by the baseline.
Both builds use the identical repository JSON module shim (SHA-256
`f7387c5fb4909ef18ee801e25fb17bc2cb2e7240245a26284b0129c76db17207`).
The shared benchmark source hash for the first historical sample was
`7bea4f6bb792b7ae72615b4fc08c0a9644b83d73354568b8e5ca00a483a5312b`.
Selected CMake cache comparisons found no differences in protocol/dependency
enable flags, discovered dependency library/include entries, allocator option,
or Release C++ flags. This does not assert identical generated machine code.

`http-observation-windows-historical-20260913.json` records eight process pairs,
their binary hashes, alternating execution order and all measurements. The
candidate-disabled / historical-raw throughput ratio is 1.06662 with interval
[0.96802, 1.20008]: **inconclusive** at zero permitted regression. No CPU affinity
was applied; the common Python peer, adjacent process correlation, unequal
per-process workload (candidate also measures raw), and short samples limit this
evidence. Further controlled measurements and other protocols remain required.

### Equal-workload historical sample

The `bench_http_disabled` target defines `CNETMOD_BENCH_DISABLED_ONLY=1`, selecting
only the disabled path at compile time. Both historical raw and current disabled
programs now perform 100 raw warmup requests and eight batches of 1000 measured
requests. The paired current-build target is unchanged. Both binaries were rebuilt
from shared driver SHA-256
`6e17e470e7f792cc05871119471f010480b9ac4b21ef76541f632b8c54262c79`.

The preselected 32 process pairs in
`http-observation-windows-historical-equal-20260913.json` have ratio 1.00340 and
interval [0.97777, 1.03625], still **inconclusive** at zero margin. This sample
removes the earlier per-process request-count imbalance, not the unpinned CPU,
Python-peer, correlated-sample or limited-protocol concerns. The previous sample
is retained. No repeated-until-passing performance gate is used.

### Client-affinity historical sample

Both binaries were rebuilt from driver SHA-256
`951834cacd2f9e1f3d3a1d44a00c12c21813fa7f42882198c0248d7de1988e18`.
The 32 preselected pairs in `http-observation-windows-historical-cpu2-20260913.json`
each acknowledge event-loop binding to CPU 2 before measurement. Ratio 1.02264
and interval [0.99564, 1.05618] remain **inconclusive**, not evidence meeting the
zero-regression requirement. Sixteen Python tool tests pass; the real Windows
driver also refuses invalid text and unavailable CPU 65535 without benchmarking.
Peer isolation, physical-core topology, CPU frequency and other workloads remain
uncontrolled. Linux affinity support is implemented but not yet exercised here.

Run `python testing/bench/analyze_http_observation.py RESULTS.json` to validate
complete, unique raw/disabled pairs and calculate the geometric mean throughput
ratio with a deterministic paired percentile bootstrap interval (20,000 resamples).
The command also accepts the runner's process-report format, including historical
disabled-only comparisons. It recomputes from the saved rounds rather than trusting
the embedded assessment, validates unique process IDs and matched baseline counts,
and uses each process pair as the bootstrap unit. The runner uses this same analysis
implementation. Eighteen tool tests cover replay and malformed process evidence;
the archived 32-pair CPU-2 report recomputes to its original inconclusive interval.
The default permitted regression is zero; `--tolerance` must be supplied explicitly
to allow a nonzero margin. Exit codes are 0 for within-margin evidence, 1 for
regression, and 2 for inconclusive evidence or invalid input (diagnostic on stderr).
No nonzero tolerance has been accepted for the project goal.

The CLI now gates on `combined_assessment`: throughput and both latency
quantiles must be within their margins. The original `assessment` field is
throughput-only. Missing latency makes the combined result inconclusive; an
established p50/p99 regression fails even if throughput improves. Invalid or
partial latency observations are rejected. Latency ratios compare geometric
means of reported round quantiles at the process-pair level, not percentiles of
pooled request samples. Bootstrap intervals are per metric, not a joint 95%
confidence region. The same explicit tolerance permits a fractional latency
increase; its default remains zero. Historical raw JSON files retain their
original embedded analysis: replay them to obtain the current combined result.

The archived eight-round Windows sample has ratio 0.99373 and interval
[0.98329, 1.00289]; Arch has ratio 1.00877 and interval [0.99639, 1.02677].
Both are **inconclusive** at zero margin. The interval assumes representative,
independent paired rounds; adjacent rounds may be correlated. It is not proof
of zero CPU overhead, tail-latency equivalence, or pre-instrumentation parity.
The historical result files are not fresh measurements of the current worktree.

Tool checks: `python -m unittest discover -s testing/bench -p "test_*http_observation.py"`.
Runner tests use a real loopback listener and simulated subprocess outcomes to
verify cleanup after success, timeout, process failure, and incomplete data.
CLI tests verify assessment exit codes and refusal to overwrite evidence.
Broader controlled process runs and validated pre-instrumentation baselines for
other subsystems remain required; do not convert inconclusive results into passing
performance gates.

Build `bench_http_observation` with `CNETMOD_BUILD_BENCH=ON` and HTTP enabled,
using Release mode. This executable is intentionally not a CTest pass/fail gate.

Start `python testing/bench/http_observation_peer.py` in another terminal, then run:

```text
bench_http_observation http://127.0.0.1:19439/bench
```

Stop the peer after measurement. It binds only loopback and does not persist data.
The benchmark warms one connection with 100 requests, then alternates raw and
disabled-wrapper ordering over eight paired rounds of 1000 requests each. It
checks status and body equality on every request and exits unsuccessfully on
transport or response mismatch. Statistics include request throughput and per-round
P50/P99 latency; logging occurs outside each timed batch.

## Initial Arch WSL measurement

Raw results: `http-observation-arch-results.json`. Clang 22.1.8, Release, epoll,
system allocator, Python HTTP/1.1 peer. Medians of the eight per-round statistics:

| Path | Requests/s | P50 (microseconds) | P99 (microseconds) |
| --- | ---: | ---: | ---: |
| Raw | 15343.18 | 58.9590 | 155.6145 |
| Disabled wrapper | 15459.70 | 58.9795 | 146.2855 |

These are medians of round summaries, not pooled percentiles. This short,
sequential, unpinned loopback measurement cannot establish statistical equivalence
or a universal zero-overhead guarantee. Python server and scheduler costs can mask
client overhead. Longer repeated runs, CPU affinity, concurrent requests, Windows,
enabled sampling/export controls, TLS, streaming, and other protocols remain
necessary. The raw and decorated clients link the same current framework: this
does not compare against the framework before instrumentation was introduced.

## Initial Windows measurement

Raw results: `http-observation-windows-results.json`. MSVC 19.51.36256.0,
Release, IOCP, system allocator, the same loopback Python peer and request counts.

| Path | Requests/s | P50 (microseconds) | P99 (microseconds) |
| --- | ---: | ---: | ---: |
| Raw | 16997.76 | 53.55 | 154.50 |
| Disabled wrapper | 16988.23 | 53.30 | 141.85 |

The throughput medians differ by about 0.06%; this is not a statistical confidence
bound. All earlier limitations apply. After stopping the peer, the executable
returned exit code 1 and reported invalid measurement rather than success.
The driver catches task exceptions and stops the event loop on failure as well as
success. The measurement coroutine is not detached.
