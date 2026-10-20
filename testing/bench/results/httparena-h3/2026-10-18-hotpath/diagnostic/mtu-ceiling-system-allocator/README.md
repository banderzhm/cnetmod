# MTU ceilings with system allocator — diagnostic comparison

All three low-concurrency qlog preflights and all18 uninstrumented timed rounds pass. The same frozen ELF5bf46a... and source snapshot are used throughout; receive limit8192, GSO1 and system allocator stay fixed. Only send discovery ceiling1472/4096/8192 varies. Default adapter limits remain1472. This is a native matched-block diagnostic, not an official framework ranking, general PMTU reliability proof or long-retention test.

The low-concurrency preflight uses1client/1stream/1thread,4096 fixed requests and32serverworkers. It verifies peer receive limit65527 and server receive limit8192, complete request/response STREAM byte ranges and FINs, and received application1RTT packet lengths. Retry/VN are recognized as non-PN packets; normal packets still require frames and PN. The preflight excludes control/probe-only packets from response counts.

| Ceiling | Max response QUIC packet bytes | Distinct response packets | Packets/completed |
|---:|---:|---:|---:|
| 1472 | 1472 | 48336 | 11.80078 |
| 4096 | 4096 | 18498 | 4.51611 |
| 8192 | 8192 | 10579 | 2.58276 |

These are qlog raw.length QUIC packet bytes, not an independent wire capture. A short-header1RTT packet is final in a UDP datagram; the ordinary application packetizer sends one per original UDP segment. GRO/GSO message sizes, syscalls and wire packet counts are different denominators. Per-stream packet memberships are stored separately because a multi-stream packet can otherwise be counted twice. No syscall count is invented. Configured ceiling is not the size of every packet, and this low-concurrency evidence does not measure the full timed-run size distribution.

The timed runs keep the original static URI/encoding and32workers,64clients,64streams,64threads,5seconds, client CPU16–31, private Docker-anchor loopback and IPv4-first gai wrapper. Three blocks/profile balance1472/4096/8192 ordering. Full source/ELF hashes are verified outside load. No qlog, shim, LD_PRELOAD or tracing is enabled during timing. Header-only2xx are not counted as completed throughput.

Median matched changes versus1472 (each percentage is computed within its own block, then the three values are summarized):

| Profile | Ceiling | Completion gain per block % | Median gain % | CPU change per block % | Median CPU change % | p99 change per block % |
|---|---:|---|---:|---|---:|---|
| baseline-h3 | 4096 | +3.37, +7.32, -3.66 | +3.37 | -1.07, -2.87, +2.08 | -1.07 | +50.99, -14.17, +3.51 |
| baseline-h3 | 8192 | +0.26, +8.21, -4.40 | +0.26 | +0.63, -2.85, +2.58 | +0.63 | +3.06, +26.67, +3.33 |
| static-h3 | 4096 | +22.00, +30.10, +22.08 | +22.08 | -20.14, -22.24, -17.03 | -20.14 | +6.15, -17.30, -26.89 |
| static-h3 | 8192 | +25.73, +23.41, +20.53 | +23.41 | -19.82, -16.76, -13.67 | -16.76 | -20.78, -8.35, -28.48 |

See [all18 rounds](rounds.md), CSV and ab/comparison.json for completed/elapsed versus printed RPS, every CPU/p99/RSS measurement and all paired changes. Fresh-server RSS is not evidence that memory returns after idle. Static improvement versus1472 does not imply the largest ceiling wins; compare4096 and8192 across all blocks. Baseline results must be judged separately. The chosen controlled-path setting still needs the coordinator's long retention and official/framework validation.

All raw qlogs and candidate ELF remain in the external directories named by the manifests; no ELF, PCM, raw qlog or key is archived here. The observer C draft was not used or compiled. JSON-SEQ byte/record, file64MiB, total1GiB and90second limits fail closed. No prior preflight failure occurred in this run; larger application sizes were established at the original4096 requests without altering probe step/interval. Repository protected interoperability JSON was not touched.

Loopback supports these controlled datagram sizes. This does not establish unfragmented PMTU reliability on external routes; existing DF/fragmentation and black-hole behavior require separate review. Earlier mimalloc performance records cannot be combined with this system-allocator comparison.
