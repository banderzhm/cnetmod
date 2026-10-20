# System allocator: matched GSO off/on CPU profiles

Both original summary.valid values are true; this offline review does not rewrite them or run traffic/builds. Same frozen arena ELF SHA2562cb22a32818646fbf63575370adf6a0c48b3ba5df4bbbc3d3002ab2838003e0a,actual HEADd0d920791a1d226711176df47148b5e98a2f8713 plus the same dirtydiff/source snapshot. Both use system allocation,32workers,64connections/streams/threads,client CPUs16–31,one5s static workload,99Hz cpu-clock and fp stacks. The allocator proof,actual commands,metadata,source diff and runner are archived. No ELF/perf.data/PCM is copied into the repo; external-inputs.json records paths/digests.

These are two single perturbed diagnostics, not an official RPS ranking or repeated CPU benchmark. Earlier05fe/mimalloc profiles are different binary/allocator evidence and must not be folded into this comparison.

## Integrity, samples and complete DSO shares

Both record processes received the runner's explicit SIGINT after workload and returned actual-2; no exit code is inferred. Original self-report preambles and independent full-self reports agree on0lost; header-only metadata are also archived. Full count rows sum exactly to recorded8974off/8855on samples and event periods90,646,463,740/89,444,443,550. Sample periods are constant in these captures, so sample-count and period shares coincide; rounded displayed percentages were not summed.

Frozen ELF,source-metadata,diff,and allocator-proof hashes independently match both summaries. Source inputs match the recorded1684-entry metadata map. Current production is deliberately not rehashed while the PMTU agent owns builds; these are checks of the frozen recorded inputs, alongside the original runner's successful source/binary checks. Runtime library symbols come from available installed DSOs; headers lack BUILD_ID feature. No new symbol package was installed.

|DSO|GSO off self %|GSO on self %|
|---|---:|---:|
|[kernel.kallsyms]|53.8222|33.2468|
|arena_h3_server|32.5496|46.5048|
|libc.so.6|8.3575|12.7273|
|libc++.so.1.0|2.1061|2.9701|
|libm.so.6|1.3149|1.8069|
|[vdso]|1.1478|1.6375|
|libc++abi.so.1.0|0.5572|0.9599|
|liburing.so.2.15|0.1449|0.1468|

Kernel4830/8974=53.8222% off versus2944/8855=33.2468% on; user46.1778%→66.7532%. Unknown DSO0both, but unresolved raw addresses within known DSOs are558/8974=6.2180% and873/8855=9.8588%. Off unresolved:libc479,libm54,libc++14,liburing11;on:libc752,libm84,libc++31,liburing6. Missing names therefore prevent precise allocation/lock attribution; do not call libc's entire share malloc/free.

The renderer with field separators unexpectedly produced no rows despite exit0; it was not used as evidence. The retained full self reports use ordinary columns with exact sample/period counts. Independent parsing rejects absent rows or any sample/period mismatch. Original bounded self and full compact TSVs are both available; gzip files preserve full raw self and caller stacks.

## CPU per completed request and symbol changes

|Metric|Off|On|
|---|---:|---:|
|Completed requests|1,874,844|2,632,413|
|2xx headers|1,874,898|2,632,469|
|Header-only delta|54|56|
|Elapsed seconds|5.66|5.65|
|Printed RPS|374,968.8|526,482.6|
|Completed/elapsed RPS|331,244.5|465,913.8|
|Server CPU seconds|93.64|92.91|
|CPU microseconds/completed|49.9455|35.2946|

Done=succeeded,all failed/errored/timeouts/non2xx0. Completed throughput uses done;2xx-only headers are excluded. This one profile pair has+40.6555% completion rate and−29.3337% CPU/completed, directionally consistent with the separate three-pair system A/B, not an independent stable-gain claim.

Multiplying measured process CPU/completed by sampled share gives estimates:kernel26.8817→11.7343µs/completed,user23.0638→23.5603,arena DSO16.2571→16.4137,libc4.1742→4.4920. These are sampling-derived attribution proxies, not exact category/leaf timers; the event period window differs from the full process CPU measurement. Kernel share is only time attributed to attached server threads, not whole-host network work or client CPU.

|Self symbol|Off %|On %|
|---|---:|---:|
|AES-GCM update|4.3793|6.0982|
|_raw_spin_unlock_irqrestore|5.4156|4.0203|
|pack_one_rtt_packet|1.7161|2.6652|
|STREAM scheduler take|1.5266|1.8972|
|tree_remove|1.0475|1.9989|
|apply_stream_write|0.8135|1.2987|
|recovery insert_sent_packet|0.7912|1.3213|
|recovery recycle_sent_packet|see full self TSV|0.9825|
|loss insert_inflight_packet|see full self TSV|0.7792|
|malloc|0.7689|0.9712|
|cfree|0.6352|0.9260|

AES-GCM's relative share rises4.3793%→6.0982%,but its estimated CPU/completed is2.1873→2.1524µs(393/540samples). This does not show encryption getting slower; a smaller kernel denominator exposes unchanged user work. Vector insert family similarly estimates1.1632→1.2037µs;packet pack0.8571→0.9407;STREAM scheduler family0.9016→0.8809. Small changes are not reliably separable from sampling/run variation. Tree helpers increase in share but generic instantiations serve multiple maps and must not be wholly attributed to recovery.

Named recovery insertion/recycle functions alone:1.9501%→3.2072%,estimated0.9740→1.1320µs/completed. Generic tree_remove+balance:1.7606%→2.9588%,0.8794→1.0443µs;named malloc+cfree:1.4041%→1.8972%,0.7013→0.6696µs. Those are disjoint self-symbol groups, not complete inclusive map/allocator costs. They cannot establish allocation counts,cache hit rate or a guaranteed savings ceiling. GSO-on getsockopt-named self samples total65,0.7340%,estimated0.2591µs/completed;include its safety checks in costs rather than deleting them.

## Two next structural experiments (proposals only)

1. **Reduce actual QUIC packet count using ACK-validated larger UDP, after PMTU/CC correctness.** GSO leaves each1472 packet's sealing,header protection,frame metadata,two tracking inserts,ACK/recovery and scheduling intact. The current static set averages about16.27KB of HTTP DATA and roughly12observed UDP/response; an independently validated8KB path could reduce required data packets substantially for that body mix, though exact per-asset ceilings/controls must be counted. There is no justified speedup percentage from this profile. AES alone is about2.15µs/completed;vector insert1.20,packer0.94,scheduler0.88,named recovery1.13 are evidence of repeated user work,not all necessarily removable per-packet cost. The overall remaining user~23.56µs is an upper bound on all user work,not an attainable saving.

   Existing boundaries:connection.cpp packet_payload_budget920,probe ACK raises ceiling2970,STREAM write fragmentation4747,late cwnd admission4393;GSO additions shifted the old report's source lines. PMTU phase1 suppresses probe loss congestion and fixes Reno flight accounting;dynamic MDS is a separate outstanding design. Maintain peer limit,exact ACK validation,per-path MDS/pacing/min-window/initial-window rules,and never bypass cwnd. A queued large never-sent frame may need safe split/provenance when available credit/PMTU falls;already sent PTO aliases currently use full-frame identity and cannot be arbitrarily split. Keep loss/cwnd-reduction,migration/blackhole,shared-owner/FIN tests. In a future experiment hold receive capacity/config/layout constant across both controls,change only validated send ceiling,record achieved probe ACK/size and packet/response distribution,then repeat same-system GSO on/off independently. Do not use a bare65507 ceiling replacement or reattribute the pre-HANDSHAKE_DONE65527 failure to current code. PMTU agent owns this direction;no patch applied here.

2. **Remove duplicated ordered packet ledgers/ACK work before growing caches.** quic_recovery.cpp127–135 calls detector.on_packet_sent and then inserts a second sent map;loss_detection.cppm71–74 adds time/bytes/PN metadata to its own ordered map. Detector ACK code around115–125 lower_bounds,builds newly_acked PN vector and extracts nodes;recovery.cpp158–178 then finds each PN in its second map and extracts again. Loss codecppm169–185 scans/extracts eligible detector nodes,thenrecovery.cpp200–220 re-finds/extracts the same PN from the second map. Both caches are bounded64nodes per map/space and clear mapped owners before caching;they remove some allocation but not duplicate tree updates/traversals/PN result vectors.

   Small staged experiment:an internal acknowledged/lost range visitor or sorted-range merge can deliver metadata in-place and avoid result-vector allocation/repeated logN lookup,while retaining public standalone loss_detector API and ACK/error/loss ordering. The exact newly_acked order is ascending within each descending ACK range,so it is not one globally sorted list;do not replace finds with an invalid single monotonic iterator. Stronger experiment:one recovery-owned ordered record containing send_time,bytes,frames,probe and alias metadata;factor RTT/loss policy into a small internal algorithm/view with no frame ownership,so the standalone detector facade can retain its own existing semantics. Keep responsibilities in recovery/loss implementation files rather than adding more connection logic. This is an architectural refactor,not a trivial cache switch.

   Preserve three packet spaces,ACK-of-ACK validation highwater,bytes-in-flight exactly once,probe-loss vs ordinary-loss classification,early-data discard,PTO alias lifetime/order,and immediate shared-owner release on ACK/loss/discard. Keepbounded reclamation/copy/move/close tests;no unbounded PMR highwater. Baseline low-payload writes may have fewer packets but still exercise ledgers and per-response bookkeeping,so both baseline/static need A/B. Current dedicated recovery self estimate~1.13µs plus shared tree~1.04µs and named allocator~0.67µs only justify prioritizing measurement;generic tree and most raw libc symbols cannot be assigned to these maps. Before implementation,an offline caller attribution or external bounded counters should establish actual duplicate operations/cache misses/allocations per completed request. No claim that half the maps means half the remaining23µs.

The separate immutable AEAD-context borrow idea remains lower-priority until callers/lifetime/concurrency can preserve public seal semantics;atomic_shared_ptr cost is not isolated by these profiles. Do not replace its concurrent loads with unowned raw pointers solely because AES now tops the relative self list.
