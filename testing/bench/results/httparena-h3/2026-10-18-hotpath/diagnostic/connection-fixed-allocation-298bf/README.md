# Fixed connection allocations behind large retained bins

Read-only analysis of the already frozen native binary SHA-256
`298bf8277e7b1edc32f7e1ab1ce64729a52e8884e97e9486a77a56a20f4cabc4`.
No application, workload, compiler, attach, or inferior function was run for
this audit. `llvm-objdump` read the existing ELF. Offline GDB loaded its DWARF
with inferior PID zero and `may-call-functions off`.

## Actual allocation sites

All four main cold size classes have concrete fixed-per-connection allocation
sites. They are not identified as per-packet UDP arrays or coroutine frames.

| Owner | ELF allocation instruction | Requested bytes | Alignment | Matching allocator block class |
|---|---|---:|---:|---:|
| `quic_write_queue::slots_` | `0x393d7d mov $0xb008,%edi; 0x393d82 call mi_new` | 45,064 | ordinary | 49,152 |
| `quic_write_queue::available_` cells | `0x393dd9 mov $0x8000,%edi; 0x393dde mov $0x40,%esi; 0x393de3 call aligned new[]` | 32,768 | 64 | 40,960 |
| `quic_write_queue::commands_` cells | `0x393f1c mov $0x10000,%edi; 0x393f21 mov $0x40,%esi; 0x393f26 call aligned new[]` | 65,536 | 64 | 81,920 |
| `quic_connection_impl` | `0x3404eb mov $0x2cc0,%edi; 0x3404f0 mov $0x40,%esi; 0x3404f5 call aligned new` | 11,456 | 64 | 12,288 |

Verbatim instruction context and full symbol identities are retained in
`allocation-instructions.json`. Offline DWARF gives completion size 88 bytes,
command size 72 bytes, and queue object size 448 bytes. The nested connection
implementation type was unavailable by the names attempted in this offline
audit; its allocation size comes directly from the machine instructions.

`src/protocol/quic/quic_write_queue.cppm:73` fixes capacity at 512. Constructor
`quic_write_queue.cpp:78` eagerly allocates all three stores and enqueues all
512 completion pointers. Hence slots consume `512*88 + 8` bytes (array cookie).
The generic bounded queue's `alignas(64) cell` contains an 8-byte sequence plus
payload: pointer cells round to 64 bytes, 72-byte command cells to 128 bytes.
Its constructor uses `make_unique<cell[]>(capacity_)` at
`src/utils/concurrentContainers/bounded_mpmc_queue.inl:10`.

The alignment effect is supported by allocator source, not inferred from RSS.
`3rdparty/mimalloc/src/alloc-aligned.c:19` accepts natural alignment only for
power-of-two blocks at most 4KiB, or explicitly 4KiB alignment. These 32/64KiB
requests with alignment 64 miss that guarantee and take the `size+alignment-1`
fallback at line 80. Thus 32,831 and 65,599 round to 40,960 and 81,920. The size
class table is in `3rdparty/mimalloc/src/init.c:78`. A 45,064 ordinary request
rounds to 49,152; 11,456+63 remains within 12,288.

The three stores request 143,368 bytes per connection and occupy matching size
classes totaling 172,032 bytes. Including the implementation's class gives
184,320 bytes per connection, before stream/session/TLS/packet storage. These
are eager allocations per connection, not per request or per UDP batch. Their
destructors free the stores when the owning connection implementation dies.

The existing 37-second cold snapshot has empty block capacity in the four bins
of 14,966,784 / 14,254,080 / 14,254,080 / 14,254,080 bytes respectively for
12,288 / 40,960 / 49,152 / 81,920. Together that is 57,729,024 bytes of empty
owned-page block capacity, out of 98,985,744. This is capacity, not resident or
live bytes. Bins can also contain other allocation types; disassembly identifies
exact matching allocation sites but does not give exclusive per-page provenance.
Zero active connections/live inbox counters and the small owned-used upper bound
remain evidence against treating these empty pages as live connection leaks.

## Coroutine boundary

The same static scan found a 16,640-byte child coroutine allocation at
`service_peer_stream(.resume):0x327819..0x327823`. Its next instruction at
`0x327828` installs `consume_webtransport_close_capsules(.resume)` (0x31d870),
identifying the child as the WebTransport close-capsule reader, whose source
`http3_session.cpp:900` has a 16KiB read buffer. This is not the ordinary static
GET path and does not match the 40/80KiB bins. Linux batch syscall scratch is
already inside synchronous helpers in
`io_uring_async_op.cpp:1691` and `:1719`, outside the suspending coroutine frame.
This scan is an allocation-instruction audit, not exhaustive dynamic allocation
profiling: inlining, register-derived sizes and other allocation entry points
limit absence claims. No claim is made that every task is smaller than 16KiB.

## Later design options preserving 512 logical slots

No capacity, allocator setting or production source is changed in this candidate.
Reducing capacity to 64 would change saturation/backpressure semantics and is
not the proposed way to improve the memory result.

1. **Per-connection lazy stable chunks.** Keep a logical 512-slot budget and
   nonmoving chunks (for example 8 chunks of 64) until connection teardown.
   Materialize completion/payload storage only as concurrency needs it. A split
   ring can keep sequence metadata eager while lazily backing payload chunks.
   Publish fully initialized chunks with release/acquire before claiming an
   enqueue ticket; allocation failure must not leave a claimed ticket unusable.
   Initialization must use the correct physical slot sequence, not reset a
   reused ring position to generation zero. This adds at least chunk lookup,
   a branch and often an atomic load on enqueue/dequeue; chunk creation adds a
   slow allocation/CAS race. Deferred allocation also moves the time at which
   allocation failure can occur and needs an explicit error contract.

2. **Worker-owned shared slabs with per-connection quotas.** Reuse completion
   chunks across sequential connections, retaining the 512 per-connection
   concurrency limit rather than a smaller global budget. A globally bounded
   shared pool could let one connection starve another and is not equivalent.
   Slab lifetime must outlive every public send and every cancellation dispatch,
   including foreign-thread completion releases. Shared free queues introduce
   cross-connection cache traffic and release work; owner posting adds scheduling
   overhead. Worker shutdown must drain leases before deleting the slab.

For either direction, a completion address cannot be reused until the receiver
has consumed its result and rejected buffers/shared body, then released the
lease. Cancel/close must fail queued commands and join send/cancellation work;
they cannot free chunk storage merely because `fail_all` resumed waiters. Waiters
can resume inline. Raw pointer reuse needs a proven lease/generation discipline;
an untagged intrusive lock-free freelist would create a new ABA risk. Chunks
should never be reclaimed mid-connection in the simplest design. Existing member
task lifetime rules still require retaining the connection until its public
tasks finish; shared slabs must not silently rely on a destroyed worker/context.

Validation would keep 512 accepted outstanding leases, attempt the 513th under
real backpressure, exercise cancellation/close while leases and foreign posts
exist, and repeatedly reuse completion addresses. Then measure connection setup,
steady static throughput, and many-round cold RSS/owned-page plateau. The current
final candidate remains frozen; these are future architectural options, not a
reason to add allocator collection to the hot path.
