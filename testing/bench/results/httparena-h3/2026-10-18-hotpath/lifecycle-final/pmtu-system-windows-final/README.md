# Windows system-allocator PMTU/final regression

Native Windows x64 Release build passed. CTest independently enumerated and
passed **38/38 tests, zero skips,110.00 seconds**. All six peer directions passed
with both one and two workers, for **12 actual case/configuration passes**.
This validates the combined ownership/protection-error/GSO/PMTU/ACK candidate;
it is correctness and interoperability evidence, not a performance ranking or
memory-retention measurement.

The explicitly requested final driver reconfigured
`CNETMOD_USE_MIMALLOC=OFF`; `CNETMOD_USING_MIMALLOC=FALSE` is also verified in the
preserved CMake cache. **All24 built executable PE import tables and actual
MSBuild link command records contain no mimalloc dependency or mi_version**.
The PE import tables show the actual system runtime dependencies. Old unused
mimalloc DLL files can remain in the reused build directory; their presence
does not demonstrate loading or override. No source default was changed.
See `allocator-proof/proof.json`, the cache, import dumps and link command files.

The frozen Windows arena executable SHA256 is
`b913b6262a3fca93054a33d144dc762c69e06f5f44b63cb2aad778d2afe48c5a`.
`native-binary-hashes.json` identifies every built executable; all24 hashes
matched again after the peer runs. The actual HEAD and dirty diff are retained,
rather than describing the candidate as clean HEAD. Before/after source
ledgers cover1136 files under src/include/cmake and the relevant tests/fixtures/
benchmark/build scripts. They match exactly. That scope is explicit and does
not pretend to be the Linux runner's broader source inventory.

The new `quic_pmtu_cc_large_response_after_loss` case passed6.61 seconds.
The notification-lifetime regression, private outbound PTO, HANDSHAKE_DONE,
send cancellation, loss/congestion accounting, HTTP/3 lifecycle, QPACK and
remaining directed cases all passed in the same complete CTest attempt.
No failure, retry, fixture edit or production edit was required in this run.
`test-enumeration.json`, `tests.log` and `LastTest-full38.log` retain the
independent enumeration and complete raw results. The Windows LastTest log
preserves its original console encoding rather than being rewritten.

## Actual peers and platforms

| Direction | Peer platform | workers1 | workers2 |
| --- | --- | --- | --- |
| aioquic-server → cnetmod-client | Windows Python | pass | pass |
| aioquic-WebTransport → cnetmod-server | Windows Python | pass | pass |
| wtransport-WebTransport → cnetmod-server | Windows Rust,3 independent connections/config | pass | pass |
| cnetmod-server → curl-HTTP3 | Native Windows pinned curl | pass | pass |
| cnetmod-server → ngtcp2/nghttp3-client | WSL/Linux | pass | pass |
| ngtcp2/nghttp3-server → cnetmod-client | WSL/Linux | pass | pass |

ngtcp2/osslclient and osslserver are explicitly **WSL/Linux peers against native
Windows cnetmod** over mirrored IPv4 loopback, not native-Windows ngtcp2 results.
The optional driver uses the pinned native curl SHA
`b2ad02d8334b4a24783cb08f615236f1572ee92edc08394baf775e509721dc81`.
Curl asserts HTTP3, exactly200 and the exact stripped health body. The ngtcp2
client independently validates h3 ALPN, structured200 headers and exact
downloaded body. All result JSON and raw per-process stdout/stderr are retained.

The mandatory reports preserve their three optional SKIPs each (system curl
lacks HTTP3, and external ngtcp2 templates are not supplied there). **Those
SKIPs are not passes.** The separately run optional driver proves each of those
remaining directions with the same final Windows cnetmod executables. The
verified summary joins actual passes through explicit evidence paths.

Every optional cleanup report is empty, and every Linux owned-process-group
cleanup audit ends with an empty `after` list. A final native Windows scan
against the exact24 validation executable paths found no live target process.
The drivers used retained process handles and unique Linux nonce/PID/group
records; there was no global kill-by-name operation. All work was serial.

## Reproduction/provenance boundary

External records and frozen24 PE files remain under
`C:/Users/25007/.codex/tmp/cnetmod-h3-pmtu-final-system-windows-20261018-ffa91ab479f4422791aaeacc51dbe847`.
The reused build is `C:/Users/25007/.codex/tmp/cnetmod-h3-windows`. The archived
drivers/commands retain those original machine-specific paths as provenance;
a different machine must configure matching dependencies/build paths and
generate fresh ephemeral test certificates. This is not a claim that historical
absolute paths are portable or that archived records alone recreate all PCMs.

The exact build/CTest launch used the archived
`cnetmod-h3-pmtu-final-windows-final.ps1 -RecordsDirectory <unique records>`;
its target list includes `quic_pmtu_cc_server`. The peer stage used the archived
`run-peers.ps1 -Evidence <same records>`, which passes explicit new result paths
to both mandatory and optional drivers. The protected repository
`testing/http3/h3-interop-results.json` remained unchanged with SHA256
`ca41c4cb5adcc34ee16042e00ea9c6dfb248b9a17649995dbacfeb43b64d8b66`.

This repository archive includes text logs, cache/link/import evidence, fixture
driver snapshots, exact commands and SHA ledgers. It omits executable/object/
library/PCM files, temporary private keys and certificate PEM files. The
verification scripts themselves were external; no production or fixture source
was edited. `sha256.json` records the archived bytes.
