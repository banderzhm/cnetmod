# Short-connection zero-error correction and retest

The reported short-connection errors are resolved in a **separate corrected load-generator build**. Both retained cnetmod configurations, the original baseline, and all three peers completed both short-connection configurations, three rounds each: **36/36 rounds with zero connect/read/timeout errors and zero refused upgrades**. The cnetmod server binaries are unchanged. This is not an unmodified-official-client result.

## Root cause and evidence

Pinned gcannon `11c802b2bdfe9af2fac4ac2571ae2de4c530cc62` recycles buffers with:

```c
io_uring_buf_ring_add(..., bid, w->buf_mask, w->buf_index++);
io_uring_buf_ring_advance(w->buf_ring, 1);
```

The insertion offset is relative to the **current tail**. When publishing one buffer and immediately advancing the tail, the offset must be zero. Incrementing both offsets writes future slots while publishing stale slots; the kernel can receive into a buffer the client is still parsing. The [upstream liburing manual](https://kernel.googlesource.com/pub/scm/linux/kernel/git/axboe/liburing/+/refs/heads/master/man/io_uring_buf_ring_add.3) specifies this rule. Initial bulk provisioning retains its incrementing offsets because it advances only once after the batch.

The diagnostic client counts errors at their actual branches without suppressing the original counters. In the original-behavior three-round diagnostic, all failures were Upgrade HTTP parse failures: 142/123/158 events, no send failures or pipeline underflows. One event in the third round occurred during warmup, explaining its 157 measured read errors. These diagnostic events cover warmup as well as measurement; they do not replace the earlier frozen 382/66 figures.

Eight captured failing **client-memory snapshots** in `evidence/bad-*.bin` start with `81 05 68 65 6c 6c 6f` (`hello` frame), followed by `1 101 Switching Protocols...`: exactly seven bytes of the HTTP prefix were overwritten. These are receive-buffer snapshots, not packet captures. The deterministic userspace regression exercises the client's actual `return_buffer` function with liburing helpers for 100,000 returns, including tail wraparound: original fails, one-line correction passes. Three further instrumented short-connection rounds then have zero parse failures and zero pipeline underflows.

The [one-line client patch](gcannon-buffer-ring.patch) sets only the recycling offset to zero. Counters, HTTP/WebSocket parsing, workload, connection retirement, CPU placement, warmup, duration, thread count and server behavior are unchanged. cnetmod's own buffer recycling already uses offset zero. No server workaround, delay, protocol relaxation or error-counter suppression was added.

## Pinned method and reproduction

- HttpArena stays at `8977be2721a5aba7b320345cddfcd6f6b654e698`. Its driver and official workload are unchanged. Original client source/binary are preserved; the original binary SHA remains `48d5fa257ca5559f2da88138ff2016adb3191914e3a685ca7e9be0a355eddf2e`.
- Corrected client SHA is `ad285056f461aaa2cf92e42a1a18091166f7cde93a112e11653d44dbe7d7fbc2`. [Manifest](client-correction.json) records source hashes and `make -j4`. There are no diagnostic counters in this measured binary.
- Same isolated WSL namespace and Docker images as the [earlier evidence](../2026-10-20-optimization/README.md). Runs are serialized under the exclusive test window. Client CPUs 16–31, 64 client threads; official server cpuset fallback to all 32 available CPUs. Same tuned batch/CQE settings and opt-in worker affinity for the 16/24-worker variants.
- `echo-ws-limited`: 512 and 4096 connections; pipeline 1; retire after 10 frames; five-byte text payload; 5 seconds; three rounds; 100 ms warmup; 2-second cool-down; no TLS or compression. Stop-time outstanding requests are retained in raw data and are not silently classified as errors.
- Only the short-connection profile is remeasured here. **Do not combine these scores with frozen-client echo/pipeline scores into an updated 8-case pass count.** The full +10% target is not established by this follow-up.

Build and verify a new client directory (Linux):

```bash
python testing/bench/prepare_ws_gcannon.py /path/to/pinned-gcannon /path/to/new-corrected-client
python testing/bench/check_gcannon_buffer_ring.py /path/to/new-corrected-client
```

For each of `cnetmod-ws-reviewed16-b64`, `cnetmod-ws-final24-b64`, `cnetmod-ws-baseline`, `vanilla-epoll`, `genhttp-ioxide`, `fulmine`, run the unchanged driver inside the isolated namespace, serially:

```bash
export GCANNON=/path/to/new-corrected-client/gcannon
export LOADGEN_DOCKER=false SKIP_TUNE=true ZRK_CMD=/usr/bin/false
bash scripts/benchmark.sh ENTRY echo-ws-limited --save
```

The original frozen-client series and its error counts remain in the earlier report. All frameworks in this new series use the same corrected binary. No corrected-client result is compared against an old-client peer score.

## Results

Official best-of-three received frames/second; every individual round is retained in [rounds.json](rounds.json) and the six [raw logs](evidence/results). Error counts below sum all six rounds per entry, including refused upgrades.

| Fixed server configuration | 512 connections | 4096 connections | Errors |
|---|---:|---:|---:|
| cnetmod 16 workers, batch 64 | 1,925,491 | 1,919,206 | 0 |
| cnetmod 24 workers, batch 64 | 1,331,742 | 1,292,655 | 0 |
| Original cnetmod baseline | 1,125,482 | 1,096,330 | 0 |
| Vanilla epoll | 1,070,274 | 1,376,500 | 0 |
| GenHTTP Ioxide | 1,078,146 | 1,104,826 | 0 |
| Fulmine | 824,185 | 836,060 | 0 |

The fixed 16-worker configuration is **+78.59%** at 512 connections and **+39.43%** at 4096 against the fastest measured peer in each configuration. Its six rounds received 53,915,884 frames with zero counted errors. These are local WSL measurements, not upstream leaderboard scores or a universal throughput guarantee.

## Acceptance and verification

- A zero-error gate checks all 36 complete rounds and requires exactly three per entry/configuration. [Gate output](evidence/verification/zero-errors.log).
- `compare_ws_target.py` now returns failure when **any candidate round** has a connect/read/timeout error or refused upgrade, even if throughput exceeds +10%. Throughput-only status stays explicit; overall `accepted` requires both. Independent wire-protocol gates remain required.
- Three C++ regression executables pass; 16 Python tests pass, including error-in-a-slower-round and refused-upgrade rejection. WS and WSS each pass 22 malformed cases plus fragmented UTF-8, ordered control/data and partial-tail checks. Official WebSocket validation: 7 passed, 0 failed. [Verification evidence](evidence/verification), [protocol evidence](evidence/gates/cnetmod-ws-reviewed16-b64).
- Production code was not changed or rebuilt in this correction. The earlier ASAN/UBSAN checks remain earlier evidence, not newly claimed runs. No Autobahn certification or native Windows verification is claimed.
- Task containers/services and the namespace were stopped, host network settings verified unchanged from the host namespace, and the keepalive/test window released immediately after checks. [Cleanup record](evidence/verification/cleanup.json).

`SHA256SUMS.json` covers this report and its evidence. Diagnostic logs and their explicit instrumentation patch are separate from the six uninstrumented measured logs.
