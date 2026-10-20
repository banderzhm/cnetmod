# Published leaderboard target audit, 2026-10-20

The user clarified that acceptance means at least 10% above the published
HttpArena gRPC leader. The earlier same-host peer acceptance is a separate
measurement and does not establish this clarified target.

Upstream snapshot: `81963a7f99ef2eb0fe1b142de1f80232f88d3dab`. All 174 result files were
scanned; 18 contain unary gRPC results. Frozen raw gRPC result files and
their SHA256 hashes are retained alongside audit.json.

The website default framework composite is led by WTX. Engine entries are
ranked in a separate league and have higher published throughput. Until the
user resolves the category scope, retain both comparisons; do not declare
the clarified performance task complete.

| Profile | Local cnetmod | WTX public RPS | WTX +10% | All-category leader | Public RPS | +10% target |
|---|---:|---:|---:|---|---:|---:|
| unary-grpc-256 | 5,547,569 | 3,990,708 | 4,389,779 | vanilla-epoll | 7,273,313 | 8,000,645 |
| unary-grpc-1024 | 5,243,930 | 3,620,970 | 3,983,067 | vanilla-epoll | 7,196,679 | 7,916,347 |
| unary-grpc-tls-256 | 5,287,944 | 3,688,532 | 4,057,386 | zix | 6,962,147 | 7,658,362 |
| unary-grpc-tls-1024 | 4,896,284 | 3,625,918 | 3,988,510 | zix | 6,781,146 | 7,459,261 |

All four local numbers exceed published WTX values by 35.04%-44.82%.
None reaches 110% of the all-category published maximum. These are
cross-host numerical comparisons, not an official ranking or a same-host win.

Official hardware is Threadripper PRO 3995WX, 64 cores / 128 threads;
local measurements used i9-14900K WSL2, 32 logical CPUs. CPU generation,
budget, pinning and OS differ. No scaling conversion is applied.

No new benchmark or build was run for this audit. The exclusive performance
token remains released; the shared checkout and database edits are untouched.
A resumed build must retain the Release parallel limit of -j4.

Runtime under test remains `70975f8d1b1939cb21cc1a00115a0c276c298c0f`.
Functional fixes remain reviewable, but merge notes must not claim official
leaderboard acceptance before category scope and comparable validation resolve.

Sources:

- https://http-arena.com/llms.txt
- https://http-arena.com/frameworks/wtx/
- https://http-arena.com/docs/hardware/
- https://github.com/MDA2AV/HttpArena/tree/81963a7f99ef2eb0fe1b142de1f80232f88d3dab/site/data/results
