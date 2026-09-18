# HTTP/1 Parser SIMD Decision

## Decision

Do not enable SIMD in the production HTTP/1 parser yet. Keep the cursor-based
incremental parser as the production implementation and retain the SIMD scanner
as a benchmark candidate.

## Evidence

The benchmark was run on 2026-09-18 with Arch WSL, Clang 22, libc++, Release
mode, using `testing/bench/bench_http_parser.cpp`:

| Case | Time | Throughput |
|---|---:|---:|
| Scalar CRLF scan | 9.83 ns/op | 101,686,050 ops/s |
| AVX2 CRLF candidate | 1.75 ns/op | 571,332,065 ops/s |
| Complete production request parser | 2,738.10 ns/op | 365,217 ops/s |
| Production parser, 31-byte fragments | 3,653.39 ns/op | 273,718 ops/s |

The isolated CRLF scan improves by 5.62 times. This result does not establish
the end-to-end parser gain: a complete request invokes the scanner once per
request line, header line, and terminating empty line, while the isolated case
measures only one search. Conversely, integrating AVX2 adds short-line, tail,
fragmentation, and architecture-dependent costs that the isolated result does
not cover. The benchmark therefore proves that the scan kernel is faster, but
does not yet prove a material improvement for the production parser.

## Reconsideration gate

Production SIMD should be reconsidered only when an end-to-end parser benchmark
on representative short and long headers demonstrates a material throughput or
tail-latency improvement on every supported architecture. Any implementation
must retain the same fragmented-input cursor semantics and a portable scalar
fallback. The current numbers do not justify the additional architecture paths
or maintenance cost.
