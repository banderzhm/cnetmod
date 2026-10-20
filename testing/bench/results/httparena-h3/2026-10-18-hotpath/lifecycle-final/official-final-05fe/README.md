# Official serial comparison, final 05fe candidate

Source HEAD d0d92079 plus archived source.diff and source hashes; binary SHA256 `05fe27108dd833f86493aa2d00eefe7efef9df47805be198ef3eacd09042c479`. Official checkout 8977be2721a5aba7b320345cddfcd6f6b654e698. Three frameworks, baseline-h3 and static-h3, three rounds each, serial with the same 32 workers / 64 connections / 64 streams / 64 client threads / 5 second configuration. Pinned peer images and original official validation/selection are retained in records. Isolation adjustments are archived. No profiler ran during this comparison.

All 18 raw rounds pass: done equals succeeded, failed/errored/timeout and non-2xx statuses are zero. For timed h2load runs, started and 2xx header counts can exceed completed requests; the verification does not incorrectly require started==done. The official best-valid-round selector remains unchanged.

| Framework | baseline-h3 best req/s | static-h3 best req/s |
| --- | ---: | ---: |
| cnetmod | 2,887,181 | 286,063 |
| agensio | 6,056,989 | 885,430 |
| genhttp-ioxide | 1,339,117 | 197,353 |

cnetmod remains slower than agensio (2.10x baseline, 3.10x static), though faster than genhttp in this run. This is not performance-goal completion. Its separate native 60-round memory run also remains unaccepted. Do not use the comparison's short peak memory as proof of long-run memory stability.

The first setup attempt failed before any load: WSL git could not resolve the Windows worktree's .git absolute path. The successful runner consumes metadata and diff captured with Windows git and verifies every source hash before starting. It does not invent the source revision. The external failed setup directory remains /var/tmp/cnetmod-h3-lifecycle-final05f-1792324231.
