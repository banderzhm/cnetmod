# Frozen ELF task allocation audit, 2026-10-18

This is a read-only disassembly/DWARF audit. No compilation, server, microbenchmark or workload was run. The actual build is `/var/tmp/cnetmod-h3-arena-build`; a private read-only mount mapped it to the recorded `/tmp/cnetmod-h3-arena-build` solely for inspection. Compiler command is Clang `-O3 -g -fno-omit-frame-pointer -march=native -mprefer-vector-width=128 -flto=thin`.

Frozen arena_h3_server SHA256: `96333f69a82f1b62c91c6655cb939064a8048471aa5bbcd567a1a574b15256b5`.
quic_connection.o SHA256: `b5f580040f9b5664676ffaab0f414a5dbc47ddfd03e977817c289257e7af619f`.
quic_stream.o SHA256: `6692b74aa4468ef2e3064abe10182b90c30c56d2669ec349b0a3a5a324467e8d`.

Both .o files are LLVM bitcode; llvm-objdump correctly rejected them rather than inventing native instructions. The final linked ELF is authoritative for ThinLTO allocation elision. The inspection script verified frozen input hashes after inspection. Full evidence: `/var/tmp/cnetmod-h3-task-static-96333f69-20261018/{inspect.json,inputs-before.json,final-binary.disassembly.txt}`. The full disassembly is approximately59MB. llvm-symbolizer and llvm-objdump are `/usr/sbin` tools. Numeric addresses below are final ELF virtual instruction addresses, not file offsets or sampled return PCs. For a runtime stack return PC from a call, symbolize PC-1.

## Actual allocations, not source task counts

| Target | Allocating call instruction | Requested bytes/alignment | Compiled DWARF source | Evidence |
|---|---|---|---|---|
| 1RTT decoded frame outer process_frames | 0x36302f -> aligned operator new PLT0xf64c0 |80/64 |task.cppm26; connection.cpp2590 factory,2337 caller |0x363025 mov edi,0x50;0x36302a mov esi,0x40; resume0x35e120 stored at0x363042 |
| STREAM std::visit branch coroutine |0x34ef30 -> same aligned new |128/64 |connection.cpp2592 lambda; variant alternative7 |0x34ef26 mov edi,0x80;0x34ef2b mov esi,0x40; resume0x35c540 stored at0x34ef35 |
| shared apply_stream_write |0x36b4e1 -> aligned new |608/64 |connection.cpp4577 factory;4489 flush caller |selected for non-null shared-body command; shared resume0x366230 stored at0x36b4f8 |
| owned apply_stream_write forwarding wrapper |0x36bda7 -> aligned new |752/64 |connection.cpp4571 factory;4491 flush caller |alternative to shared allocation; owned resume0x367c60 stored at0x36bdba |
| quic_stream::send factory |0x3920bd -> aligned new |88/64 |quic_stream.cpp54 |unconditional factory allocation; shared apply directly calls factory0x3920a0 at0x36772c(prefix),0x366710(body),0x366629(empty branch) |

These are actual non-elided allocating call instructions on the respective executed branches. This establishes calls per invocation of the identified path, not dynamic invocations/request or end-to-end allocator attribution. The prefix/body calls are mutually selected by nonempty spans; the empty branch is not a third send on a nonempty shared response.

## HALO proved by parent-frame addresses

- STREAM state handler already has no separate allocation: visitor resume0x35c540 initializes child process_stream_frame resume0x35bfd0 at parent+0x20 (lea at0x35c59d, child frame pointer at0x35c5a8); child cleanup0x35c530 is stored at parent+0x28. DWARF maps this to connection.cpp3198 process_stream_frame and2598 visitor call. Thus this source task is embedded in its allocating visitor parent.
- FIN close_local already has no separate allocation on the target write path: shared apply resume initializes embedded child at parent+0x1a8 (lea0x367799), stores imported close_local resume0x3661d0 via0x3677a0 and cleanup0x366220 via0x3677ae. DWARF identifies quic_stream.cpp89 and connection.cpp4635. The public close_local factory0x392110 still exists and allocates48B at0x392127, but this write path bypasses that factory. Counting the public symbol alone would give a false extra allocation.
- Owned forwarding apply already embeds the common shared-state task:0x367d53 loads common resume0x366230, stores it at owned parent+0x40 (0x367d5a), and stores cleanup0x367bd0 at parent+0x48 (0x367d5e/0x367d65). It does not call a second allocating common factory.
- process_frames resume0x35e120 calls the variant dispatch table at0x35e1d4; the allocating STREAM branch factory above is reached through that indirect dispatch. The shared apply, owned apply, STREAM visitor and process_stream_frame resume bodies contain no aligned-new invocation of their already embedded children.

For one incoming STREAM frame plus one successful nonempty shared write containing prefix+body+FIN, the target cluster has5 real frame allocations:2 receive +1 apply +2 send. Requested frame bytes80+128+608+176=992 before allocator rounding. For one incoming STREAM plus one owned nonempty write+FIN (low-payload baseline case), it has4:2 receive +1 owned apply +1 send,80+128+752+88=1048 requested bytes. These are conditional path examples, not a measured global allocation/request metric: multiple received frames, separate logical writes, flow-control retries, headers/body composition and non-STREAM frames change the denominator. The prior75–77 alloc/request diagnostic includes many other sites and used another frozen diagnostic binary. Do not subtract5 from that number as a measured improvement.

## Highest-value next experiment and boundaries

The historical baseline gap1.196M vs6.793M is not explained by static response fragmentation alone. This task cluster exists for low-payload owned baseline too. Removing no-suspend allocation adapters therefore has an evidence-backed baseline hypothesis, but no measured RPS claim is justified while correctness failures remain.

Minimal first isolation: retain public lazy task APIs; extract private synchronous stream byte/FIN accounting into quic_stream.cpp (send state/credit/total_sent/empty-send and close-local bodies). Both public wrappers and connection-owned synchronous callers use exactly those helpers. Keep connection-wide credit reservation, stream lookup, encoded fragment enqueue and owner cancellation in connection. Replace only internal apply_stream_write adapters with a synchronous result helper under the current state guard. This can remove1 allocating apply frame+1 owned send or2 shared sends, i.e.2/3 target allocation calls per successful logical write. Existing HALO FIN/owned-common children do not add extra savings.

Receive isolation: have decoded variant classification route STREAM to a synchronous state kernel preserving the current process_stream_frame body and error/notification order; retain asynchronous dispatch for genuinely suspending handlers. Removing both outer process_frames and visitor frames for STREAM can remove2 calls per handled STREAM frame; merely making the leaf handler synchronous does not automatically remove its allocating parents. Verify regenerated final assembly after any edit. Use separate control/candidate stage to distinguish send and receive gains.

Do not copy stream state rules into the6000-line connection. Decode classification and stream bookkeeping are separate responsibilities. Keep bookkeeping in existing quic_stream implementation; connection coordinates global flow credit and registry. Physical connection receive/stream implementation-file split needs moving its existing cpp-local pimpl into a nonexport implementation partition and should be a separate mechanical change. Preserve public module/type names; do not create a generic types file.

Preserve receive/flush serial domains, lazy public task semantics, inline channel wake reentrancy, local readiness shared owner, decoded STREAM span lifetime through push_received copying, whole logical-write credit reservation before prefix mutation, shared body ownership, cancellation tail ordering, real fairness/yield posts and IO waits. A helper must not access stream iterators after a callback/cancel can retire them. No sync helper should silently change errors to exceptions or become noexcept while allocating.

Prepared future dynamic probe files are cnetmod-h3-task-probe.{py,c}, cnetmod-h3-task-micro.cpp and README.md in this external tmp directory. They are not built or run. An actual aligned-new wrapper counts calls/bytes with fixed stack records and delegates to the real allocator; it must remain separate from native RPS. Static audit is sufficient to reject the old seven-constructions-equals-seven-allocations claim, but dynamic calls/request still needs an authorized diagnostic run after correctness freeze. Native control relink hash must match frozen candidate before accepting a probe, with identical PCMs/production objects/allocator.

## Source line provenance

All compiled source line numbers above come from this ELF's DWARF and must be quoted with its SHA. The worktree may change while lifecycle fixes proceed; current source line numbers can drift (e.g. current shared apply factory begins4590, compiled DWARF4577). Addresses and compiled DWARF are immutable evidence for this frozen ELF. Re-audit the final chosen binary before implementation or ranking.
