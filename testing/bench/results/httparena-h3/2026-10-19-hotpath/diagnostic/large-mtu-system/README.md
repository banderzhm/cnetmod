# Larger-MTU diagnostics: completed evidence, not performance acceptance

The nine original configuration rounds use frozen DATAGRAM-fixed fc6e50f5935ab7bde6b3767a459fc36efc4b2e666239c0f5099de812db7061c3 with receive65507/GSOon/system allocator. All9 timed gates and cleanup passed. This implementation has the subsequently confirmed arm_path_mtu_discovery deadline-overwrite defect, so these are historical diagnostic results, not final1100000-RPS acceptance. Same-block order is16K/32K/65K,65K/16K/32K,32K/65K/16K. Original32workers/64clients/64streams/64threads/5s/static URI/encoding/clientCPU16-31 remain unchanged. Timed success requires done==succeeded>0,2xx>=done and all errors/non2xx0. Printed RPS is distinct from completed/elapsed; extra headers are retained separately.

|Send ceiling|Median completed/elapsed|Median printed RPS|CPU us/completed|p99 ms|Received UDP/completed|DATA bytes/received UDP|RSS bytes|
|---|---:|---:|---:|---:|---:|---:|---:|
|16384|493035|561074|36.19|23.66|1.3820|11773|158789632|
|32768|413581|469828|44.19|35.49|0.8800|18489|204218368|
|65507|337009|383362|56.14|41.41|0.5917|27499|279932928|

DATA/completed is approximately16270B in every arm. The loadgen UDP counter expands UDP_GRO original segments and includes probes/control; it is not a syscall count or pure response STREAM-packet count. Allthree matched blocks show lower throughput and higher CPU/completed at32K/65K relative to16K. There was no same-run4096 timed control. Fresh-server RSS is not retention evidence.

Independent fixed4096-request1c1m/rps1000 qlog diagnostics each completed every request/response FIN with all4096 statuses successful. Peer maxUDP65527, server receiveTP65507. Ordinary non-PING application maxima were16384/32768/65506; do not label the last maximum65507. Exact target-sizedPING+padding probes were received and client-ACKed, followed by near-ceiling application packets. Counts119/247/503 probes allACKed;65K target clientACK86ms and ordinary near-ceiling packet92ms. Times are qlog relative milliseconds. Full target/ACK CSV shows423 adjacent65K probe receives sharing a millisecond; +128 steps plus final51B. Frozen fc6e50 arm disassembly confirms an already-present next-probe optional is still overwritten at0x302dc0. These timings reflect that defect rather than a valid1ms-per-step search. Raw qlogs remain external with hashes.

## Guard-fixed two-profile comparison

Both profiles use the same final guard-only ELF2fa51db8652cf8a0d166d79ab4d36241c9a5c789aa8ed4c0cb5e57eb56bb7d28, source-guard snapshot1321inputs, unchanged historical metadata/diff, system allocator proof andGSOon. Compare4096/8192 then65507/65507 serially;99Hz cpu-clock/fp with CPU IDs. Both loadexit0/originalvalidtrue/ownSIGINT-2/lost0/Dockercleanup0. These are single perturbed profiles, not repeated rankings or final acceptance.

|Measure|4096/8192|65507/65507|
|---|---:|---:|
|Completed|3450898|2007468|
|2xx headers|3450952|2007478|
|Completed/elapsed|610778|352806|
|Printed RPS|690180|401494|
|Process CPU us/completed|28.430|55.398|
|Estimated kernel us/completed|9.776|10.367|
|Estimated user us/completed|18.654|45.031|
|AES self estimated us/completed|2.230|2.435|
|Const-iterator byte insert self us/completed|1.834|17.443|
|Samples|9347|10671|
|Lost samples|0|0|
|Known-DSO unresolved addresses|10.85%|18.07%|
|Unknown DSO samples|0|0|

Estimated function/category CPU is process CPU/completed multiplied by sampled period share, not direct leaf timing. Inclusive stacks are not additive and samples are not invocation counts. About26.97us extraCPU is predominantly user-space. Actual byte-insert PCs identify the existing-capacity XMM contiguous-copy loop, not a pathological noncontiguous iterator algorithm. Across all byte-insert leaves630/3372, the physical caller pack_one_rtt_packet+0xe0c returns at0x30bffc for459/2420 samples: approximately1.40/12.56us per completed response. addr2line binds it to quic_send_frame::append_to and connection.cpp4341. This proves packet materialization attribution; it does not distinguish successful packets from late-CC rejected/requeued packets or count rejects.

Other major65K insert callers are apply_stream_write+0xec7/0x3124c7 with472samples and STREAMencode_frame+0xa5/0x346745 with460samples. Every latter sample's next physical caller is apply_stream_write+0xf28/0x312528. Shared body ownership survives HTTP3 async_send_shared: HEADERS+DATA header are prefix, sharedbody owner remains separate. The first chunk spanning the prefix/body boundary takes the owned boundary_chunk path, copying body into boundary_chunk and again into encode_frame output. Only following chunks beginning after the prefix use shared_stream directly. Large PMTU can therefore make most of a response fall into this first owned chunk. This is an existing representation limitation, not evidence of a lost owner or a runtime per-asset count.

libc0x176ed3 disassembles to rep stosb; recorded and examined libc buildID503200d7fda94a5dc6058d7e0694e5d1dcb2e372 match. Of130/491samples there,104/468 physical callers return at pack_one_rtt_packet+0x1573/0x30c763 (append_sealed_payload,connection.cpp4436), approximately0.316/2.430us per response. This is a specific zero-fill PC; other unresolved libc addresses are not assigned to it. Kernel copy_from_iter/clear_page are separate families and must not be added as if each is redundant user-space copying.

Host aggregatebusy85.5%/84.8%; processCPU/observedwall16.82/19.11 CPUs, activeTIDs29/31 out of66recorded with no unmatchedTIDs. The larger arm uses more CPU per response; this is not evidence of simply lower CPU utilization. Lastprocessor snapshots are not residency, connection ownership or reasons for blocking. The same source/ELF mainline is necessary for subsequent candidates; deferred materialization and mixed prefix/body are unimplemented experiments relative to this evidence. Do not claim their speedup before independent regression/AB validation.

Original summaries, workload logs, commands, snapshots/proof, bounded timelineCSV and PC/assembly are included. Raw perf.data, ELF, raw sample stacks, qlog, keys and certificates remain outside this archive, listed by hash/path. The fixture symlink tooling failure and narrow correction are retained; historical input metadata was not rewritten to a later commit.
