# Reproducible WebSocket measurements

Use an exclusive benchmark slot on the host. Do not run other protocol
benchmarks, full builds or stress tests concurrently. Builds use at most `-j4`.

## Reference and comparison contract

The reference is [HttpArena](https://github.com/MDA2AV/HttpArena/tree/8977be2721a5aba7b320345cddfcd6f6b654e698)
and its [leaderboard](https://http-arena.com/). At that revision,
`scripts/lib/profiles.sh` and `scripts/lib/tools/gcannon.sh` define:

| Local profile | Arena profile | Connections | Pipeline | Messages per connection |
| --- | --- | --- | --- | --- |
| echo | echo-ws | 512, 4096, 16384 | 1 | unlimited |
| pipeline | echo-ws-pipeline | 512, 4096, 16384 | 16 | unlimited |
| limited | echo-ws-limited | 512, 4096 | 1 | 10 |

Use `/ws`, exact text echo with the original opcode, masked client frames,
unmasked server frames, no extensions/compression, no TLS, and a 5-byte text
payload. The reference generator's default payload is `hello`; the local
runner explicitly uses `aaaaa`, with the same length and framing. There is no
application formatting or per-message logging. Only compare baseline and
candidate measured on the same host with the same generator, CPU sets, worker
counts and flags. This scaled local CPU allocation is **not** a reproduction
of Arena's 64-core dedicated hardware or a leaderboard ranking.

The runner alternates baseline/candidate order between rounds. It restarts
the server for each sample, checks startup, runs an explicit two-second warmup
and then a five-second sample, with a two-second pause after each sample.
gcannon also includes its own 100 ms connection warmup in every invocation.
Three rounds are retained; report every round, median and range rather than
selecting only the best result. Keep the core source unchanged when building
the baseline with the same benchmark harness.

## Release build

Initialize the repository's pinned submodules first. Example Linux configuration:

```bash
cmake -S . -B build-ws -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -g1 -fno-omit-frame-pointer' \
  -DCNETMOD_CPU_TARGET=portable \
  -DCNETMOD_ENABLE_HTTP=ON -DCNETMOD_ENABLE_WEBSOCKET=ON \
  -DCNETMOD_ENABLE_ORM=OFF -DCNETMOD_ENABLE_YAML_CONFIGURATION=OFF \
  -DCNETMOD_BUILD_TESTS=ON -DCNETMOD_BUILD_BENCH=ON
cmake --build build-ws --target bench_ws_server bench_ws_codec test_websocket_frame -j2
ctest --test-dir build-ws -R '^test_websocket_frame$' --output-on-failure
python -m unittest discover -s testing/bench -p test_run_ws_arena.py
python testing/bench/check_ws_echo.py build-ws/testing/bench/bench_ws_server
```

Keep allocator, TLS provider, compiler, standard library, CPU target and LTO
settings identical. Save the baseline executables before changing the library.
`bench_ws_server PORT WORKERS [CERT KEY]` is a standalone server using the
public WebSocket API and minimal `cnetmod_websocket` component. Its optional
certificate arguments allow WSS correctness checks. The Arena runner uses
plain WS because gcannon does not implement WSS; do not label those results
as TLS results. A separate WSS load generator is required to measure WSS.
Run the low-volume wire checks over WSS with
`check_ws_echo.py SERVER --cert CERT --key KEY`. They cover text/binary frames
through 65536 bytes, empty frames, fragmented messages with interleaved ping,
16 coalesced messages, opcode/order integrity, the upgrade accept key and close.

Build [gcannon at 11c802b](https://github.com/MDA2AV/gcannon/tree/11c802b2bdfe9af2fac4ac2571ae2de4c530cc62)
using `make -j2` and keep its binary SHA-256. This revision prebuilds a frame
in a 256-byte stack buffer: the runner limits payloads to 1..248 bytes. Do not
pass kilobyte payloads to this version of `--ws-msg`. `bench_ws_codec` covers
16, 125, 126, 4096, 65535 and 65536 bytes without that generator limitation.

## Three-round comparison

```bash
python testing/bench/run_ws_arena.py \
  --baseline /absolute/path/baseline/bench_ws_server \
  --candidate /absolute/path/candidate/bench_ws_server \
  --baseline-revision BASELINE_SHA --candidate-revision CANDIDATE_SHA \
  --gcannon /absolute/path/gcannon --output /absolute/path/new-result-directory \
  --server-cpus 0-3 --client-cpus 8-11 --workers 4 --threads 4
```

CPU sets must be disjoint and available. Record SMT sibling placement and the
virtualization environment; guest CPU affinity cannot control host scheduling.
The output directory must be new. The runner preserves environment, exact
commands, executable hashes, every raw JSON result, warmups, server logs and
derived samples. It fails on process failures, absent responses, absent latency
samples, changed generator settings, connection/read/timeout/upgrade errors.
Completed samples remain on disk if a later sample fails.

Throughput is received frames divided by the generator's measured duration.
Latency comes from the generator histogram in microseconds: at pipeline 16 it
is a batch/closed-loop latency, not an independent per-message RTT. Error event
rate is `(connect + read + timeout + upgrade failures) / (responses + errors)`;
these are transport events, not proof of echoed payload integrity. Validate
payloads and opcodes separately with Arena's `scripts/validate-ws.py` and the
regression tests. The sent-minus-received counter delta when the timer expires
is recorded separately and is not automatically counted as failed echoes.
It can include frames crossing the generator's asynchronous warmup reset.

Server CPU seconds are the process-wide `/proc/PID/stat` user+system delta
around the entire measured generator invocation (including its 100 ms
internal warmup and shutdown). CPU percent uses that same wall interval and
100% means one fully used core. It is not directly interchangeable with
Arena's cgroup CPU metric. Logger and worker threads are included; generator
CPU is excluded. The guest clock tick resolution is recorded.

## Profiling and codec checks

Run profiling separately from timing comparisons, with the same workload:

```bash
python testing/bench/run_ws_arena.py ... --perf --profiles pipeline \
  --connections 512 --rounds 1 --output /absolute/path/new-profile-directory
perf report -i SAMPLE.perf.data --stdio --no-children --percent-limit 0.5
taskset -c 0 build-ws/testing/bench/bench_ws_codec 200000
```

The profiler attaches to the server only and uses `cpu-clock` at 997 Hz with
frame-pointer call stacks. The Release build retains frame pointers. Report
parsing, frame encoding, mask XOR, buffer allocation/copies, io_uring/syscalls
and coroutine scheduling separately; time in the kernel is not evidence that
the codec is the bottleneck. Some WSL kernels expose software events but no
usable hardware PMU. Do not fabricate instructions, cycles or cache-miss data
when the PMU is unavailable. Keep raw perf data locally and commit the text
report plus its collection command.

Codec tests compare every byte against a byte-wise XOR oracle for four keys,
16 alignments, all lengths 0..257, and sentinel bytes on both sides. Frame
tests grow, shrink and reuse output storage across the RFC extended-length
boundaries; they verify mask/FIN/reserved bits, byte-for-byte payloads, partial
headers and control-frame size/fragment restrictions. All library changes
stay in implementation units, leaving the public module interface unchanged.
