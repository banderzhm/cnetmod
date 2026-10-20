# Current 05fe candidate: prepared 60-round cold memory diagnostic

Prepared only; do not execute before root assigns the exclusive local window.
This is a different candidate and experiment from archived 298bf single-round
results. It does not reproduce the original uninstrumented run byte-for-byte:
GDB briefly pauses all target threads only at the five stated cold boundaries.
Throughput rankings must come from the separate no-GDB run.

Frozen binary:
`/var/tmp/cnetmod-h3-arena-build/testing/bench/arena_h3_server`
SHA-256 `05fe27108dd833f86493aa2d00eefe7efef9df47805be198ef3eacd09042c479`.
The runner copies that binary and the debugger script before starting. Its
layout manifest freezes 941 current production/arena/allocator source files;
all input hashes are checked again after completion. Actual field offsets come
from this binary's DWARF, not assumed offsets. Pointer/atomic widths and walk
limits are checked. Optional unsupported container layouts report unavailable.

```sh
wsl -d archlinux -u root -- unshare --mount bash \
  /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-isolated.sh \
  python /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-pool-60.py --execute \
  --output /var/tmp/cnetmod-h3-pool-60-719ab4bdc8c34cdd950fcbed758cc1a1
```

Use a fresh nonce if that output already exists. The approved wrapper preserves
the same private gai.conf/IPv4-first localhost environment as successful native
runs; the runner validates this rather than changing the request URI. A fresh
nonce-labelled Docker network namespace isolates actual loopback. Server/client
cleanup is limited to owned PID start-times/process groups and the exact nonce
labelled container. No build, reconfigure, inferior allocation/reclamation call,
pool construction, mi_collect, or hot-path instrumentation is performed.

Workload: one process, 60 rounds of 200,000 requests, 64 connections, max64 streams,
64 client threads, 32 reuseport workers; unchanged h2load169 flags/URIs and
Accept-Encoding. Every round must have 200,000 total/started/done/succeeded/2xx
and zero failed/errored/timeout. A failed round receives a cold snapshot and
stops further traffic. After each round idle2s RSS is recorded. GDB snapshots:

1. Initial, before any client connection.
2. Round 1 +2s.
3. Round 30 +2s.
4. Round 60 +2s.
5. Round 60 +37s (35 additional seconds after the normal +2s sample).

Each snapshot retains proc status/maps/smaps_rollup/stat, exact GDB command,
duration and actual time since load completion, plus debugger JSON. Limits are
256 heaps, 2048 thread-heaps, 32768 owned pages, 5s owned-page traversal and
16384 weak timer entries per worker. A bound failure is explicit partial or
unavailable evidence, never a silent zero. GDB uses `may-call-functions off`
and performs no inferior writes.

Distinguish these measurements:

- Server maps: connections are CID entries, not distinct peers; sessions and
  published-generation maps have their own counts. Size zero with nonzero hash
  buckets means retained capacity, not live values. Bucket bytes only count the
  pointer array, not nodes.
- Timer heap: vector size/capacity/reserved bytes, schedule-map size/buckets,
  expired versus live weak control blocks. Weak expiry uses libc++ control-block
  shared-owner count, without calling lock/expired or touching an old object.
  A vector can be empty yet retain high-water capacity. Unknown weak layout
  does not discard readable vector metadata.
- Reset routes: 30s retention is intentional; pending Initial routes expire
  after10s. Expect these to differ at +2s and +37s. The maintained timer wakes
  perform reclamation without additional traffic. Linux reuseport does not
  construct the Windows shared-socket dispatcher/inbox tables; an absent table
  must not be used to explain Linux RSS.
- Allocator: reserved and committed OS-accounting counters, owned-page used
  upper bound (includes deferred thread frees), definitely-empty block capacity,
  and deferred-free page count. Empty capacity is not RSS. Heap-maintained
  abandoned page counts are sampled by bin; abandoned pages are not enumerated
  and no abandoned live-byte claim is made. Incomplete counters yield null total.
- Small/large UDP cache count and fixed capacity are distinct from allocator
  retained pages. No live-allocation inference is made from disabled MI_STATS.

Interpret deltas only after inspecting completeness flags. Rising owned used
upper together with persistent live-owner/map values suggests a live-retention
lead; rising empty capacity with stable live values suggests page retention.
Rising abandoned page counts cannot be assigned bytes from this snapshot alone.
Rising RSS/committed with all these stable needs OS/arena purge/stack/other-owner
investigation; it is not sufficient evidence for a request leak or a plateau.

Known source bounds for interpretation:
`http3_server.cpp:1311` only adds an earlier timer generation; stale weak heap
entries are popped after their own deadline at2037. The underlying vector does
not shrink. Closure erases connection/session/generation records at1462..1479.
Reset routes expire after30s, pending routes after10s and have explicit
maintenance deadlines. Windows epoch retired records retry maintenance while
nonempty; that structure is not allocated on this Linux path.
