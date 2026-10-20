# Explicit QUIC UDP GSO candidate — prepared, not applied or measured

Patch `gso-opt-in.patch` SHA256: `0992b679da0d687a4f6c7480e92b90655432c08d1fcc30925caecb79fc257a63` (eight files,25714bytes).
`inputs.json` contains all eight actual base/candidate file digests. The connection base includes the notification RAII fix (`eed001f58285451ae216f67aaa687787fb327643e1240fe204af66a01b09afef`). `git apply --check --whitespace=error-all` passed. No compilation, test execution, network load or production edits have occurred for this candidate.

## Scope and contract

The new Linux-only `async_sendto_segmented_batch(ctx, socket, original_datagrams, token)` is explicit; generic batch defaults stay unchanged. QUIC config defaults false; the server has a pre-start setter, and the arena exposes same-binary opt-in via `CNETMOD_QUIC_GSO=1`. Windows keeps the existing submission path. Enabling on a non-io_uring backend reports operation_not_supported.

Every original UDP remains independently sealed, numbered, admitted against cwnd, retained in recovery and included in aggregate-byte pacing. No PMTU or fragment-size change. The synchronous helper groups at most sixteen adjacent equal-peer packets, equal-sized interiors and at most one shorter final packet; empty packets remain independent. Existing packet buffers are scatter iov entries, with fixed stack ancillary storage. No combined user-space payload allocation or async ancillary lifetime. Group total is bounded to 65507; current 1472 maximum gives at most 23552 bytes.

Both UDP_CORK and native UDP_SEGMENT are queried before each actual grouped attempt. A nonzero native override or unavailable query takes the existing ordinary path. Native option mutation must be serialized by the opt-in caller. These two extra getsockopt syscalls must be included in A/B cost. No global setsockopt, capability cache or silent default change.

A positive sendmmsg result maps kernel-message prefix through cumulative original-datagram counts. It never falls back, even if a later message errored or cancellation raced. Negative ENOPROTOOPT/EOPNOTSUPP/EINVAL/EIO/EMSGSIZE makes exactly one ordinary attempt on the original datagrams, whose progress/error is authoritative. Zero/EAGAIN uses the existing ordinary cancellable backpressure adapter. No combined buffer is ever sent as an ordinary UDP.

EMSGSIZE is deliberately not treated as proof that GSO is permanently unsupported. Linux `udp_send_skb` checks segment header+size against route fragment size and returns EMSGSIZE; ordinary sending must preserve that real per-datagram error. Conversely, a group-only aggregate/offload constraint must not reject otherwise valid original UDPs. Only a negative, zero-new-progress grouped attempt may retry. Sources: [Linux UDP implementation](https://github.com/torvalds/linux/blob/master/net/ipv4/udp.c), [udp(7)](https://man7.org/linux/man-pages/man7/udp.7.html), [sendmmsg(2)](https://man7.org/linux/man-pages/man2/sendmmsg.2.html).

## Prepared regression coverage

The patch extends existing `test_udp_batch` fixtures, retaining their actual receiver bytes, datagram boundaries, deadlines and trailing sentinel rather than comparing helper internals. Four new Linux-only cases:

1. `udp_segmented_batch_preserves_ipv4_ipv6_tail_empty_peer_and_cap_boundaries`: sixteen 1472-byte packets; IPv4/IPv6; checksum-disabled fallback; forty mixed-peer/short/empty inputs; 24KB inputs that exceed group aggregate cap. All exact bytes and boundaries required.
2. `udp_segmented_batch_reports_original_prefix_and_preserves_early_cancel`: three valid uniform packets followed by invalid port; returns original count three, then reports suffix error; pre-cancel never sends. This distinguishes accepted message count one from original datagram count three. A distinct trailing sentinel after the cancel/error checks requires exactly the three accepted packets before it; extra prefix retransmission cannot hide unread in the receive queue.
3. `udp_segmented_batch_fallback_preserves_pending_cancel_full_queue_and_reuse`: existing real send-buffer pressure, writable wait, full-SQ cancellation and token reuse fixtures, using explicit API. Both UDP payload and cancellation behavior remain required.
4. `udp_segmented_batch_retains_native_cork_and_segment_options`: native segment64 keeps two128-byte originals producing four64-byte datagrams; native CORK remains enabled across submission, manual uncork yields one256-byte datagram. Native option values are asserted unchanged.

Existing QUIC shared-body/alias ACK-loss-PTO, packet failure-before-submit, crypto terminal close, stream fairness, shutdown and cross-context cancellation tests remain mandatory. Backend tests alone cannot establish connection-level semantic preservation.

## Deterministic fault/negative controls to run later, serially

`gso_fault.c` is an external LD_PRELOAD shim, not built or run. It targets only sendmmsg calls containing UDP_SEGMENT ancillary data. `CNETMOD_GSO_FAULT` selects EMSGSIZE/ENOPROTOOPT/EOPNOTSUPP/EINVAL/EIO/EAGAIN, prefix or observe. The first applicable call per process is affected and emits the actual message count/GSO count/stride/iov count/bytes; absence of the marker invalidates an injection result. `prefix` forwards only the first real kernel message when there are at least two and that first message is GSO, then reports the actual syscall result. `observe` changes nothing and reports the actual real GSO syscall result. Ordinary fallback calls remain unmodified. Negative injected errno modes simulate a zero-progress submission and must not be mislabeled real kernel rejection.

After the authorized candidate build, compile the shim outside the repo with `cc -shared -fPIC -O2 -o gso_fault.so gso_fault.c -ldl -pthread`. For each errno independently run the segmented layout/prefix/backpressure cases with LD_PRELOAD and CNETMOD_GSO_FAULT. Require the injection marker, exact receiver bytes/count, no duplicated sentinel, original accepted prefix and eventual cleanup. A process containing only a pre-cancel case may never call sendmmsg; that cannot count as injected coverage.

The prepared `../cnetmod-h3-gso-fault-check.py` driver runs eight serial filtered cases with stdout/stderr preserved before any assertion; its observe case requires first stride1472,16originaliov,23552bytes and real syscall result1, while prefix requires three originals/4416bytes,at least two kernel messages and real result1. Only the actual selected receiver test's success is accepted. A missing marker, absent real acceptance, timeout, receiver mismatch or test failure keeps summary valid=false and all raw evidence. It takes mandatory test/shim SHA arguments. Compile/execute later only in the assigned window:

```text
python /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-gso-fault-check.py --test-binary <frozen-test_udp_batch> --test-sha <exact-sha> --shim <external-gso_fault.so> --shim-sha <exact-sha>
```

Separately, on an isolated route with MTU1500 and IP_MTU_DISCOVER=IP_PMTUDISC_DO, two original1473-byte IPv4 UDPs must still report EMSGSIZE after the one ordinary attempt and deliver nothing; two1472-byte originals with an injected group-only EMSGSIZE must deliver both unchanged. Repeat IPv6 using its actual header budget, not IPv4 numbers. This distinguishes fallback from swallowing PMTU errors. Keep this route fixture external; do not mutate shared host routes/options.

Two explicit negative controls: replace cumulative-original-prefix return with raw kernel-message count and expect the three-valid-prefix regression to fail; bypass the native option guards and expect the native segmentation/CORK regression to fail. Do not claim these controls passed until executed. The cancellation-after-positive-progress race is preserved structurally and by existing semantics, but no deterministic syscall-race test is newly claimed here.

## Measurement gate

First finish the notification leak fix and its long retention validation. Then compile once with the candidate, test default-off and on from identical binary/source, and run all correctness gates before short serial A/B. Preserve 32workers/64c/64m/64t, static URI/body/encoding/CPU/gai namespace and readiness. Require exact done=succeeded=2xx and zero failures/non2xx/errors/timeouts, not substring success.

Measure completed/elapsed and printed rate separately, server CPU/completed response, p99, live/idle memory, actual sendmmsg/getsockopt call counts, original datagrams and grouped-message histogram. Datagram/capture counters can reflect aggregation and are not syscall counts. GSO does not remove per-packet AEAD/recovery/ACK costs. Existing one-profile kernel share is diagnostic evidence, not an upper bound on achievable gain. Keep PMTU/CC work independent: current larger-PMTU support has a fixed-MDS congestion-control gap and cannot be represented by a simple 65507 ceiling switch.

The prepared `../cnetmod-h3-gso-ab.py` requires one exact frozen binary SHA and a newly generated Windows-git source snapshot (actual HEAD/dirtydiff/all source digests). It copies that one binary once, clears inherited CNETMOD_QUIC_GSO for off and sets it to1 only for on. Fresh server each round;3pairs/profile,off/on,on/off,off/on;12total timed rounds. It uses the established image, isolated network namespace,lo-up,32binds,one exact-count readiness request+2s,client taskset16–31 and unchanged5s64c64m64t baseline/static workloads. Raw stdout/stderr/commands/server logs are saved before validation, CPU/RSS/p99/UDP/counts and header delta recorded, hashes rechecked each round; cleanup belongs to the exact created server/container. A cleanup failure invalidates the run.

Root approved timed acceptance matching official policy: done==succeeded>0,2xx>=done,failed/errored/timeout/non2xx=0. 2xx−done is separately recorded; completed throughput uses done/elapsed only. Fixed-n long regressions retain exact started/done/succeeded/2xx equality. AST and small synthetic checks passed (two valid timed cases,five rejected invalid cases); no process/service/load was started by those checks. Run later through the existing gai mount wrapper:

```text
wsl.exe -d archlinux -u root --exec unshare --mount /bin/bash /tmp/cnetmod-h3-isolated.sh python /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-gso-ab.py --binary <current-candidate-arena> --binary-sha <exact-sha> --source-snapshot <new-gso-freeze-snapshot>
```

The temporary API additions are candidates, not an unconditional public API commitment. Cross-module access needs an explicit executor declaration; the server setter exposes controlled same-binary toggling because its transport config is private. A generic batch flag or silently native socket option would broaden existing semantics instead. Stable A/B benefit is required to retain this interface; if the two option queries remove the gain, withdraw the GSO candidate instead of adding capability-cache complexity. The arena environment switch remains opt-in; it never changes the official selector/default or historical results.
