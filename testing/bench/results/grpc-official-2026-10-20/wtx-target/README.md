# WTX published target acceptance, 2026-10-20

The user explicitly selected the default framework leaderboard leader WTX
and handed the exclusive test token back to gRPC. This report supersedes
the unresolved category scope in the preceding leaderboard audit.

**All four published RPS thresholds pass in a fresh complete matrix.**
This establishes the requested comparison with published WTX numbers,
not an official HttpArena ranking or a same-host cnetmod versus WTX result.

| Case | Same-host control RPS | Candidate RPS | Published WTX RPS | Required +10% | Gain vs published WTX |
|---|---:|---:|---:|---:|---:|
| unary-grpc / 256 | 835,303 | 5,670,176 | 3,990,708 | 4,389,779 | 42.08% |
| unary-grpc / 1024 | 584,774 | 5,102,319 | 3,620,970 | 3,983,067 | 40.91% |
| unary-grpc-tls / 256 | 871,187 | 4,890,197 | 3,688,532 | 4,057,386 | 32.58% |
| unary-grpc-tls / 1024 | 540,594 | 4,764,466 | 3,625,918 | 3,988,510 | 31.40% |

Each candidate and control case has exactly three fresh five-second rounds;
the official headline is the best valid 2xx count divided by measured wall
seconds. All 24 rounds have zero failed, errored, timed-out or non-2xx
requests, and progress beyond the initial stream batch. Linux build-activity
monitoring found no interference. The candidate matrix is kept intact;
none of the earlier, sometimes faster case results is substituted.

The control and candidate use the same local host, native/ThinLTO builds,
16 individually pinned workers on CPUs 0-15, client CPUs 16-31 with 64
threads, 256/1024 connections and 100 concurrent streams per connection.
Runs were sequential, candidate matrix then control matrix. Frozen images
and executable hashes were rechecked. No source, image or build changed.

WTX published numbers are from the default framework leader in HttpArena
snapshot `81963a7f99ef2eb0fe1b142de1f80232f88d3dab`. Engine results are
outside the user-selected acceptance scope. The upstream recipe at this
snapshot only subscribes WTX to plaintext; its site retains TLS results.
The published TLS numbers are preserved as public reference values; this
run does not claim a reconstructed same-host WTX TLS measurement.

Hardware differs: WTX official results use Threadripper PRO 3995WX
(64 cores/128 logical threads); local results use i9-14900K WSL2
(32 logical CPUs). No cross-machine scaling factor is applied.

Published sources: [WTX results](https://http-arena.com/frameworks/wtx/),
[default rankings](https://http-arena.com/llms.txt),
[official hardware](https://http-arena.com/docs/hardware/).
Frozen upstream result JSON and hashes are in [the target audit](../leaderboard-audit/).

## Code, verification and merge notes

- Tested runtime: `70975f8d1b1939cb21cc1a00115a0c276c298c0f`.
- Matched control: `036f369f387272f3689bda72468ccc454b6b3e2d`.
- Latest develop was checked again and remains
  `80e40a01c2092e12c5b851a6ef87676e854a6540`, already merged into the branch.
- Unchanged image correctness evidence: official 9/0 validation, 20 targeted
  Release suites and 20 ASan/UBSan suites; see [the original report](../README.md).
- Unchanged runtime perf evidence: matched 499Hz frame-pointer profiles,
  control 35,843 samples/candidate 20,932 samples, both zero lost;
  see [profiles](../perf/profiles.json). No new perf speedup is inferred.
- Main changes are shared synchronous unary dispatch, borrowed protobuf and
  headers, bounded stateless HPACK memoization, bounded HTTP/2 duplex writes,
  flow-control correctness, and task/cancellation slot reuse.
- Review/squash the complete `codex/grpc-performance` branch after native
  Windows CI passes. Do not merge the measurement control branch or
  cherry-pick only the final Content-Length change. No push, PR or merge
  to develop has been performed.
- No new build was necessary. All prior Release builds used at most `-j4`.

## Resource release

Fresh heavy measurements ended and the token was released at `2026-10-20T15:32:10.410262+00:00`.
The task-scoped benchmark containers were already cleaned by the driver.
The shared database checkout, index and edits were left untouched.

Raw logs, invocation settings, quiet-host monitors and the complete 24 rounds
are retained with [summary.json](summary.json) and [manifest.json](manifest.json).
