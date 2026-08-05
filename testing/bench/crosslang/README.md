# Cross-language HTTP benchmark

This directory provides one reproducible `/hello` benchmark across cnetmod, Rust,
Go 1.26, and Java 26. Every server returns `200 text/plain` with the exact body
`Hello, World!`.

Go covers the standard `net/http` server (HTTP/1.1, h2c, HTTPS/2), `fasthttp`
(HTTP/1.1 and TLS), and `quic-go` (HTTP/3). Java 26 covers the JDK
`HttpServer` with virtual threads (HTTP/1.1 and TLS) and Jetty 12.1.11
(HTTP/1.1, h2c, HTTPS/2, with an experimental HTTP/3 path). They are deliberately reported as separate
implementations: JDK HttpServer and Jetty do not represent the same HTTP stack.

## Prerequisites

Run this on Linux, ideally a dedicated host. The runner uses `taskset` to pin
server and client CPUs, and needs `openssl`, `curl` with HTTP/3 support, `oha`,
Go 1.26+, JDK 26+, Maven, Rust, and the cnetmod benchmark binaries. Jetty HTTP/3
also requires the Linux quiche native dependency; use a writable directory via
`CNETMOD_JETTY_QUICHE_PEM_DIR` when `/root/.local/cnetmod-jetty-quiche` is not
appropriate.

Point `CNETMOD_JAVA` at the exact Java 26 executable. This prevents an installed
system JDK from silently changing the Java result. `CNETMOD_MVN` similarly selects
Maven, and `CNETMOD_OHA` selects the benchmark client.

### Arch Linux validation

The build and smoke suite was run on Arch Linux under WSL2 (Linux 6.18.33.2),
using Go 1.26.5, OpenJDK 26.0.2, Maven 3.9.16, oha 1.15.0, and curl 8.21.0 with
HTTP/3 enabled. All Go modes passed (net/http HTTP/1.1, h2c, HTTPS/2; fasthttp
HTTP/1.1 and TLS; quic-go HTTP/3). Java 26 virtual-thread HTTP/1.1/TLS and Jetty
HTTP/1.1, h2c, HTTPS/2 passed.

Jetty 12.1.11 HTTP/3 starts its quiche connector but times out before replying
to the local HTTP/3 curl client on that host. It is therefore not included in
the default measured set and has no published throughput result. Run it only as
an explicit experimental diagnosis with
`CNETMOD_BENCH_SCENARIOS=java26-jetty-h3`; the default suite remains fully
passing. This is an upstream Jetty/quiche integration path, not a Java 26 or Go
result.

```bash
cd testing/bench/crosslang
CNETMOD_JAVA=/opt/jdk-26/bin/java bash ./build_servers.sh
CNETMOD_JAVA=/opt/jdk-26/bin/java bash ./smoke.sh
CNETMOD_JAVA=/opt/jdk-26/bin/java \
  CNETMOD_BENCH_RESULT_DIR="$PWD/../results/crosslang/$(date +%F)-full" \
  bash ./run_benchmarks.sh
python3 summarize.py "$CNETMOD_BENCH_RESULT_DIR"
```

`build_servers.sh` produces only ignored build outputs. `smoke.sh` verifies that
all exposed protocol endpoints return the common response before a timed run.
`run_benchmarks.sh` writes raw oha JSON, logs, and `environment.txt`.
`summarize.py` generates `summary.csv` and `summary.md` after the benchmark.

## Fair comparison rules

Compare rows only within the same protocol table. HTTP/1.1 uses 256 connections;
h2c and HTTPS/2 use 16 connections with 16 parallel streams; HTTP/3 uses 16
connections with 16 parallel streams by default. The runner separates pinned
server and client CPU sets, uses the same `oha` invocation, warm-up, request
count, and three measured runs for each scenario. Inspect `environment.txt`
before comparing results: CPU model, kernel, compiler/JDK/Go versions, client
version, TLS implementation, and CPU affinity materially change throughput.

Do not compare results gathered on different hardware or different kernel/JDK/Go
versions as a framework ranking. Report the protocol, implementation, mean,
range, P50/P99 latency, success rate, and the attached environment together.

## Configuration

Useful variables are `CNETMOD_BENCH_SERVER_CPUS`, `CNETMOD_BENCH_CLIENT_CPUS`,
`CNETMOD_BENCH_RUNS`, `CNETMOD_BENCH_SCENARIOS`,
`CNETMOD_BENCH_RESULT_DIR`, `CNETMOD_OHA`, `CNETMOD_JAVA`, and
`CNETMOD_JETTY_QUICHE_PEM_DIR`. To run Go HTTP/3:

```bash
CNETMOD_BENCH_SCENARIOS=go-quic-go-h3 bash ./run_benchmarks.sh
```

To diagnose Jetty HTTP/3 explicitly:

```bash
CNETMOD_BENCH_SCENARIOS=java26-jetty-h3 bash ./run_benchmarks.sh
```

The default result directory is suitable for the existing WSL workflow; set
`CNETMOD_BENCH_RESULT_DIR` for every published run so results cannot overwrite
each other.
