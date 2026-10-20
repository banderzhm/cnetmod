# Final system-allocator MTU CPU profiles

Both serial profiles completed successfully, with original summary.valid=true, explicit own SIGINT exit-2 and Docker cleanup0. They use the same frozen ELF `5bf46aeeee227934742969972dfa3e4705d5fb87e66289c9109f41d89f353470`, matching frozen source/dirtydiff and verified system allocator. GSO is on, receive capacity8192,32workers/64clients/64streams/64threads, original static URI/encoding, client CPU16–31 and5second workload. Only send discovery ceiling1472/4096 differs. These are two instrumented diagnostics, not a repeated ranking or standalone performance acceptance.

| Measure | 1472 | 4096 |
|---|---:|---:|
| Completed requests | 2,881,320 | 3,711,486 |
| 2xx headers | 2,881,369 | 3,711,539 |
| Completed / elapsed seconds | 508,169.3 | 654,583.1 |
| h2load printed RPS | 576,264.0 | 742,297.2 |
| CPU microseconds / completed | 34.064 | 26.768 |
| Kernel sample share % | 34.161 | 35.002 |
| Estimated kernel microseconds / completed | 11.637 | 9.369 |
| Estimated user microseconds / completed | 22.428 | 17.399 |
| Samples | 9,344 | 9,471 |
| Lost samples | 0 | 0 |
| Known-DSO unresolved raw addresses % | 9.429 | 11.002 |
| Unknown DSO samples | 0 | 0 |

CPU/completed changes by-21.42%. Kernel and user both decrease in the sample-based absolute attribution, although kernel relative share rises. CPU delta multiplied by sampled period share is an estimate, not a measured leaf timer. Samples sum exactly to captured counts and total periods; zero lost samples does not eliminate sampling variance, optimized-frame truncation or symbol limitations.

## Full DSO distribution

| DSO | 1472 share % | 4096 share % | 1472 estimated us/completed | 4096 estimated us/completed |
|---|---:|---:|---:|---:|
| [kernel.kallsyms] | 34.161 | 35.002 | 11.637 | 9.369 |
| [vdso] | 1.680 | 0.802 | 0.572 | 0.215 |
| arena_h3_server | 46.254 | 45.360 | 15.756 | 12.142 |
| libc++.so.1.0 | 2.729 | 2.777 | 0.930 | 0.743 |
| libc++abi.so.1.0 | 1.102 | 0.729 | 0.375 | 0.195 |
| libc.so.6 | 11.943 | 14.233 | 4.068 | 3.810 |
| libm.so.6 | 1.894 | 0.929 | 0.645 | 0.249 |
| liburing.so.2.15 | 0.235 | 0.169 | 0.080 | 0.045 |

## Selected self families

| Symbol/family | 1472 self % | 4096 self % | 1472 estimated us/completed | 4096 estimated us/completed |
|---|---:|---:|---:|---:|
| aes_gcm_enc_update_vaes_avx2 | 6.625 | 8.362 | 2.257 | 2.238 |
| pack_one_rtt_packet | 2.986 | 1.647 | 1.017 | 0.441 |
| __insert_with_size | 3.360 | 6.768 | 1.145 | 1.812 |
| quic_stream_scheduler | 2.354 | 1.679 | 0.802 | 0.449 |
| apply_stream_write | 1.252 | 0.792 | 0.427 | 0.212 |
| on_ack_received | 0.792 | 0.634 | 0.270 | 0.170 |
| tree_remove | 1.659 | 1.109 | 0.565 | 0.297 |
| getsockopt | 0.599 | 0.380 | 0.204 | 0.102 |
| aead_context_for | 0.278 | 0.232 | 0.095 | 0.062 |

AES relative share increases6.625%→8.362%, while its CPU/completed estimate is approximately2.257→2.238microseconds. This is not evidence encryption slowed down. Named aead-context lookup is only about0.095→0.062microseconds in self samples; this does not capture all inlined refcount/locking cost or prove it is zero. Named malloc/cfree totals and generic tree helpers are recorded separately in symbol-groups.json; unresolved libc addresses cannot all be assigned to allocation, and generic tree costs cannot all be assigned to recovery.

## Byte-vector insertion: measured family, unimplemented candidate

The combined byte-vector `__insert_with_size` family rises from3.36%/about1.145microseconds per completion at1472 to6.77%/about1.812microseconds at4096. This is an absolute estimate increase as well as a relative share increase. The profile does **not** establish that one callsite owns the family, that libc++ implements a pathological loop, or that replacing it will improve throughput. Packet size/call mix, inlining/debug attribution and sampling/run variance remain relevant. Caller stacks contain insertion under packet assembly and STREAM encoding; optimized inline labels are not a reliable unique source-PC attribution.

Exact current source candidates are `src/protocol/quic/quic_send_pipeline.cpp:129` (`quic_send_frame::append_to`), owned-byte insertion at133, shared span/body insertion at140, and its packet-builder call at `src/protocol/quic/quic_connection.cpp:4340`. STREAM encoding also uses `src/protocol/quic/quic_frame.cpp:720` and shared `append_bytes` insertion at28. Numbered excerpts and per-source SHA256 are retained in source-locations.json and verified against the same frozen metadata. The ELF SHA above binds these observations; no different binary is substituted.

A future minimal experiment could compare a checked contiguous data()/pointer range with the current iterator range, keeping bytes, reserve policy, empty-input handling, errors, ownership, cancellation, packetization and scheduling identical. It first needs actual ELF/callsite examination and an independent measured control. Do not add an untested pointer-range patch to the current repair, replace append with resize+memcpy without accounting for zero-fill, or infer an allocation count from self samples. No such candidate was implemented, compiled or benchmarked here. The more invasive visitor/range-merge/single-ledger ideas remain deferred under their separate lifetime review.

## Evidence limits

The earlier2cb off/on system profiles and older mimalloc profiles use other binaries/configurations and are not part of this same-binary comparison. Neither sampled shares nor these two profiles provide syscall/packet/allocation counts or a complete explanation of the agensio gap. Configured ceiling is not every actual packet size; the independent low-concurrency qlog preflight proves application sizes are reachable but does not measure the full timed/profile distribution. Current unresolved raw-address samples account for about9.43%/11.00%, despite all samples having known DSOs. Inclusive stacks must not be summed as disjoint costs.

Both raw perf.data and frozen ELF stay in the unique external directories in external-input-manifest.json. This archive contains original summaries, metadata/dirtydiff/proof, workload/server/perf logs, headers, compressed self/caller reports, all-row compact self counts, analysis, source excerpts and scripts. No perf.data, ELF, PCM or keys are copied. Prepared command/runner files are archive-time copies; runtime metadata and data/summary hashes bind the recorded profile.
