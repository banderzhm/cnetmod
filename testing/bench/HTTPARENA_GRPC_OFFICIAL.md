# Official HttpArena gRPC / HTTP2 preparation

Status: **preparation only; no official benchmark, validation container or load
was started for this phase. No performance token is currently held.**
The previous custom-harness report is exploratory profiling evidence and is
ineligible for the new official-suite acceptance requirement. Its merge
recommendation is superseded until the official comparison is completed.

## Locked upstream and exact workload

Repository: [MDA2AV/HttpArena](https://github.com/MDA2AV/HttpArena).
Official main was checked with `git ls-remote` on 2026-10-17 and matches the
local clean checkout at **`8977be2721a5aba7b320345cddfcd6f6b654e698`**.
The original reference is `build-grpc-reference/` (ignored build material).
For executable Bash source inspection, a clean native Linux checkout at the
same commit is prepared in WSL at `/var/tmp/cnetmod-grpc-official`; this avoids
Windows checkout line-ending transformations. The manifest uses that checkout.

The source of truth is the pinned executable configuration, particularly
[`profiles.sh`](https://github.com/MDA2AV/HttpArena/blob/8977be2721a5aba7b320345cddfcd6f6b654e698/scripts/lib/profiles.sh),
[`common.sh`](https://github.com/MDA2AV/HttpArena/blob/8977be2721a5aba7b320345cddfcd6f6b654e698/scripts/lib/common.sh),
[`h2load.sh`](https://github.com/MDA2AV/HttpArena/blob/8977be2721a5aba7b320345cddfcd6f6b654e698/scripts/lib/tools/h2load.sh),
[`benchmark.sh`](https://github.com/MDA2AV/HttpArena/blob/8977be2721a5aba7b320345cddfcd6f6b654e698/scripts/benchmark.sh)
and [`validate.sh`](https://github.com/MDA2AV/HttpArena/blob/8977be2721a5aba7b320345cddfcd6f6b654e698/scripts/validate.sh).
Some illustrative documentation uses 128 client threads; the pinned runtime
default `H2THREADS` is **64**. Do not substitute those examples for the driver.

| Official profile | Connections | Streams/connection | Endpoint / input |
|---|---|---:|---|
| unary-grpc | 256, 1024 | 100 | h2c `:8080/benchmark.BenchmarkService/GetSum`; official `requests/grpc-sum.bin` |
| unary-grpc-tls | 256, 1024 | 100 | TLS h2 `:8443`, same official RPC and file |
| baseline-h2 | 256, 1024 | 100 | TLS h2 `:8443/baseline2?a=1&b=1` |
| static-h2 | 256, 1024 | 32 | official `requests/static-h2-uris.txt`, 20 files, `Accept-Encoding: br;q=1, gzip;q=0.8` |
| baseline-h2c | 256, 1024, **4096** | 100 | prior-knowledge h2c `:8082/baseline2?a=1&b=1` |
| json-h2c | 1024, **4096** | 32 | official seven-pair URI rotation and mounted `data/dataset.json` |

This is **13 configurations, 39 measured rounds per implementation** for an
implementation subscribing to all six profiles. Preserve the high-load 4096
connection cases (409,600 potential streams for baseline-h2c); do not replace
them with a 32/256-connection smoke run. Unsupported profiles must remain
explicitly unsupported, never served by a simpler substitute endpoint.

The gRPC request is the official 9-byte framed SumRequest(1,2), expected framed
reply SumReply(3). JSON rotates `(count,m)` through `(1,3), (5,7), (10,2),
`(15,5), (25,4), (40,8), (50,6)` and computes each item's derived total from
the live official dataset. Static bodies come from the mounted official
directory and its documented precompressed variants; a constant response or
entry-assembled file cache is not an equivalent test.

All profiles use the official h2load client, three 5-second rounds and a
2-second cooldown. There is **no explicit h2load warmup option or separate
warmup load in this driver**; container readiness and validation are separate
steps. Preserve that behavior for official headline results. Extra warmup or
extended stability experiments must be separately labelled, not substituted.
The client adapter has a hard 45-second timeout, so blindly setting a 60-second
duration would produce a killed client, not a stability measurement.
For the requested additional stability evidence, prepare a separately labelled
`DURATION=30s RUNS=3` high-connection pass with the same official driver/client
for every supported peer after the default matrix. This supported duration
override fits the 45-second client limit; it does not replace the official
5-second headline results or establish long-duration endurance.

TLS uses upstream `/certs/server.crt` and `/certs/server.key`, mounted by the
driver. The upstream generation script specifies RSA-2048, localhost SANs,
serverAuth, and a 3650-day self-signed certificate. Use the same mounted pair
for all entries and official TLS posture/quality probes; record the negotiated
version/cipher/ALPN rather than forcing a cheaper certificate or a different
cipher. QUIC is not part of these six TCP HTTP/2 profiles; record QUIC as N/A.

## Statistics that must stay official

The official headline is the **best of three** integer RPS values, using HTTP
2xx count divided by the duration parsed from h2load's `finished in` line.
It is not the custom harness's fixed-5-second denominator or median headline.
Keep all three stdout records, HTTP status/error/timeout counters and exact
argv so success rate can be independently checked. A nonzero error rate cannot
be hidden by a successful HTTP-only readiness probe; run the complete official
randomized gRPC sum, frame, trailer and TLS checks.

The upstream field named `p99_latency` is populated from **maximum request
latency**, not an actual 99th percentile. Preserve this official field for
score parity and explicitly annotate its meaning. True p50/p95/p99 require a
separate diagnostic run with request logging or an upstream client extension,
using the same extension for every entry. Such a diagnostic changes logging
cost and must not replace the official throughput run. Never relabel max as a
measured p99 or silently compare it with the old custom harness's real p99.

Official CPU/memory come from Docker stats: mean sampled CPU percentage and
peak sampled memory. The driver also brackets each load with cgroup v2 CPU
usage counters. For gRPC it does not persist every such delta in its saved
headline JSON; retain the full official stdout and, if exact per-round CPU
time is required, capture separate cgroup metadata without replacing the
official sampled fields. Record Docker's actual memory limit (the
upstream framework launcher does not set one), host RAM, container IDs/image
digests and the sampling coverage. `/proc` process CPU alone is not the
official container measurement.

## Serial competitor queue

Selection is computed from the pinned `site/data/results/*.json` at each
profile's highest connection count and the matching enabled, subscribed
`frameworks/<entry>/meta.json`. These published numbers select peers only;
they are not local same-machine comparisons.

| Profile family | Leading standard flagship/emerging entries to rerun |
|---|---|
| unary-grpc, unary-grpc-tls | libuvcpp, helidon-production, trillium |
| baseline-h2, baseline-h2c | genhttp-ioxide, bun, libuvcpp |
| json-h2c | bun, genhttp-ioxide, libuvcpp |
| static-h2 | bun, genhttp-ioxide, genhttp-kestrel |

The generated manifest separately lists leading engine entries, including
cardigan and vanilla-epoll where subscribed. grpc-go is a tuned reference, not
a standard-tier peer. Keep standard, tuned, experimental, engine and
infrastructure comparisons distinct. Unresolved renamed result/entry aliases
are not guessed into the queue. Iterate baseline cnetmod, candidate cnetmod,
then peers **one entry and profile at a time**, after validation, on exactly
the same host, CPU sets, datasets and official client image.

Use official entry Dockerfiles and dependency pins, preserving their runtime
and code-generation settings. Record each real compiler/JDK/Rust/Go version,
Release flags, upstream library commit, image digest and binary hash. Upstream
base images and the h2load Ubuntu apt recipe are not fully immutable merely
because the Git SHA is fixed: freeze built image digests and package versions.
Several recipes use `$(nproc)` or uncapped `cargo build`; constrain **build
parallelism only** to at most four jobs and record that build-recipe overlay.
Do not alter runtime parameters, crypto validation or profile subscriptions to
obtain a throughput gain. Prebuild images serially before timed measurement.

## Current execution gates

The official reference is a 128-logical-CPU Threadripper PRO 3995WX. Runtime
profiles assign server CPUs **`0-31,64-95`** and generator CPUs
**`32-63,96-127`**. Current local WSL/Docker expose only **0–31**, with Docker
reporting 16,598,654,976 bytes of memory. The literal official disjoint CPU
sets cannot run here. Upstream `framework_start` falls back to all CPUs when
the profile set is unavailable, and the small-host client default becomes
16–31, overlapping the server. That fallback is **ineligible under the new
strict acceptance requirement**; the preparation checker rejects it. Do not
silently remap to the previous 0–3/4–7 setup and call the result official.

The official top-level driver also stops `httparena-*` containers, globally
prunes unused Docker objects, changes host socket/governor/MTU settings,
restarts Docker and drops caches. A performance token does not make shared
unrelated containers disposable. Use a dedicated benchmark host/Docker daemon
for that unmodified lifecycle. `SKIP_TUNE=true` is not equivalent to the
default tuned official run, and does not disable its global cleanup.

The current cnetmod custom server is not yet an installed official framework
entry: its CLI chooses one port, its TLS run used a separately generated
certificate, and it does not implement the official static/JSON/baseline2
profiles. Before any formal run, prepare versioned baseline/candidate container
entries on the required ports, mount official inputs, and pass upstream
`validate.sh` for their actual subscriptions. Do not treat the existing images
named `httparena-cnetmod*` as verified artifacts without provenance and digest
checks. No image build, container validation or benchmark was performed here.

## Prepared artifacts and next execution

`tools/prepare_httparena_grpc.py` only inspects source, calls the actual
official **argv-building** functions, hashes inputs and reads Docker info.
It never invokes a client, benchmark, validator, image build, cleanup or tune.

```bash
python3 tools/prepare_httparena_grpc.py \
  --arena-root /var/tmp/cnetmod-grpc-official \
  --output testing/bench/results/httparena-grpc-official-preparation-2026-10-17.json
# Same read-only preparation, but exit 2 when the official CPU sets cannot fit:
python3 tools/prepare_httparena_grpc.py \
  --arena-root /var/tmp/cnetmod-grpc-official \
  --output /tmp/httparena-grpc-official-preflight.json --require-official-host
```

The committed JSON records all 13 generated argv vectors, file hashes for
official scripts, payload, URI lists, data/static variants, public certificate,
profile documentation and selected competitor source/Dockerfiles. It also
records this host's failed CPU gate and the missing prerequisites. No image
digest or compiler result is invented before a real build.

After the explicit token and prerequisites are available, use the upstream
`./scripts/validate.sh <entry>` and `./scripts/benchmark.sh <entry> <profile>`
in the dedicated environment, preserving default `DURATION=5s RUNS=3
H2THREADS=64`. Capture full driver stdout for all three rounds and the official
saved result separately. Benchmark entry selection is positional in this
pinned script; do not copy the GitHub comment bot's `-f` syntax into the shell
driver. Do not start these commands during preparation. Release the token
immediately after the eventual serial measurements and diagnostics finish.
