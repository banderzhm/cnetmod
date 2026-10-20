# static-h3 repair checks

These are low-load correctness checks, not a new HttpArena measurement group.
No benchmark token has been received for this repair phase. The previous
21-round group and its failures remain unchanged in `../2026-10-17-official-h3`.

## Diagnosis and changes

The previous `records/cnetmod-official-static-static-h3.log` reports
`ERR_QPACK_DECOMPRESSION_FAILED` in rounds 1 and 2. Not every one of the 15
failed requests has been traced to an individual packet.

* QPACK previously forgot references to entries known received by the decoder,
  before the field sections themselves were acknowledged. New insertions could
  evict entries that delayed response HEADERS still needed. A deterministic
  128-byte-table test reproduces the invalid decode without network load.
  `qpack-before-fix.log` retains the failing assertion; that initial test then
  dereferenced the failed result and exited with SIGSEGV. The committed test
  returns after the assertion to preserve a clean failure.
* Required Insert Count previously used the whole table's insertion count,
  including static/literal-only sections. It now describes the entries actually
  referenced, with zero for sections without dynamic references, as required by
  [RFC 9204 section 2.1.2](https://www.rfc-editor.org/rfc/rfc9204.html#section-2.1.2).
* Every referenced field section stays pinned until its own FIFO Section ACK;
  an Insert Count Increment alone does not release references. New insertions
  cannot evict unacknowledged entries, including during the same encode call.
  See [RFC 9204 section 2.1.1](https://www.rfc-editor.org/rfc/rfc9204.html#section-2.1.1).
  The unused, lifetime-growing encoder cancellation-ID set is removed.
* The server inserted a completed-header queue even for unblocked requests,
  but never erased it. It now erases request header queues and waiters on every
  coroutine exit.
* QUIC retained final sizes for every retired stream. Closed IDs now coalesce
  into intervals separately for the four stream types; final-size checks retain
  the most recent 1024 retired IDs. Old IDs remain closed and cannot reopen;
  gaps remain gaps so delayed earlier streams are still accepted. The unit test
  retires 400,000 IDs and checks four intervals and 1024 final-size entries.
  Out-of-order gaps can require more intervals; this is not a fixed-size cap on
  arbitrary input. [RFC 9000 section 4.5](https://www.rfc-editor.org/rfc/rfc9000.html#section-4.5)
  permits forgetting final sizes after closure. Older closed-stream duplicates
  are ignored; active/recent-stream final-size checks remain.
* A sub-packet write could return a transient flow-control error without sending
  the response FIN. Rejected writes now retain their bytes and wait/fragment
  against available credit. The wire regression sends eight serial 512-byte
  bodies with a 128-byte stream window and a 512-byte connection window, in
  both ordinary and dynamic-QPACK end-to-end suites.

`qpack-intermediate-literal-fixture.log` records an existing blocked-stream test
that relied on a literal-only section incorrectly carrying nonzero Required
Insert Count. Its second section now references the first inserted name so it
still exercises a genuinely blocked dynamic reference.

## Verification

Linux Release: 14 QPACK unit cases passed. The QUIC stream unit executable
(six cases) and ordinary/dynamic-QPACK end-to-end suites passed: four CTest
entries total. All six independent interoperability cases passed: aioquic in
both directions, Rust wtransport, curl HTTP/3, and nghttp3 in both directions.

`static-correctness.json` records 40 serial exact-byte checks: the pinned
official 20 static files, identity and official Brotli negotiation, on one
reused QUIC connection. Curl reported one new connection for the first request
and zero for the following requests. Payloads, status and encoding matched.
The diagnostic script and its log are retained alongside the JSON.

Windows Release build succeeded; all six selected CTest entries passed,
including ordinary/dynamic-QPACK E2E and one/two-worker interop gates. Native
gates cover aioquic in both directions and Rust wtransport; native curl has
no HTTP/3 and native nghttp3 is unavailable, so those three peer cases are
covered on Linux, not counted as native passes. Build and CTest outputs are
retained separately. Builds run serially between platforms, Linux uses at most
four jobs, and MSBuild `/m:1`.

There is no new RPS, whole-process memory plateau, long-duration soak, or
zero-error official static-h3 claim yet. After token handoff, rerun the pinned
official profiles serially with the same inputs/settings/statistics and retain
all three rounds in a new directory; do not overwrite the previous group.
