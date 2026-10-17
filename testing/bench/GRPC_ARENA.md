# gRPC Release benchmark and profiling

Use `grpc_arena_server`, an independent server implementing
`/benchmark.BenchmarkService/GetSum` with cnetmod's protobuf codec and normal
gRPC router. The endpoint decodes both integers and computes each reply;
it does not return a prerecorded result. Plain HTTP/2 and TLS with ALPN `h2`
use the same handler. The server is portable; the measurement harness uses
Linux `/proc` and `taskset`.

## Reference and comparison contract

Reference: [HttpArena](https://github.com/MDA2AV/HttpArena), inspected at commit
`8977be2721a5aba7b320345cddfcd6f6b654e698` on 2026-10-17:

- [Unary contract](https://github.com/MDA2AV/HttpArena/blob/8977be2721a5aba7b320345cddfcd6f6b654e698/site/content/docs/test-profiles/grpc/unary/implementation.md)
- [Actual h2load arguments](https://github.com/MDA2AV/HttpArena/blob/8977be2721a5aba7b320345cddfcd6f6b654e698/scripts/lib/tools/h2load.sh)
- [Profile CPU and connection settings](https://github.com/MDA2AV/HttpArena/blob/8977be2721a5aba7b320345cddfcd6f6b654e698/scripts/lib/profiles.sh)
- [Leaderboard and documentation](https://http-arena.com/)

Match the 9-byte request `00 00000004 08011002` (SumRequest 1 + 2), 7-byte
framed response, 256 and 1024 connections, 100 concurrent streams per
connection, 5 seconds per measurement and three runs. Keep plaintext and TLS
as separate profiles. HttpArena takes the best run; retain every round and
report both best and median locally. This harness adds an explicit 2-second
warmup before each round and real p50/p95/p99 from h2load's per-request log.
The inspected HttpArena parser labels a maximum latency as p99 because its
normal h2load output does not contain p99; do not compare those as equivalent.

The reference uses dedicated 64-logical-CPU server and load-generator CPU
sets and 64 load threads. A WSL i9-14900K run with smaller, disjoint CPU sets
is a same-machine baseline/candidate comparison with the same wire workload,
not a reproduction of leaderboard numbers. Freeze CPU sets, worker count,
load threads, allocator, compiler, CPU target, LTO, kernel, compression and TLS
configuration across both binaries. The in-tree protobuf implementation is
cnetmod's own protocol engine, not Google's generated C++ runtime; any future
Arena submission must select the appropriate entry category with its maintainers.

## Build and correctness

Start from a freshly fetched `origin/develop` in a dedicated task branch.
For this task the baseline is `5c969d4ac3b772963df80ff66c93fc8165901dc1`.
Keep the harness identical when building baseline and candidate.

```bash
cmake -S . -B /tmp/cnetmod-grpc-release -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -g -fno-omit-frame-pointer' \
  -DCNETMOD_ENABLE_HTTP=ON -DCNETMOD_ENABLE_GRPC=ON \
  -DCNETMOD_ENABLE_ORM=OFF -DCNETMOD_ENABLE_YAML_CONFIGURATION=OFF \
  -DCNETMOD_BUILD_TESTS=ON -DCNETMOD_BUILD_BENCH=ON \
  -DPython3_EXECUTABLE=/opt/cnetmod-ci-venv/bin/python
cmake --build /tmp/cnetmod-grpc-release --parallel 4 --target \
  grpc_arena_server test_grpc test_grpc_proto test_http2_hpack test_http2_stream \
  grpc_interop_server grpc_interop_client
ctest --test-dir /tmp/cnetmod-grpc-release --output-on-failure \
  -R '^(test_grpc|test_grpc_proto|test_http2_hpack|test_http2_stream|test_grpc_arena_harness|test_grpc_arena_smoke)$'
```

Build only the needed protocols. `portable` CPU target, no ThinLTO and bundled
mimalloc are the defaults in the command above. Retain debug symbols and
frame pointers in both Release binaries for attributable perf call stacks.
Save each binary and its SHA256 before rebuilding the candidate.

For the commands below, install grpcio in the selected Python environment.
Obtain h2load from a recorded nghttp2 version and keep its binary unchanged.
Set `H2LOAD` to its absolute path.

```bash
taskset -c 0-3 /tmp/cnetmod-grpc-release/testing/bench/grpc_arena_server \
  18080 4 > /tmp/grpc-arena-server.log 2>&1 &
SERVER_PID=$!
/opt/cnetmod-ci-venv/bin/python testing/bench/grpc_arena_check.py 127.0.0.1:18080
```

The oracle checks gRPC trailers with an independent grpcio client, variable
inputs, omitted defaults, duplicate scalars, unknown fields, negative int32,
malformed protobuf and unknown method errors. h2load counts HTTP status only:
its error rate cannot prove that every gRPC response is OK. The harness runs
the oracle before and after each round. For protocol changes, also run the
existing grpcio interop and HTTP/2 stream/flow-control tests.
`test_grpc_arena_smoke` starts one worker, runs the same small oracle, and
reaps the server. When SSL and openssl are available it also generates a
temporary localhost certificate and tests TLS. It returns CTest skip code 77
when grpcio is unavailable; a skip is not correctness evidence.

## Measurement (requires shared performance token)

Only one task may run load tests at a time on the shared host. Wait for an
explicit token before these commands; release it as soon as runs finish.
Do not overlap compilation, other benchmarks or long tests with measurements.

```bash
/opt/cnetmod-ci-venv/bin/python testing/bench/grpc_arena_bench.py \
  --server-pid "$SERVER_PID" \
  --server-binary /tmp/cnetmod-grpc-release/testing/bench/grpc_arena_server \
  --revision "$(git rev-parse HEAD)" \
  --server-cpus 0-3 --client-cpus 4-7 --threads 4 --connections 256 \
  --h2load "$H2LOAD" --output /tmp/grpc-baseline-clear-256
```

Repeat with `--connections 1024`, retaining separate output directories.
For TLS, create a local certificate with `subjectAltName=DNS:localhost`, start
the same binary with `<port> <workers> <cert.pem> <key.pem>`, and pass
`--url https://localhost:<port> --cert <cert.pem>` to the harness. Record the
negotiated TLS version and cipher from h2load's output; keep the certificate,
ALPN, provider and load-generator TLS options constant. Do not compare TLS
numbers to cleartext numbers as a code improvement.

`results.json` records binary hash, exact source revision, server command,
kernel/CPU information, affinity and every load command. Each round retains
warmup output, h2load output and the per-request TSV. CPU percent means
100 times process CPU seconds divided by command wall time (100% is one
logical CPU); it includes the generator's measured command interval, excluding
the separate warmup and oracle calls. CPU affinity is checked for every server
thread. RPS is HTTP 2xx completions divided by the configured measurement
duration. Failed requests make the run ineligible for a speed comparison.
Per-request logging has a cost; use it consistently in baseline and candidate.

## Profile first, optimize second

With the same workload active, record a separate diagnostic run. Do not mix
profiled runs into the throughput table:

```bash
perf record -e cpu-clock -F 499 -g -p "$SERVER_PID" \
  -o /tmp/grpc-baseline-perf.data -- sleep 7
perf report -i /tmp/grpc-baseline-perf.data --stdio --no-children \
  > /tmp/grpc-baseline-perf.txt
```

The baseline environment is WSL2 kernel `6.18.33.2-microsoft-standard-WSL2`.
`perf stat -e cycles true` reports cycles unsupported; software `cpu-clock`
recording works. Record this limitation instead of fabricating cycle counts.
Attribute samples across HTTP/2 session/frame work, HPACK, protobuf,
stream creation/destruction, copies/allocations and kernel/socket work. Use
`perf stat` software events and syscall tracepoints only if supported. Make
changes only after the profile identifies a target; preserve flow-control,
HPACK state, trailers, cancellation and non-Linux paths. Rerun correctness,
then repeat all four three-round configurations on the same host. Retain an
optimization only if its gain exceeds run-to-run noise without error or tail
latency regressions. Record inconclusive or negative results honestly.

No throughput or optimization claim is established merely by adding this
harness. Performance results belong in a dated file under `results/`, with
raw evidence, exact commits, commands, conditions and known limitations.
