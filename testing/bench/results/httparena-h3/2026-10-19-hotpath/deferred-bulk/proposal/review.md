# Deferred bulk plaintext candidate — independent review

External-only proposal; not applied, compiled, or exercised. HEAD is 5afc506dfefe021ca17caadaa1870572255dea1f, but the base is the owner's current guard source, SHA256 50cd8c3ba2cc226c45e83a483ba4c04665e229b76aae69ebf4523170abb7f1c2. That guard source already differs from HEAD outside this function. Baseline.cpp preserves its exact CRLF bytes. Do not replace production with candidate.cpp: use the one-function patch after checking its base.

Patch SHA256: 3f0aefad9df3d885dd092dc123876e8379d4e17d1cf941ab57c5439285477746.
Candidate full-source SHA256: d2a8a62e278ea04b2ea2350c459dce595b409a17a8483ee94114b4e1e0fa731a.

## Scope and cost

ACK/PATH_ACK encoding and optional PMTU PING remain the eager plaintext prefix. The existing payload and packet cold reserves remain in place, so a cold ACK does not acquire an extra small allocation before the full reserve. No vector, frame container, new recovery storage or allocator dependency is added. The existing retransmittable storage owns the chosen CRYPTO and reliable frames; a size_t counts the selected DATAGRAM deque prefix. Selection and all metadata creation still happen before admission.

Only CRYPTO/reliable frame copies, DATAGRAM copies, and padding writes move after existing whole-packet admission. There is no earlier cwnd budget, frame splitting, alias change, in-place seal, receive scheduling change or public interface change. Successful packets add a traversal of the retained reliable descriptors, and DATAGRAMs add a size-only deque scan before append/pop. This can regress a workload with few rejected packets; performance acceptance requires a separate paired experiment.

The maximum avoided bytes for a rejected attempt are its planned payload length minus the already-written ACK/PING prefix. Existing full reserve capacities and recovery metadata costs remain. No RSS, peak-memory or throughput saving is claimed.

## Migration invariants

| State/operation | Required equivalence and candidate behavior |
|---|---|
| Ownership domain | Selection, both checks and append occur synchronously under the same receive/state lock; no new await/callback is inserted. |
| Prefix and order | Retain exact ACK/PATH_ACK then optional PMTU PING prefix. Append retransmittable storage in its insertion order: selected application CRYPTO first, then generic reliable/priority selections in their old order. HANDSHAKE_DONE stays at its original position in the encoded reliable queue; it is not elevated ahead of CRYPTO/PING. Append DATAGRAM prefix last, then padding. |
| Selection length | planned_payload_size initially equals prefix size. Add exactly frame.size() for every old successful append; whole-frame fit comparisons use that value. CRYPTO retains its existing treatment even when its complete encoding already exceeds the nominal payload budget. |
| Scheduler | Same take/requeue calls, same remembered indices and reverse rejection loop. No extra take/peek/requeue or budget restore. |
| CRYPTO | Same pop/take_handshake_data and send-offset update before admission. Same reliable storage ownership and generic FIFO restoration on rejection. |
| DATAGRAM selection | Read-only scan selects the identical contiguous prefix that the old front/pop loop selected. No other code can change it under the held lock. |
| DATAGRAM rejection | Both path/amplification and cwnd exits drop precisely that prefix once before the unchanged reliable restore loop. Neither branch preserves previously dropped DATAGRAMs or touches the unselected suffix. |
| Empty output | planned_payload_size==0 returns the same empty packet without PN/accounting. It also discards the selected prefix: valid encoded DATAGRAMs cannot be zero length, so normally the count is zero; this retains the old pop behavior even for an internal zero-byte frame. No whole-queue drain is introduced. |
| PMTU padding | planned=max(unpadded size, existing exact required_payload expression). No write occurs until admitted; append then resizes only if still below planned. The expression deliberately keeps the original fixed 16-byte overhead assumption rather than broadening this optimization into a PMTU change. |
| Admission | Identical estimated_packet_size, anti-amplification ordering, PTO bypass, CLOSE exemption and DATAGRAM/PMTU congestion inclusion. No check is performed per frame or against a smaller budget. |
| Accepted bytes | Appending selected reliable descriptors and DATAGRAMs yields exactly the former plaintext sequence and length. No mutation of descriptor/header/body/owner/offset/FIN. |
| Post-admission | The entire source tail starting at constexpr pn_length is byte-for-byte unchanged: PN allocation, header/AEAD/HP, error fallback, ACK clearing, path accounting, probe state and recovery ownership/alias flags. |
| Scratch | Rejected payload scratch now contains only its ACK/PING prefix, rather than discarded bulk bytes. This scratch is private and cleared on next use; packet scratch remains empty. No caller reads its old bulk contents. |

The semantic equivalence argument covers ordinary nonthrowing operation and existing expected-error paths. std::bad_alloc timing can shift because bulk append now follows selection; the project does not provide strong exception rollback guarantees for this function. This proposal does not add such a guarantee or use exceptions as a flow-control path.

## Required validation before any production acceptance

1. Deterministic old/new packet comparison at fixed PN/keys/time: empty, ACK/PATH_ACK ranges, PING, CRYPTO fresh/retransmit, HANDSHAKE_DONE, owned/shared STREAM header+body+seam+FIN; single/multiple priority classes and generic FIFO mixtures. Verify decrypted frame order and UDP payload bytes for CID sizes and all supported ciphers.
2. Boundary fits: next frame equals remaining payload; one byte too large; no room after ACK; CRYPTO at/over budget; no payload; zero data FIN; multiple DATAGRAMs including zero-length *application* data (the wire frame remains nonempty), selected prefix fits while following suffix does not.
3. Force both rejection branches with reliable-only, DATAGRAM-only and mixed contents. Assert same restored reliable ordering/metadata/budget, same exact dropped DATAGRAM count and preserved suffix, unchanged PN/recovery/ACK/probe state, no UDP submission, no padding/bulk copy. Repeat rejection and then release the gate and compare eventual output.
4. CC exceptions: ACK-only at full cwnd, CLOSE bypass with other selected frames, PTO bypass/alias retirement, lost near-full STREAM+FIN with ingress blocked, exact padded PMTU success/loss/reject, antiamp despite PTO/CLOSE.
5. Actual fault regression on unchanged append_sealed_payload and HP branches: real AEAD failure, HP failure, empty native SSL error fallback, failure after an earlier packet was sealed in the same local batch. Assert no unsubmitted prefix escapes and no repeated internal completion; pending/future writes see terminal state under the existing completion contract.
6. Existing UDP batch accepted-prefix/EAGAIN/cancellation/stop-buffer-join tests and raw/H3 interoperability, including DATAGRAM conformance. The patch does not move the socket boundary, but this protects integration claims.
7. External diagnostic first: rejected attempts and avoided byte distribution versus successful descriptor-traversal count, then matched performance A/B with the same allocator, receive ceiling, send ceiling, GSO, original workloads and correctness gates. Do not infer savings from configured MTU alone or combine with the in-place-seal candidate.

## Checks actually performed

- External preparer asserts the pinned guard base, CRLF format, one-function scope, unchanged number of vector/frame-storage declarations, retained reserves and a byte-identical post-admission tail.
- Generated unified patch passes plain git apply --check against the current working tree (no application).
- Preparer AST parses; production source SHA remains pinned above after preparation.
- No C++ build, runtime test, traffic, profile, debugger attachment or source modification was performed. The required matrix above is pending, not claimed coverage.