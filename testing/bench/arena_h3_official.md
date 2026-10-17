# HttpArena official HTTP/3 profiles: local reproduction

Follow-up repairs and low-load regression evidence are recorded in
[static-h3 repair checks](results/httparena-h3/2026-10-17-static-fixes/README.md).
Those repairs have not yet received a benchmark token for official retesting;
the measurements below remain the original results, including all failures.

This record supersedes the performance conclusions from the earlier custom
[local harness](arena_h3.md). It runs the official `baseline-h3` and `static-h3`
profiles at HttpArena commit
[`8977be2721a5aba7b320345cddfcd6f6b654e698`](https://github.com/MDA2AV/HttpArena/tree/8977be2721a5aba7b320345cddfcd6f6b654e698).
These are local WSL results using that driver, not published leaderboard scores.

## Workload and execution

The official `scripts/benchmark.sh`, `scripts/lib/profiles.sh`,
`scripts/lib/tools/h2load-h3.sh` and `scripts/lib/stats.sh` perform generation,
timing, CPU/memory sampling, parsing and winner selection. Both profiles use
64 connections, 64 streams per connection, 64 client threads, a 5-second
measurement, three serial rounds, no added warmup, and the official 2-second
cooldown. The winner is the highest **2xx count / reported elapsed seconds**,
not h2load's differently timed headline req/s and not a median.

`baseline-h3` requests `/baseline2?a=1&b=1`; the application parses and sums
the query and returns `2`. `static-h3` uses the unchanged official
`requests/static-h2-uris.txt`: 20 actual files with
`Accept-Encoding: br;q=1, gzip;q=0.8`. Their committed bytes and precompressed
variants are used directly. Mean wire body size over the 20-file list is
16,270.25 bytes; the original vendor file is 307,200 bytes. No tiny replacement
payload, constant-query shortcut, or protocol/security check was used.

The host is an i9-14900K, 32-vCPU / approximately 15-GiB Arch WSL2 guest,
kernel `6.18.33.2-microsoft-standard-WSL2`. The official smaller-host CPU
fallback grants servers all 32 CPUs (`--cpus=32`), while clients use CPUs
16–31. Thus 64 generator threads oversubscribe 16 vCPUs; this differs from
the leaderboard's dedicated machine. Every implementation uses the same
local CPU policy and client argv. Only one benchmark runs at a time, with
no builds during the timed group; builds used at most `-j4` or MSBuild `/m:1`.

The native client is the official-supported nghttp2/h2load 1.69.0 binary,
linked with OpenSSL 3.6.4, ngtcp2 1.25.0 and nghttp3 1.18.0. All measured
logs negotiate TLS 1.3, `TLS_AES_128_GCM_SHA256`, X25519 and ALPN `h3`.
The cnetmod builds use Clang 23.1.3, libc++ 22.1.8 and
`-O3 -DNDEBUG -g -fno-omit-frame-pointer`. agensio uses its official Dockerfile,
GCC 14.2, Release / native CPU flags, tag `v0.1.0-alpha.23`, source
`e60c61c7643e47c046fbb5136f8b46f7f7f9289b`.
GenHTTP 11.0.5 / ioxide 0.15.251 uses the previously built official entry image.
Exact image IDs, compiler commands, environment and binary hashes are in
[`environment.json`](results/httparena-h3/2026-10-17-official-h3/environment.json)
and the manifest. Framework build flags remain each entry's recorded defaults;
they are not claimed to be identical across languages.

The wrapper preserves an audit patch for environment-only adjustments:

- Official `SKIP_TUNE=true` avoids changing the shared host's governor,
  sysctls, loopback MTU or Docker daemon. Global Docker prune/stop sweeps are
  omitted; this run still stops/removes its own containers.
- An isolated mount namespace prefers IPv4 for `localhost`. URI, SNI,
  payload, measured port 8443 and load arguments are unchanged for every entry.
- Readiness probes use actual HTTP/3 for UDP-only servers. The official
  correctness validator uses isolated port mappings 18080/48443/18081/18082,
  and its readiness grep drains stdout under `pipefail`; measured profile
  parameters are unchanged. The earlier readiness failure is retained.
- A staging copy limits entry subscriptions to the two H3 profiles, avoiding
  unrelated database sidecars. Exact prebuilt images are tagged by build
  scripts so no compilation happens during a measurement.
- agensio's Dockerfile adds only `CMAKE_BUILD_PARALLEL_LEVEL=4`; its build uses
  host networking for the machine's configured proxy.

## Results

Official RPS for every round:

| Entry / profile | Round 1 | Round 2 | Round 3 | Official best | Failures across rounds |
|---|---:|---:|---:|---:|---:|
| Original cnetmod / baseline-h3 | 1,418,432 | 1,437,490 | 1,452,292 | 1,452,292 | 0 |
| Current cnetmod / baseline-h3 | 1,568,028 | 1,543,190 | 1,524,744 | 1,568,028 | 0 |
| Current cnetmod / static-h3 | 12,400 | 14,785 | 6,967 | 14,785 | 15 |
| agensio / baseline-h3 | 3,740,676 | 3,808,081 | 3,658,728 | 3,808,081 | 8 |
| agensio / static-h3 | 61,292 | 65,380 | 66,954 | 66,954 | 0 |
| GenHTTP/ioxide / baseline-h3 | 300,602 | 346,621 | 344,765 | 346,621 | 55 |
| GenHTTP/ioxide / static-h3 | 48,772 | 51,451 | 53,186 | 53,186 | 2 |

Latency and complete-response throughput from the official winning round:

| Entry / profile | P50 | P95 | P99 | Completed responses / elapsed |
|---|---:|---:|---:|---:|
| Original cnetmod / baseline-h3 | 1.90 ms | 5.52 ms | 9.21 ms | 1,452,292/s |
| Current cnetmod / baseline-h3 | 1.89 ms | 4.67 ms | 6.98 ms | 1,568,028/s |
| Current cnetmod / static-h3 | 60.36 ms | 178.23 ms | 345.77 ms | 14,657/s |
| agensio / baseline-h3 | 751 us | 1.65 ms | 2.41 ms | 3,808,081/s |
| agensio / static-h3 | 30.05 ms | 184.75 ms | 294.80 ms | 66,365/s |
| GenHTTP/ioxide / baseline-h3 | 9.74 ms | 20.22 ms | 27.32 ms | 346,565/s |
| GenHTTP/ioxide / static-h3 | 62.67 ms | 132.54 ms | 193.07 ms | 53,175/s |

In this local group, the current branch's `baseline-h3` official best is
**7.97% above** the preserved develop baseline
`5c969d4ac3b772963df80ff66c93fc8165901dc1`. Both complete all three rounds
without reported failures, errors or timeouts. This compares the combined
branch changes, including the correctness fixes below; it does not attribute
the entire difference to receive-buffer sizing alone. agensio is substantially
faster than cnetmod in this comparison, so no fastest-framework claim is made.

The cnetmod static result is **not zero-error stable**: 15 reported failures
out of 195,611 total requests (0.007668%), and substantial round-to-round RPS
variation. Its static throughput also trails both comparison entries. agensio
baseline, GenHTTP baseline and GenHTTP static have 8, 55 and 2 failures
respectively; these remain visible rather than being discarded. Every round
reports zero `errored`, zero `timeout`, and zero 3xx/4xx/5xx responses.
The failed-request causes were not isolated by this measurement group.
All 21 timed rounds are retained; no low-scoring or failing round was rerun
or discarded after inspecting its result.

For long responses, a 2xx header can arrive before its body completes. The
official metric counts those headers; `summary.json` additionally retains
complete-response throughput, the header/completion delta, failed fractions,
and started-but-unfinished requests at the duration boundary. The table does
not substitute this supplemental metric for the official metric. Passing
low-load correctness checks does not establish zero-error high-load stability.
No weak-network, long-duration soak, memory-saving or CPU-cost claim follows
from these three short runs. No new perf sampling was necessary for the
correctness issues identified before timing.

Memory also grows over the repeated baseline rounds: the original server's
sampled peaks are 1.8 / 2.8 / 3.9 GiB, and the current server's are
1.9 / 3.1 / 4.2 GiB. A lower memory value in the winning current round does
not demonstrate a saving; the winners occur at different round positions.
This growth requires follow-up before a long-running stability claim.

## Correctness changes and verification

The full static fixture exposed issues that the short query response missed:

1. A packet builder appended a whole STREAM frame after checking only the
   current payload length. It now requeues a frame that cannot fit, preserving
   FIFO/priority metadata instead of generating an oversized UDP datagram.
2. A peer-opened bidirectional stream retained a 256-KiB fallback send cap.
   It now starts with the peer's negotiated `bidi_local` credit, including
   zero. Large buffered writes send bounded fragments and asynchronously
   wait for actual stream/connection credit; limits are not enlarged.
3. Dynamic QPACK retained the encoder mutex while a large body waited on flow
   control, blocking peer decoder feedback. Encoder instructions still precede
   referring HEADERS, but the body is sent after releasing encoder state.

Regression coverage includes zero initial send credit, an exact 300-KiB
send-credit boundary, and a 300-KiB buffered response in both ordinary and
dynamic-QPACK client E2E modes. The file adapter reads the actual files through
the existing asynchronous file API; only fixture paths/MIME metadata are
indexed at startup. It does not freeze response bodies in a startup cache.
It explicitly supports the official Accept-Encoding profile and identity
byte probes; it is a benchmark fixture, not a general static-file API.

All five entry variants pass the official H3 correctness checks (four real
QUIC requests per subscribed endpoint). Independent curl HTTP/3 byte checks
pass query calculation, all 20 identity bodies and all 20 negotiated bodies,
plus a missing-file 404, for current cnetmod and both comparators. The original
baseline has only baseline-h3 coverage. Final Linux interoperability passes
all six peers without skips, including native Rust wtransport and both
nghttp3 directions. Final Windows MSVC 19.51 Release passes all 13 selected
CTest entries, including single/two-worker gates and the large-response E2Es.
Windows curl HTTP/3 and native nghttp3 remain unavailable there; Debug was
not run. Source-generation, version and whitespace checks also pass.

## Reproduction and records

[`run_official_h3.py`](run_official_h3.py) stages the pinned official scripts
and invokes their driver; it does not implement a replacement load generator.
[`probe_official_h3.py`](probe_official_h3.py) performs supplemental byte checks;
[`summarize_official_h3.py`](summarize_official_h3.py) checks that its three
rounds agree with the official saved winner. They require the test token
before running any benchmark; keep other benchmarks and builds stopped.

The exact local preparation script, isolated address-policy wrapper, image
IDs, invocation environment, source/data hashes, audit patch, raw outputs,
official selected JSON, all 21 round records, failure diagnostics and final
Windows/Linux checks are under
[`2026-10-17-official-h3`](results/httparena-h3/2026-10-17-official-h3).
The baseline executable is preserved locally; its SDK revision, original
adapter revision and hash are recorded. Source/binary hashes identify the
timed build independently of subsequent documentation commits.
The test token was released immediately after the final timed group;
subsequent work only organized records and ran low-load interoperability.
