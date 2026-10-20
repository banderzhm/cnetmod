# HTTP/3 static hot path: read-only review of current candidate

Date: 2026-10-18. Workspace: `C:/Users/25007/.codex/worktrees/2c3c/cnetmod`, branch `fix/http3-arena-performance`.

**Latest validated snapshot:** native `05fe27108dd833f86493aa2d00eefe7efef9df47805be198ef3eacd09042c479` passes Linux35/35, Windows36/36, six peers on Linux and both Windows worker configurations, and60serial200k static rounds (12million requests, zero errors). Its official18-round comparison also has zero errors, but still trails agensio: best baseline2,887,181 versus6,056,989, static286,063 versus885,430 req/s. Memory is **not accepted**: final37s-idle RSS267,325,440 bytes, with continued growth across later rounds. Current CPU/memory diagnostics are underway. See the parent README and `lifecycle-final/{linux-final,windows-final,native-final-60-rounds,official-final-05fe}`. Historical snapshots and pending statements below describe investigation stages, not the latest acceptance status.

This report began as a read-only evidence review. The subsequent authorized sync implementation/build/test is archived in `lifecycle-final/sync-state/`; it has not been benchmarked. The frozen ELF allocation audit below still describes96333f69, not a measured allocation count for920f7172.

Latest confirmed allocation evidence supersedes the source-level task estimates below: frozen ThinLTO ELF96333f69 has5 target allocations for one incoming STREAM plus one shared prefix/body/FIN write, or4 for the owned low-payload path. The STREAM leaf, FIN child and owned forwarding common child are already embedded. See the archived `task-allocation-static-review.md` for addresses and sizes. These are conditional call counts, not a measured global allocation/request result.

## Main recommendation

Synchronous STREAM receive state and write accounting are now implemented and passed the Linux directed set. Public task laziness and state/credit/FIN ordering are preserved. Next validation is an allocation recount and a serial native comparison, coordinated separately from node-cache and buffer-pool candidates; these results remain pending. The failed large-UDP ceiling experiment is not being retried as this change.

The source-level normal shared-static-response path contains approximately seven removable task constructions: three for receiving one STREAM frame, and four for applying its prefix/body/FIN write. This is a count of task call sites, not proof of seven heap allocations: optimized coroutine allocation elision must be measured. There is no claim of an already-proven RPS gain.

## Evidence that can be quantified

All paths below are relative to the workspace unless explicitly absolute.

### Packets and accepted responses

`testing/bench/results/httparena-h3/2026-10-18-hotpath/diagnostic/cnetmod-h3-large-udp-ab-1792311419/summary.json` pins the conditional-nogso stage1 binary to SHA256 `f7872d2c046d3002a12a25dfb804bc6f8310f9ce15d1866d2b4c3b45f1011f79` and records three serial stage1 static measurements:

| Measurement | 2xx/elapsed RPS | Server UDP/request | Client UDP/request |
| --- | ---: | ---: | ---: |
| static-1-stage1 | 321627.03 | 12.06756 | 0.38385 |
| static-4-stage1 | 336461.02 | 12.06706 | 0.39756 |
| static-5-stage1 | 338286.42 | 12.06410 | 0.38894 |

Here server UDP is h2load's received datagram count divided by its 2xx count. Client UDP is h2load's sent datagram count divided by its 2xx count. These denominators are response headers, not an assertion that every header's body completed; the native h2load failure/completion fields must also pass the zero-error gate.

The older packet capture at `.../stage1/diagnostic/cnetmod-h3-packets-1792309067/summary.json` sampled 50000 cnetmod static datagrams: mean payload 1369.9279 B, median/p95/max 1472 B, zero payloads over 1472, zero IP/UDP length mismatches. Its static h2load log has 1590812 completed/succeeded requests, 1590879 2xx, 19214047 received UDP, zero failures/errors/timeouts. Data bytes/2xx are about 16.27 kB decimal per response.

Pinned agensio evidence:

- `testing/bench/results/httparena-h3/2026-10-17-official-h3/records/images.json` pins `agensio-official-h3` to image `sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504`.
- `.../2026-10-17-official-h3/agensio-runtime.toml`: workers=0 (cpuset CPUs), reuse_port="auto", idle_timeout=65, no request-per-connection cap, static cache max_file_size=4MB/max_size=256MB, static alias `/data/static/`, dual-stack 8443 h1/h2/h3 listener.
- `.../stage1/reproduce/cnetmod-h3-sendmsg-profile.py` uses that image, isolated loopback, server cpus=32, the same static mount and request list, client cpus16-31, 64 connections/64 streams/64 client threads/5s. It attaches strace for roughly 0.1s during the run, then captures 50000 packets. Those traces are perturbing diagnostics, not RPS ranking runs.
- `.../stage1/diagnostic/cnetmod-h3-packets-1792309267/summary.json`: 50000 sampled static datagrams, mean 44999.98748 B, median 46460 B, p95 55928 B, max65468 B, 49129 over1472, zero IP/UDP length mismatches.
- `.../cnetmod-h3-packets-1792309133/agensio-static-h2load.log`: 5553102 completed/succeeded, 5554252 2xx, 2329973 received UDP, zero failures/errors/timeouts: 0.41949 server UDP/2xx.
- `.../cnetmod-h3-packets-1792309267/agensio-static-h2load.log`: 5807934 completed/succeeded, 5809047 2xx, 2379492 received UDP, zero failures/errors/timeouts: 0.40962 server UDP/2xx.
- `.../cnetmod-h3-packets-1792309267/sendmsg-summary.json` records 1041 UDP_SEGMENT control messages plus many unsegmented iov payloads around46460 B. Thus the observed large packets are not exclusively a capture illusion from GSO.
- A read-only parse of the archived `agensio-static-sendmsg.log.gz` found817 successfully returned sendmmsg calls accepting7045 message entries, or8.623 accepted message entries/call, and no failed syscalls. GSO can represent multiple UDP segments per accepted message entry. This is only a short trace-window statistic.

The existing evidence therefore shows roughly29 times fewer observed server UDP datagrams/request for agensio static. It does not establish a29-times-lower syscall cost or allocation cost. No agensio allocations/request measurement exists in these records.

### Syscalls: distinguish the available counts from bounds

Current `src/executor/platform/io_uring_async_op.cpp:1684-1710` constructs bounded ordinary-stack scratch and calls nonblocking `sendmmsg`; capacity is16 datagrams. The cancellable batch entry at1863 first tries this helper and only enters a pending send/writability path on actual pressure. Current `quic_connection.cpp:3595` packs at most16 datagrams and has no per-packet send task on the normal primary-socket batch path.

Consequently12.065 UDP/request implies a theoretical best full-batch sendmmsg rate of about0.754 calls/request, not12 calls/request. The actual rate is higher when batches are short or retries occur. The archived cnetmod records lack a final-candidate batch-size/syscall histogram; an exact current syscall/request figure cannot be recovered from packet counts. Likewise dividing agensio's whole-run packet count by its short strace-window call count would be invalid.

### Allocations

`.../2026-10-18-hotpath/diagnostic/cnetmod-h3-memory-counter-run-1792311917/summary.json` is a diagnostic alias binary using the same mimalloc implementation, includes C++/DSO/aligned forwarding, excludes mimalloc-internal metadata, and records foreign_calls=0. Five200000-request static rounds give allocation-call deltas/request:

74.916205, 76.935520, 77.233405, 74.523255, 76.892865.

These are from conditional-nogso oldruntime, not the final candidate. Allocation-counter throughput must not be mixed with native RPS ranking. This run also did not complete the full intended retention workload; it is not proof of a final memory plateau.

The old two-map model is two node allocations per recovery-tracked packet, roughly2*12.065=24.13 allocations/request for static if all those packets are tracked and no node is reused. This is about32% of the old75-call total. It explains why bounded node reuse deserves the first A/B, but does not assert that cap64 eliminates every node allocation or that observed savings already match this bound. ACK-only/close packets and cache misses change the count.

Current code already has optimizations that invalidate direct reuse of the old allocator percentages:

- `src/coro/channel.cppm:215` uses inline_slot for capacity1, so per-stream readiness/default notification channels no longer allocate their one-slot array.
- Current1RTT decode calls `process_ack_frame_state` synchronously rather than constructing the process_frames/visitor/ACK-handler tasks.
- Current `quic_connection.cpp:616-620` reuses packet and plaintext vectors; `append_sealed_payload` in `quic_crypto.cpp:1621` writes ciphertext directly into reserved packet storage and uses an AEAD context cache.
- UDP receive payloads already use bounded small/large block pools (`async_op.cppm:243,249`, `buffer_pool_impl.cpp:90-115`); normal warm receive buffers must not be counted as fresh packet-vector allocations without observing pool misses.

Do not sum source-level candidates into a claimed current allocations/request result. Recount the final identical runtime/binary first.

### Existing profile

`.../stage1/diagnostic/cnetmod-h3-profile-1792308382/static-perf-self.txt`, old binary SHA256 `3bbf8402eb9d0cc0afa17455ddbdf0e6a7ae69ea5038a83011f4f4c495eefa9a`, has self samples: aligned_alloc4.13%, new[]2.51%, delete[]1.56%, vector insert1.88%, tree_remove1.00%, aligned operator new1.09%, pack_one_rtt1.50%, scheduler take1.10%, process_frames1.29%, kernel spin/softirq/copy/UDP work substantial. The perf run has1173727 2xx and94.14 server CPU seconds (about80.2 microseconds CPU/2xx); its unprofiled static reference differs (1604983 2xx,102.48 CPU seconds, about63.85 microseconds CPU/2xx). Profiling perturbs this workload.

Allocator self samples identify a direction; they are neither allocation call attribution nor a speedup budget. In particular channel inline storage and ACK synchronization postdate this profile. Keep all statements about current costs conditional on fresh same-candidate counters.

## Next candidate: synchronous state updates

Line references are from the current live source and may shift as lifecycle work lands.

| Current function/call | Why it can use a synchronous internal path | Normal shared-static task calls removable |
| --- | --- | ---: |
| `quic_connection.cpp:2578 process_frames`, visitor at2580, STREAM branch2586 |1RTT STREAM can call an extracted state handler directly; retain existing task slow dispatch for other frame classes |2 per received STREAM frame |
| `quic_connection.cpp:3186 process_stream_frame` |Entire body has co_return but no co_await; validations, push_received, credit and readiness changes are synchronous |1 per received STREAM frame |
| `quic_stream.cpp:54 send` called at connection4606/4612 |Only checks state/credit, advances total_sent, and handles empty write; no kernel work, buffering or suspension |2 per nonempty prefix+body |
| `quic_stream.cpp:89 close_local` called at connection4623 |Only changes stream state; no suspension |1 per FIN write |
| `quic_connection.cpp:4565 apply_stream_write(shared)` called from flush at4477 |Its only awaits are the above send/close calls; after replacing those, it can return expected synchronously |1 per shared write |
| `quic_connection.cpp:4559 apply_stream_write(owned)` forwarding overload, called at4479 |Only forwards to shared overload with empty owner; can return expected synchronously too |Additional saving for owned responses, not counted for shared static |

For one incoming STREAM carrying the request and one shared prefix/body/FIN response, total is7 task constructions removed. If a request spans multiple incoming STREAM frames, the receive portion scales3 per frame. Missing prefix/body or non-FIN changes the write portion. Channel readiness/response delivery can still create genuinely asynchronous tasks; those are outside this count.

`src/coro/task.cppm:24` requests64-byte-aligned allocation for a task frame. `task_awaiter::await_suspend` and final_awaiter use symmetric coroutine-handle transfer; these tasks do not each represent a reactor post or executor hop. Removing seven tasks therefore removes potential frame allocations and transfers, not seven event-loop turns. Clang may already elide some allocations. The frozen ELF audit supersedes arithmetic based on seven source constructions: the qualifying target allocation counts are5(shared) and4(owned). Neither can be divided by the old75-call diagnostic as a measured current per-request or RPS saving.

### Small implementation boundary

1. Extract private `process_stream_frame_state(const stream_frame&) -> void` in connection `.cpp` and declare it in `.cppm`. Keep its validation and state body in exactly the same order. The1RTT decode loop's existing ACK synchronous branch gains a STREAM synchronous branch; other frame types still use current task dispatch. Retain the old private STREAM task wrapper for slow dispatch initially to minimize handshake/multipath scope.
2. Extract synchronous `quic_stream` send-accounting and local-close helpers; existing exported send/close_local task functions call them so external API and sync_wait users remain unchanged. Prefer narrow friend access by quic_connection rather than exporting a diagnostic/internal public method. C++ module declaration visibility must be checked: a forward/friend declaration must attach to the same primary module and avoid a partition dependency cycle. No new module/types file is needed.
3. Make the two private apply_stream_write overloads return expected synchronously, or retain task wrappers plus a state helper for the initial same-layout A/B. The only executable call sites are the two flush command branches found by repository-wide rg. Change the owned forwarding branch too; otherwise an extra wrapper task remains for owned responses.
4. Do not change task's global allocation policy, transport MTU, packet segmentation, congestion admission, pacing, stream credit, or global coroutine scheduling to implement this candidate.

### Reentrancy and lifetime checks

- `process_stream_frame` calls readiness->try_send and accepted_streams.try_send. A channel can resume a waiter inline when the current executor matches. That reentrancy already exists before the task returns. Preserve the current local shared_ptr copy of readiness and avoid using stream-map iterators after a wake that can retire the entry. Existing abandoned-stream retirement/wakeup order must be copied exactly.
- The decoded stream_frame contains a span into decrypted plaintext. A synchronous state call must consume/copy it before the decode loop advances or replaces plaintext. `push_received` currently copies the accepted ranges. Do not enqueue the span or store a pointer to the local variant/decoded frame.
- STREAM receive runs under the same receive_mutex as today. Removing a task with no actual suspension must not move work outside that serial domain or release/reacquire the mutex.
- apply_stream_write takes refs/spans into a dequeued command owned by the flush frame. That command must remain alive through synchronous application, and shared_stream descriptors must still copy the shared body owner before command destruction. Keep header-only storage owned as today.
- Keep whole-logical-write connection/stream credit prechecks before mutating prefix/body accounting. Do not merge empty-write and FIN behavior or partially account the prefix on failure. Preserve existing bytes_sent and first_offset computation before packet fragmentation.
- apply_stream_write's tail may cancel receive_wait_token; legacy adapters can resume inline. Preserve the cancellation point and the fact that no stream iterator/frame data is accessed after it. The caller still holds its current serial-domain guard.
- Public send/close task wrappers must remain lazy. Calling them without awaiting must not mutate state earlier than before. Only the connection owner's internal call switches to the synchronous helper.
- Exception propagation/destructor scope changes need review: do not mark allocating helpers noexcept or replace errors with exceptions. Existing std::expected error values/order remain identical. Removing allocation failures from unused coroutine frames is the intended difference.
- No fairness yield is removed by these no-await helpers. Keep writer coalescing post, listener receive quantum posts, real socket/pacing waits, and completion publication order unchanged.

### Responsibility boundaries and file organization

This proposal must not accumulate more transport/stream logic in the6000-line connection implementation. Keep two responsibilities explicit:

- Packet decode state dispatch owns classification of the already-authenticated decoded variant and the decision whether a handler is synchronous or may suspend. It does not own stream send accounting, buffering, credit limits, or FIN transitions. The connection entry point remains a thin coordinator under its existing serial-domain guard.
- Stream bookkeeping belongs to `quic_stream` and its existing `quic_stream.cpp`: local send permission, total_sent advancement, empty-write behavior and local FIN state. The connection only validates connection-wide credit, identifies the stream, invokes bookkeeping, fragments/enqueues frames, and publishes completion. Do not copy these state rules into connection as a performance shortcut.

For the first isolated performance change, extract the existing bodies without duplicating logic: the public stream task API delegates to the same stream-owned synchronous body used by the connection. The connection's STREAM async wrapper similarly delegates to one existing-body state implementation. No new public module or public type name is needed; private declarations are sufficient. This adds only the narrow dispatch/adapter calls to the connection, rather than a second implementation of state handling.

A physical implementation split is desirable but should be a separate mechanical change with its own module build validation, not mixed into the first performance A/B. Current obstacle: `quic_connection_impl` is defined only in `quic_connection.cpp`; merely moving member definitions to another `.cpp` leaves their pimpl incomplete. A correct split could move that existing internal definition into a non-exported implementation partition, then place packet decoding/state-dispatch member definitions in `quic_connection_receive.cpp` and stream command application in `quic_connection_stream.cpp`. Both implementations import the same internal partition and preserve all current exported module/type names and pimpl layout. Avoid a generic `types` file or new public interface. The internal definition move is sizeable and requires explicit dependency/import review; do not improvise it during a frozen correctness build.

If a later architectural extraction is authorized, the natural state component is a connection-owned stream registry/credit coordinator delegated to by the connection facade, while individual byte/FIN state remains in quic_stream. That is a cohesive responsibility split; a bag of callbacks or moving declarations between files alone would not reduce coupling. It is larger than this seven-task experiment and should not block the immediate correctness/final freeze.

## A/B plan, separate from correctness and node reuse

1. Finish all lifecycle/cancel/credit-stall directed tests and freeze the exact production candidate. Any remaining static failures block throughput interpretation. Preserve the independent node-cache A/B result first; use the same node-cache state in both later variants.
2. Build one new common set of module PCMs/layout declarations exposing both synchronous state helpers and the temporary legacy wrappers. Produce control/candidate connection implementations that differ only in the marked synchronous call selections. Both use the identical helpers and unchanged exported API/ABI/layout. Keeping temporary wrappers makes control use the old task chain while candidate bypasses it, without comparing stale PCMs/runtime.
3. Archive hashes for source snapshot, common PCM files, compiler flags, every non-replaced object/archive member, fixture dataset, loadgen and server binaries. All unrelated members must match. Do not combine a baseline binary from an older lifecycle snapshot with the new candidate.
4. First run separate fixed200k native correctness rounds with full body completion and zero failed/errored/timeout, then isolated-netns native baseline/static ABBAAB serial rounds,3 each,32 workers,64 connections,64 streams,64 threads,5s, identical cpus/driver. Retain every round and compare completed successful response RPS/CPU seconds; do not select a faster failed round.
5. In separate diagnostic-counter runs, measure allocation delta/request and alignment/size bins for the same frozen control/candidate. Add a low-rate return-address/stack sample if needed to distinguish task frames from map nodes/vectors; keep this out of native RPS runs. Required outcome is an actual reduction matching some of the seven eligible frame sites, not merely a lower allocator self percentage.
6. Collect cheap per-worker batch histograms (attempted/accepted datagrams and EAGAIN/retries), recovery-tracked packet count, received STREAM-frame count and applied prefix/body/FIN write count in a diagnostic-only build. Reduce once outside the hot path. This provides exact calls/request and task-call denominators; no per-packet logger or atomic shared global counter.
7. Existing semantic tests should cover send credit, empty send/FIN state, receive overlap/gap/final-size, reset/abandoned streams, shared-owner lifetime through ACK/loss/PTO and retry. Add only missing behavior tests, not implementation-mirroring helper tests. Reuse HTTP/3 large-response/interoperability/multipath tests and Windows asynchronous stop/cancel tests after final relink.
8. Re-run retention on the chosen binary. This candidate must not add a cache or retain historical stream/frame owners; shared owners should still expire after ACK/discard/close. No commit/RPS claim until final artifact validation passes.

## Secondary candidates and limits

- `pack_and_send_packets` currently allocates pending.reserve(16) even when no packet is admitted, then a datagrams vector. A count/array or owner-scoped reusable scratch can remove two short-lived allocations per batch, but a coroutine-local fixed array enlarges every frame and connection-level reuse needs proven unique flush ownership across all call paths. Measure actual batch fill and frame-size distribution before prioritizing it over fixed per-request no-await tasks.
- The shared response's first fragment can straddle prefix/body and construct a boundary_chunk before encode_frame copies it. Direct encoding into the final owned first-frame vector can remove one temporary allocation/copy/request while keeping shared-body frames and FIN offsets identical. It is smaller potential than the task cluster and needs byte-for-byte wire/offset tests.
- Late whole-frame cwnd rejection after MTU-sized frame construction explains why naively enlarging maxpayload can worsen progress and copying. The failed65527 experiment reduced packets/request but static fell to roughly81–84k RPS. It does not justify changing1472 or transparent GSO again. A future explicit transport batching/offload design would need its own datagram-boundary/progress/cancellation contract and congestion accounting, outside this minimal proposal.
- Old perf shows significant kernel packet work, so reducing allocator calls alone is unlikely to close the entire agensio gap. The next safe experiment should make a measured incremental claim, not predict parity.


# Update: actual frozen ThinLTO allocation evidence

See archived `task-allocation-static-review.md` for final ELF96333f69 addresses, sizes and HALO proof. This supersedes any seven-source-tasks-as-seven-allocation estimate above: normal one-STREAM/shared-write example is5 allocating frames; owned low-payload example4. process_stream_frame, FIN close_local and owned forwarding common child are already HALO embedded. Exact calls/request and throughput gain remain unmeasured. No compilation or workload was run.


# Existing baseline batching evidence and syscall/coroutine limits

Read-only offline extraction on2026-10-18, no new network run. Historical official-isolated baseline selected rates are cnetmod1,196,473 and agensio6,792,921 RPS; these are archived measurements, not final-candidate claims. Their three h2load log rounds all report0failed/errored/timeout and completed==succeeded==2xx:

| Archived baseline round | cnetmod observed server receive datagrams/completed | agensio observed server receive datagrams/completed |
|---|---:|---:|
|1|1.1220661541|0.0312650333|
|2|1.1374897662|0.0312660892|
|3|1.1384859087|0.0312658004|

Sources: `testing/bench/results/httparena-h3/2026-10-18-lifecycle/official-isolated/records/{cnetmod-official-optimized,agensio-official-h3}-baseline-h3.log`; selected JSONs under that run's official-selected/baseline-h3/64. The old cnetmod baseline packet sample (stage1/diagnostic/cnetmod-h3-packets-1792309067/summary.json) has median/p95/max44-byte UDP payload, mean42.85182. Agensio sample1792309133 has median1622, mean1622.85534, all50000 captures over1472. This shows radically different aggregation behavior even for tiny baseline replies; static-file fragmentation alone cannot explain the baseline gap.

Important qualification: apparent agensio baseline receive datagrams are not proven individual wire UDP segments. Offline parsing of archived stage1/diagnostic/cnetmod-h3-packets-1792309267/agensio-baseline-sendmsg.log.gz matched2285 full iov/control pairs, all with ceil(iov_len/UDP_SEGMENT)=2. Common pairs are1622/1332(639),1624/1334(459),1621/1331(323),1625/1335(219). Example: SOL_UDP cmsg_type0x67 cmsg_data"\x34\x05"=>segment1332, iov1622. Captures/GRO may preserve the combined1622-byte view. Therefore the roughly36-fold historical h2load observed-datagram ratio cannot be called36-fold fewer wire packets. Do not extrapolate a precise wire-packet/request count by mixing this short strace window with whole-run counters. It is strong evidence of batching/response aggregation, not an exact decomposition of wire/GRO/GSO costs.

The static trace differs: archived sendmsg-summary.json has many unsegmented46460-byte message entries (3061 at46460,1043 at46462,639 at46458) and1041 GSO cmsgs. Static capture includes roughly46k payloads; a large fraction are actual unsegmented UDP sends, not exclusively GSO artifacts. Static historical observed receive counts are approximately0.41 versus cnetmod12.06 per response, using the previously documented denominators. Never generalize baseline GSO structure to all static sends.

Syscall cost is a third denominator. Archived agensio static trace has817 successful sendmmsg accepting7045 messages,8.623 messages/syscall, with possible multiple GSO segments/message. Current cnetmod ordinary primary socket batch already uses capacity16 sendmmsg, so12UDP/request does not mean12send syscalls/request. The minimum at full batches is about0.754sendmmsg/request; actual fill, retries and EAGAIN recovery need final same-binary counters. There is no aligned whole-run current cnetmod versus agensio syscall/request measurement in these records.

Coroutine cost is separate again. cnetmod task await_suspend/final_suspend symmetrically transfer handles; a no-suspend task is not an executor turn. ELF-proven4/5 target allocating frames imply allocator calls and coroutine state transitions on qualifying paths, not4/5 reactor posts or syscalls. Agensio is pinned C++ image dd75c505... with32CPU workers/reuse-port/static cache; its apt build dependencies mentioning ngtcp2 do not establish the library actually used by its server. Existing records contain no agensio alloc/request, per-callsite allocation, or coroutine scheduling profile. Do not claim it avoids coroutines or assign the remaining gap to one language/runtime without evidence.

The prior cnetmod mimalloc-alias75–77calls/request run and allocator perf self percentages identify candidates but belong to older diagnostic binaries. Current node reuse, ACK synchronous fastpath, channel inline slot, buffer reuse and batching have changed allocation behavior. The next meaningful measurement remains identical-PCMs/native control versus synchronous STREAM/write bookkeeping candidate after correctness closes, recording actual allocation call delta and successful completed-request CPU/RPS separately. A historical baseline packet aggregation improvement already motivated the conditional flush coalescing post; these old packet ratios must not be attributed to the current candidate without recounting.


Current correctness investigation is archived separately at `testing/bench/results/httparena-h3/2026-10-18-hotpath/lifecycle-final/qlog-failed-r6/findings.md`: all138 target response FINs arrived, while41 request FINs had no ACK-covered original/retransmitted packet; no HANDSHAKE_DONE reached the pinned ngtcp2 client, explaining absent application PTO via its confirmation gate. That is a correctness finding, not performance acceptance of either external optimization proposal.

The post-correction recovery design review is in [remaining-ledger-design.md](remaining-ledger-design.md). It separates PN-vector removal, restricted ordered range merging, and a future single ledger. None is implemented: an immediate callback can expose both maps' cursors to shared-owner deleter reentry, and event-vector allocation prevents assuming a no-throw callback. The independent malformed-ACK prevalidation fix has [old-failing/new-passing evidence](ack-range-validation/README.md); that correctness result is not visitor performance evidence.
