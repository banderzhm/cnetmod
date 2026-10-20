# Native sync state A/B (2026-10-18)

Candidate performance remains partially unresolved. This is a paired native A/B with the same server fixture and loadgen, not an official arena ranking or an agensio comparison. Linux semantic/lifecycle validation is separately recorded in the parent directory (19/19). Windows sync validation is still pending.

The runner exited 0 and summary `valid` is true. Independent analysis checked exactly 12 unique rounds (two profiles × three pairs × two variants), `done == succeeded > 0`, all failed/errored/timeout and non-2xx counts zero. Timed static rounds have 60–64 more 2xx headers than completed responses; this is retained rather than mistaken for completed requests. The baseline rounds have no header/completion difference.

Before SHA256: `298bf8277e7b1edc32f7e1ab1ce64729a52e8884e97e9486a77a56a20f4cabc4`.
After SHA256: `920f7172c770db8ec2a4665c6db2efe712a01501e58d93d7f3d0314a7d30ec3e`.
External run directory: `/var/tmp/cnetmod-h3-sync-ab-7e8557d235494f4d9a710aa6c2d33146`.

Each profile used AB / BA / AB order, fresh 32-worker servers, 32 bound sockets, a one-request readiness check and two-second settle, h2load 64 connections × 64 max streams × 64 threads for five seconds with client taskset 16–31, and the same isolated network namespace. Full commands and binary/loadgen/fixture hashes are in summary.json. Servers were native processes; the namespace anchor used the pinned arena image. These controls do not reproduce all official Docker CPU/runtime conditions.

| Profile / pair | Completed/s before → after | Change | Printed RPS before → after | Request p99 before → after | RSS KiB before → after |
|---|---:|---:|---:|---:|---:|
| baseline-h3 / 1 (AB) | 1,623,840 → 2,379,175 | +46.52% | 1,899,893 → 2,726,534 | 14.19ms → 9.94ms | 198,060 → 225,204 |
| baseline-h3 / 2 (BA) | 1,950,538 → 2,397,464 | +22.91% | 2,227,514 → 2,723,520 | 11.19ms → 7.56ms | 218,424 → 210,192 |
| baseline-h3 / 3 (AB) | 1,887,194 → 2,064,061 | +9.37% | 2,155,176 → 2,361,286 | 11.50ms → 10.93ms | 195,804 → 208,124 |
| static-h3 / 1 (AB) | 271,541 → 199,765 | -26.43% | 308,471 → 229,730 | 37.02ms → 59.46ms | 185,312 → 190,580 |
| static-h3 / 2 (BA) | 195,910 → 201,191 | +2.70% | 228,431 → 230,163 | 66.57ms → 56.54ms | 180,168 → 181,144 |
| static-h3 / 3 (AB) | 244,988 → 302,110 | +23.32% | 280,266 → 341,989 | 43.43ms → 36.99ms | 181,388 → 183,560 |

Baseline completion throughput improved in all three pairs: +46.52%, +22.91%, +9.37%; the median paired gain is +22.91%. Request p99 decreased in all three pairs. The medians of the before and after rates are 1,887,194 and 2,379,175 completed/s; their ratio is a different statistic from the median paired gain.

Static completion throughput changed −26.43%, +2.70%, +23.32%; the median paired gain is +2.70%. The before and after medians are 244,988 and 201,191 completed/s (−17.88% by ratio). This disagreement and the large per-pair variation preclude a claim of stable static improvement or stable regression. Static p99 worsened in pair 1 and improved in pairs 2 and 3. The last after result alone (302,110 completed/s / 341,989 printed RPS) is not a representative acceptance result.

Printed h2load RPS uses its five-second timed interval; completed/s divides completed responses by the printed full elapsed time (5.66–5.85 s here). Elapsed values are rounded in the original text. Both metrics are preserved. Request p99 is parsed from this pinned h2load's actual `min max median p95 p99 mean sd +/-sd` table, not inferred from mean or max.

RSS is a live post-load smaps snapshot, not a quiescent retention/leak measurement. Static after RSS is higher by 5,268 / 976 / 2,172 KiB; baseline deltas are +27,144 / −8,232 / +12,320 KiB. Work completed differs across timed rounds, so these samples do not establish a per-request memory change.

Potential factor isolation, if further measurement is needed: compare RX-only (direct synchronous STREAM dispatch, original task write bookkeeping) and write-only (original STREAM visitor/task dispatch, synchronous write bookkeeping) against a common corrected baseline and the current both-factor candidate. Keep HANDSHAKE_DONE/CID-error close and all other correctness changes common; retain public lazy send/close wrappers, empty-send semantics, owner notifications, flow reservation, cancellation and fairness. First repeat semantic tests, then serial paired A/B for both profiles. No factor candidate has been applied, built or measured in this archive.

No packet/syscall/allocation improvement is inferred from these timings. Large ELF/PCM files remain external; this archive includes only small evidence and exact runner/analyser snapshots.
