# Bundled allocator retention: evidence and scope

This is read-only analysis of the bundled mimalloc3.5.3 (`MI_MALLOC_VERSION`
30503 at `3rdparty/mimalloc/include/mimalloc.h:11`) and the completed frozenf971
cold12 run. `allocator-source-provenance.json` records SHA256 and verbatim
line-numbered excerpts of every source cited below. No allocator options,
production defaults, or hot paths were changed, and no inferior collection or
allocator function was called.

The user subsequently requested mimallocOFF and a separate system-allocator
performance run. Further allocator-tuning and lazy-chunk design work stopped.
These findings explain observed residency; they do not establish mimalloc
retention as a throughput bottleneck or predict system-allocator performance.

## Idle is not a collection clock

- `options.c:145,154` defaults are purge_delay1000ms and arena_purge_mult4.
  `arena.c:2266–2276` computes the product, normally4000ms.
- `arena.c:2312–2339` schedules a purge by setting expiry metadata and bitmap
  bits. It does not create a background timer or thread. Actual expiry is checked
  in `arena.c:2413–2430` only when `mi_arenas_try_purge` is invoked.
- `theap.c:124–146` connects full theap collection to arena collection. The
  direct free-path arena-collection calls at `page.c:482,592` and `arena.c:1513`
  are commented out. `page.c:1213–1241` performs admin work after1000 generic
  allocation calls, full collection after the default10000 accumulated generic
  calls, otherwise a smaller retired-page collection. The generic counter
  increment is at `page.c:1336`; not every fast malloc increments it.
- Empty page retirement is separate. `page.c:595–636` uses16 admin cycles for
  small pages and4 for medium pages, with at most3 retired pages considered per
  size bin; a sole medium page can retire rather than free immediately.
  `page.c:663–699` decrements retirement counters during collection. Wall-clock
  idle alone does not decrement them. A reused page resets its retirement state.

Thus37 seconds without traffic does not promise RSS reduction under these
defaults. This is not a request to force `mi_collect` in the event loop or alter
environment variables invisibly.

## `page_full_retain` is not an empty-page limit

Its default2 (`options.c:167`) is applied in `page.c:949–980` while searching
already **full**, non-expandable small pages. Beyond that count they move to
the full/abandoned path, making them available for other theaps. It does not
mean “retain two empty pages per bin.” For blocks larger than the small maximum
(about10KiB), the local retain count is0. Therefore this option does not target
the observed12/40/48/80KiB medium empty-page bins.

## Measured allocation geometry

Finalf971 cold12 has one heap,33 allocator threads, and32 empty pages in each
of the following bins:

| Bin bytes | Empty pages | Empty reserved block capacity bytes | Used upper bytes |
| --- | ---: | ---: | ---: |
| 12,288 | 32 | 16,515,072 | 110,592 |
| 40,960 | 32 | 15,728,640 | 0 |
| 49,152 | 32 | 15,728,640 | 245,760 |
| 81,920 | 32 | 15,728,640 | 0 |

Those four bins total63,700,992 bytes of empty block capacity. This resembles
one retained medium512KiB page per worker and bin, not live request objects.
`types.h:249–251` defines64KiB small and512KiB medium pages;
`arena.c:1200–1223` selects the medium page geometry for these block sizes.
Capacity is not a resident-byte attribution: bins can contain several object
types, and metadata, page slack, mapping residency, and purged arena slices
have separate accounting.

The earlier actual-ELF allocation audit identifies fixed per-connection owners
with matching classes: connection implementation,512 completion objects,512
available-pointer queue cells and512 command queue cells. See the sibling
`connection-fixed-allocation-298bf` diagnostic for that old ELF's exact
instruction addresses; these addresses are not claimed as f971 addresses.
The corresponding current source still allocates these eagerly in
`quic_write_queue.cpp:78–88`, capacity512 in `quic_write_queue.cppm:73`, using
64-byte-aligned queue cells. `alloc-aligned.c:19–27,76–82` explains why larger
64-byte aligned arrays can overallocate by63 and cross a size-class boundary.
The global task-frame64 alignment (`task.cppm:18–36`) is required by optimized
AVX frame stores; removing it is not a safe memory optimization.

Simply moving a large request to a slightly smaller medium bin may save live
peak bytes without removing the per-worker512KiB retained-page floor. More
allocation classes can also increase that floor. A retention claim therefore
needs both class occupancy and RSS measurements, not requested-byte arithmetic.

## Cross-thread frees: known mechanism, limited current contribution

`free.c:63–96` pushes foreign-thread frees through an atomic thread-free list.
`page.c:356–372` collects that list on the owner side before decrementing the
page's used count; the documented invariant at `types.h:444–459` explicitly
includes deferred frees in `used`. Theap-local ownership therefore affects
when blocks/pages become reusable without implying leaked owners.

In this actual final snapshot, only one48B page and one8B page had pending
thread-free lists. All four large bins above had none, and maintained abandoned
page counts were zero with a complete declared-scope traversal. That evidence
does not support blaming a large foreign-free backlog for the remaining120MB
owned-empty capacity. Abandoned pages were not individually enumerated.

Final owned used upper3,858,112 bytes, owned-empty reserved capacity120,148,800
bytes, committed213,778,432 bytes, and RSS211,750,912 bytes remain distinct
metrics. The old notification leak was established and repaired separately;
the48B/80B request-proportional growth disappeared in the real regression and
cold census. This document makes no claim of all-object zero growth, exact
live malloc bytes with MI_STATS off, or equivalence to another allocator's
private accounting.
