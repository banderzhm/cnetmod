# HTTP/3 HttpArena baseline, 2026-10-17

This is a historical custom-harness record, not an official HttpArena result.
The subsequent [official HTTP/3 reproduction](arena_h3_official.md) uses the
pinned official driver, both profiles, three-round errors and comparison
entries. Its conclusions supersede the earlier comparison below.

The ordinary HTTP/3 receive-buffer change improves median throughput by **5.62%**
in this local three-run experiment. It does not establish a leaderboard rank.
Median server CPU usage rises by 4.88%; this is not a demonstrated CPU-saving
change. All nine timed runs report zero failed/errored/timed-out requests and
zero non-2xx responses.

Baseline: remote `origin/develop`, fetched before branching, commit
`5c969d4ac3b772963df80ff66c93fc8165901dc1`.
Branch: `fix/http3-arena-performance`. The same new benchmark adapter is used
with the baseline and optimized library, so application work is held constant.

## Method and limits

Reference source: [HttpArena](https://github.com/MDA2AV/HttpArena/tree/8977be2721a5aba7b320345cddfcd6f6b654e698),
especially `scripts/lib/profiles.sh`, `scripts/lib/tools/h2load-h3.sh`,
`scripts/lib/common.sh` and `docker/h2load-h3.Dockerfile`.
The [published site](https://http-arena.com/) is context, not same-machine evidence.

This reproduces **baseline-h3**, not static-h3, gateway-h3 or a composite score:

| Parameter | Every local implementation |
|---|---|
| Request | GET `/baseline2?a=1&b=1`, no request body |
| Response | 200, `content-type: text/plain`, body `2` (one byte) |
| Application | Parse numeric query values and calculate their sum per request |
| Connections / streams | 64 persistent QUIC connections, 64 concurrent streams each |
| Workers | 16 server workers/reactors, 16 client threads |
| CPU affinity | Server `0-15`; client `16-31` |
| TLS / ALPN | TLS 1.3, AES-128-GCM, X25519, `h3`; same fixture certificate |
| Sampling | 2 s internal warmup + 5 s measured interval, three sequential runs |
| Between runs | 2 s outside the measured interval; same server remains running |
| Client | nghttp2 h2load v1.69.0, commit `68cb6900fde14c77f0cd7add0e094a862960eb99` |
| Latency | h2load's native reservoir-sampled percentile table; no per-request log I/O |
| CPU | `/proc/PID/stat` user + system seconds, process-wide, divided by wall time |

CPU accounting includes the explicit warmup and teardown; RPS and latency refer
to the 5-second main interval. CPU 100% means one logical CPU, not the whole
machine. Both completed-success RPS and HttpArena-style `2xx / main duration`
are retained in each summary. Header and completion counters are gated at
different times in h2load: the comparator has 188–252 more measured 2xx headers
than completed streams at the phase boundary (at most 0.016%). This discrepancy
is recorded as `status_counter_delta`, not labelled a transport failure.

Differences from the upstream harness are explicit: this 32-vCPU WSL machine
uses 16 client threads rather than 64 and a smaller server cpuset, adds the
requested warmup, and reports all runs plus the median rather than best-of-three
alone. The load generator uses ngtcp2's OpenSSL 3.6.4 `crypto_ossl` backend;
HttpArena's Dockerfile uses quictls. The shared TLS offer is pinned, while
framework-specific congestion control, QPACK, flow-control and UDP batching
remain framework defaults. They have not been proved identical across engines.
These are comparable **local workload results**, not a bit-for-bit reproduction
of the leaderboard environment.

Server: Windows host, Arch Linux WSL2 kernel
`6.18.33.2-microsoft-standard-WSL2`, Intel Core i9-14900K, 32 vCPUs, 15 GiB WSL
RAM. The guest reports adjacent CPU pairs sharing a core, so the two affinity
sets do not share guest core IDs. WSL affinity does not reserve physical host
cores. Other builds/load generators were checked before each phase. The task's
build was paused when another task started a build; no timed sample ran during
another build. Builds were restricted to `-j4`.

Toolchain: Clang 23.1.3, libc++/libc++abi 22.1.8, CMake 4.4.3, Ninja 1.13.2,
liburing 2.15, ngtcp2 1.25.0, nghttp3 1.18.0, aioquic 1.7.0. cnetmod uses its
pinned BoringSSL submodule `1a5078d343ba5d9501062caf11e3173fc364dc59`, assembly
enabled, bundled mimalloc 3.5.3, portable CPU target, io_uring, and Release
`-O3 -DNDEBUG -g -fno-omit-frame-pointer`. No native-CPU target or LTO is enabled.

## Results

RPS below is `2xx / 5 s`, following the reference's 2xx-based scoring approach
but excluding our added warmup from the denominator. All error rates are zero.

| Implementation | Run | RPS | p50 ms | p95 ms | p99 ms | CPU % incl. warmup |
|---|---:|---:|---:|---:|---:|---:|
| develop baseline | 1 | 1,336,913.8 | 2.74 | 5.64 | 7.65 | 1296.98 |
| develop baseline | 2 | 1,354,182.6 | 2.69 | 5.69 | 8.21 | 1338.18 |
| develop baseline | 3 | 1,374,383.0 | 2.67 | 5.94 | 8.12 | 1301.18 |
| optimized | 1 | 1,430,355.4 | 2.41 | 5.58 | 8.68 | 1364.96 |
| optimized | 2 | 1,399,259.8 | 2.58 | 5.29 | 7.31 | 1364.68 |
| optimized | 3 | 1,481,683.0 | 2.35 | 5.60 | 7.84 | 1316.28 |
| GenHTTP/ioxide | 1 | 319,939.8 | 10.76 | 25.15 | 38.74 | 748.31 |
| GenHTTP/ioxide | 2 | 353,564.8 | 10.34 | 20.70 | 28.05 | 683.85 |
| GenHTTP/ioxide | 3 | 334,727.8 | 10.93 | 23.46 | 32.06 | 712.76 |

Median RPS: baseline **1,354,182.6**, optimized **1,430,355.4** (+5.62%),
comparator **334,727.8** (optimized/comparator **4.27×**). Median p99 changes
from 8.12 ms to 7.84 ms; the overlapping per-run ranges do not establish a
stable tail-latency improvement. Three short runs on WSL are not a confidence
interval or a long-duration soak test.

The comparator is a prebuilt local snapshot of the leaderboard's
`genhttp-ioxide` entry, exact image ID
`sha256:72d6de94308798c44f38e98576f132ea1daf4bba07463011eadbb2f859beb8be`.
Its dependency manifest confirms GenHTTP 11.0.5 and ioxide 0.15.251, matching
the pinned reference entry's package versions. Its complete source provenance
was not independently rebuilt in this task. It runs in Docker host networking,
with the same affinity/certificate and `IOXIDE_REACTORS=16`; seccomp is disabled
for io_uring. cnetmod runs natively in the same WSL guest. Container packaging
and different TLS implementations remain comparison limits.

The independent aioquic probe validates the actual unique-key workload and
256 reused streams with 64 concurrent streams against both implementations.
The stricter cnetmod probe also checks duplicate keys, percent-decoding, invalid
integers, 404 and a fragmented >6 KiB query. The comparator returns `-2` for
`a=1&a=2&b=-3`, whereas cnetmod returns `0`; this additional-contract difference
is outside the measured unique-key workload and is not concealed as a pass.

## Profile and change

Separate profiles use `perf record -F 199 -e cpu-clock:u -g --call-graph dwarf`
against the server PID. Profiled runs are not included in the timing table.
The initial exploratory profile and the final v1.69 baseline both identify
allocation costs. Final baseline self time includes `aligned_alloc` 6.11%,
`operator new[]` 5.61%, `service_peer_stream` 3.70%, and `operator delete[]` 3.37%.
There are no lost samples in the final profiles. Relative shares are not
absolute allocation counts; optimized allocation symbols remain prominent.

Code inspection along that path found that every short request reserved 2 KiB
and then asked for another full 2 KiB to discover FIN, growing the vector even
when no more data existed. Ordinary listeners now start with 256 bytes and use
the unwritten tail for that next receive. Requests exceeding the initial block
continue with the existing 2 KiB reads. Complete-body parsing, QPACK blocking,
Content-Length checks, stream cancellation and transport flow control are
unchanged. WebTransport listeners keep their original allocation/read strategy.
No public API, ABI, platform-specific code or protocol limits change.

The benchmark adapter uses the existing request-handler Strategy boundary;
production protocol code does not depend on benchmark code. No additional
abstraction or production benchmark-only fast path was introduced.

## Reproduction

From this worktree, initialize the pinned submodules, then build once at a time:

```sh
cmake -S . -B /tmp/cnetmod-h3-arena-build -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -g -fno-omit-frame-pointer' \
  -DCNETMOD_ENABLE_HTTP=ON -DCNETMOD_ENABLE_QUIC=ON \
  -DCNETMOD_ENABLE_BORINGSSL_QUIC=ON -DCNETMOD_ENABLE_ORM=OFF \
  -DCNETMOD_ENABLE_YAML_CONFIGURATION=OFF \
  -DCNETMOD_BUILD_TESTS=ON -DCNETMOD_BUILD_BENCH=ON
cmake --build /tmp/cnetmod-h3-arena-build --target arena_h3_server \
  h3_interop_server h3_interop_client http3_e2e_server http_client_http3_e2e \
  test_quic_packet test_quic_stream test_quic_frame test_quic_loss_detection \
  test_quic_congestion test_http3_qpack test_http3_frame -j4

git clone --depth 1 --branch v1.69.0 https://github.com/nghttp2/nghttp2.git /tmp/cnetmod-h3-nghttp2-169
git -C /tmp/cnetmod-h3-nghttp2-169 submodule update --init third-party/urlparse
cmake -S /tmp/cnetmod-h3-nghttp2-169 -B /tmp/cnetmod-h3-loadgen169 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_HTTP3=ON -DENABLE_APP=ON -DENABLE_EXAMPLES=OFF
cmake --build /tmp/cnetmod-h3-loadgen169 --target h2load -j4

export CNETMOD_H3_ARENA_SERVER=/tmp/cnetmod-h3-arena-build/testing/bench/arena_h3_server
export CNETMOD_H3_LOADGEN=/tmp/cnetmod-h3-loadgen169/src/h2load
export CNETMOD_H3_PYTHON=/tmp/cnetmod-h3-peer/bin/python
export CNETMOD_H3_CERTS=/tmp/cnetmod-http-arena/certs
bash testing/bench/run_arena_h3.sh optimized /tmp/new-h3-results
```

The Python environment needs `aioquic==1.7.0`; the Linux load generator needs
the recorded OpenSSL/ngtcp2/nghttp3 dependencies. Use an existing loopback
certificate or the reference's certificate-generation script; private keys are
not committed here. For `baseline`, first build the same adapter against the
recorded develop revision in an isolated checkout and preserve that executable.
For `compare`, set `CNETMOD_H3_COMPARE_IMAGE` to the recorded exact image ID.
Each phase refuses to start if another known load generator or Ninja is running.

Every timed command, stdout, CPU accounting snapshot, raw percentile table,
server log and flat perf report is checked in under
[`results/httparena-h3/2026-10-17-wsl`](results/httparena-h3/2026-10-17-wsl).
The large binary profiles remain in `/tmp/cnetmod-h3-arena-169/` in the local
Arch WSL instance; their hashes and executable identities are in `manifest.json`.
The early v1.68 exploratory samples had full TSV logging and are excluded from
all final comparisons; they remain in `/tmp/cnetmod-h3-arena-verified/`.

## Validation and merge notes

- Eight selected CTest entries pass, including the measurement parser suite.
- Ordinary HTTP/3 client E2E and dynamic-QPACK E2E pass after the final build.
- Independent aioquic query/fragmentation/concurrency probes pass in all phases.
- aioquic-server → cnetmod-client and cnetmod-server → curl HTTP/3 pass.
- Initial acceptance reported an aioquic WebTransport HTTP Datagram timeout on
  both original develop and optimized builds. The follow-up below resolves the
  test-peer bug; both original failure JSON records are retained. The platform
  follow-up below supplies the previously skipped Rust wtransport/nghttp3 peers.
- Windows/MSVC Release verification passes in the follow-up below.
  Windows Debug and weak-network/long-duration tests were not run in this task.
- `git diff --check`, generated AGENTS consistency and version checks pass.

The production diff is confined to ordinary HTTP/3 receive-buffer sizing. It
is suitable for review as an isolated portable optimization. The corrected
peer passes the available acceptance gate, but this report does not claim
cross-platform release qualification. Nothing has been pushed to `main` or merged.

### WebTransport gate follow-up

The apparent Datagram failure was preceded by the aioquic client closing the
connection with `H3_FRAME_ERROR` 262, reporting a truncated HTTP/3 frame. The
client had created a WebTransport bidirectional child but then passed its raw
echo bytes (`strm`) to the HTTP/3 frame parser. aioquic 1.7.0's stream-creation
helper does not initialize that local child's receive classification.

The probe now routes only its own registered child stream's QUIC data to an
explicit echo check, waits for FIN, and requires the complete exact
payload before checking Datagram/close. All unrelated streams still use
aioquic H3 parsing. Six focused regression cases cover complete/fragmented
echoes, truncation, extra bytes, unrelated streams, and diagnostic errors.
No production transport/session code changes were needed for this fix; no
additional high-load measurements were run after releasing the test token.
Follow-up validation passed all 12 selected CTest entries (including the
acceptance gate and both client E2E modes). The corrected probe also passed
against the preserved original develop server and on three fresh optimized
acceptance runs. Each acceptance run has three passed cases and three explicitly
skipped optional peers. The subsequent platform follow-up supplies those peers;
weak-network coverage remains outstanding.
See [acceptance details](../http3/ACCEPTANCE.md) and the `webtransport-followup`
records in the result directory for the corrected baseline and optimized runs.

### Windows and optional-peer follow-up

On `fix/http3-arena-performance`, Visual Studio 2026 / MSVC 19.51 built the
optimized HTTP/3 server, clients, benchmark adapter and seven QUIC/QPACK test
targets in Release using one MSBuild node. All 13 selected CTest entries pass,
including ordinary and dynamic-QPACK client E2E, and both one-worker and
two-worker IOCP interoperability gates. Each Windows gate passes aioquic
HTTP/3, aioquic WebTransport and three independent native Rust wtransport
connections. Windows curl lacks HTTP/3, and native nghttp3 peers were not
provided; these cases remain explicitly skipped on Windows. Debug was not run.

The Arch WSL gate supplies all six peers and passes all six with no skips:
aioquic HTTP/3, aioquic WebTransport, Rust wtransport (three independent
connections), curl HTTP/3, nghttp3 client and nghttp3 server. The latter two
use the official ngtcp2 1.25.0 examples linked with nghttp3 1.18.0 and OpenSSL
3.6.4. The same unmodified Rust probe source was built on each platform with
wtransport 0.7.1; an isolated Cargo package avoids unrelated cross-language
benchmark dependencies, and its exact manifest and lockfile are recorded.

The acceptance harness now expands `{port}` for the nghttp3 client command
so it reaches the randomly assigned fixture port. cnetmod client health
checks also require the `ok` response body alongside status 200. Both Windows
interop gates and the complete Linux peer gate pass with these stronger checks.
No production code changed and no high-load measurements were repeated.

Commands, tool and executable identities, build logs, detailed Windows CTest
output and final peer JSON are preserved in
[`platform-peer-followup`](results/httparena-h3/2026-10-17-wsl/platform-peer-followup).
The root manifest hashes these records. This adds local Windows Release and
optional-peer evidence without claiming Debug, weak-network or soak coverage.
