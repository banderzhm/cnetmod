# Upward ACK-validated MDS: real connection comparison

2026-10-18, Arch Linux / Clang Release / system allocator (mimallocOFF).
All four runs use a private `unshare --mount --net` namespace, one Path0,
loopbackMTU65536. This is an explicitly adapted **cryptography AEAD test peer**
using aioquic1.3.0 protocol machinery, **not stock aioquic interoperability**.
The adapter changes no installed package. Runtime provenance and successful
three-suite native-small-packet differential tests are included. No throughput
claim follows from these correctness runs.

Both versions preserve all current GSO, lifecycle/RAII, probe-loss, Reno-flight
and ACK-atomicity fixes. The only production difference in the new version is
`02-upward-validated-mds.patch`. Defaults and allocator are unchanged. Initial
CC policy14720/MSS1472 is retained; only matching real probe ACK publishes MDS.

| Variant | Result | Evidence |
|---|---|---|
| Old, no production diagnostics | Required negative | All16 epochs completed; final16KiB response timed out at2002.68ms |
| Old, diagnostics | Mechanism proven | Actual4096 probe ACK,16 real ordinary-loss epochs; floor2944; finalcwnd3009,flight0,noPTO,pendingframe4066; timeout2003.25ms |
| New, no production diagnostics | PASS | Exact16384 F bytes and FIN,2.58ms |
| New, diagnostics | PASS |16 real ordinary losses,lossfloor>=8192; finalcwnd8257,flight0,noPTO; exact16384 F bytes+FIN,45.09ms |

Both successful body hashes:
`47d30d4078849f62042ce5a0e873cd00cfdaecce1b8404708580bafb42edf7e5`.
Every run uses four confirmation requests per epoch,25ms intervals and75ms
quiet periods. Diagnostics observe/assert and do not alter stimulus. Old/new
accepted diagnostic reports and new plain report contain no `error` field and
have empty proxy errors. The old plain report's sole accepted failure is its
final-response timeout. Main orchestration now sets statusfailed on any later
exception; pure mocked post-acceptance exceptions passed2/2 negative controls.

New targeted units: congestion28/28 and loss_detection27/27,55/55 total. The
additional probe-blocked/peer-cap/small-frame-transition scenarios are not
implemented or claimed. Downward MTU, migration and blackhole fallback remain
outside this upward-only patch.

Fixture SHA256:

- Old plain:8342f588a6d74d4632f8de36f2fefcf8da02ddd52fa50872012f7e6b9df5c6ee
- Old diagnostic:f2579b060a99952b435d008bcfcf03b5cde5977c736a2577d594572c05f749a2
- New plain:e74820d16cb56d7ea808e2434d6418cccc2a70491d40d33c9914568bfc0bce17
- New diagnostic:55737929dbec28fce266f4d1a64fb58a7cdf89dde0cc15c5065f1575abdfb54f

Old binaries were reused from earlier transactions only after verifying the
complete1148-file baseline manifest and their saved ELF hashes. This avoids
rebuilding identical frozen old code. The earlier host-netns setup failures
never count as old negatives: they showed missing fragmented UDP delivery and
failed before discovery4096. The independent netns result isolates that
environmental difference without changing production semantics.

The transaction restored every original owned source byte and all1148 tracked
source/test hashes, restored the original CMake hook, rebuilt arena and verified
the exact original ELF SHA256
`4cf852610eaa366ac5ed64907645f0d39f3e93d1b06a1f1404e455b05c8f170b`.
No PMTU_CC_ string remained. Phase2 was not retained automatically. The
coordinator may apply the two final patches after review.

Final patch hashes:

- Production +8unit regressions,6files:
  `13a0c2b80ee44bca10f229878f87ddd0c3cdfaa3378f4dfc0a4df0ef5e37d315`.
- Permanent plain connection regression, CMake+3new fixture files:
  `6d67e7f86ac765faa36853bf2b59b41fb770fd90d85f4d95d3182a0573de3070`.

This curated archive contains only patches, metadata, build/test logs, source
hashes, syscall traces and necessary decrypted frame ledgers. It deliberately
excludes certificates, private keys, TLS keylogs and executable/archive binaries.
Full private validation records remain in the external temporary directory.

Coordinator integration after the transaction: the production phase2 increment
and permanent fixture were applied. The coordinator normalized the three new
fixture files to LF and removed the candidate target's redundant explicit
`cnetmod_enable_release_codegen` call because the global all-target loop already
applies it. Thus the candidate-patch SHA above identifies the reviewed external
artifact, not a claim that the final working-tree test diff has the same bytes.
The coordinator separately added two opt-in arena environment diagnostics while
retaining defaultMDS1472; those changes were outside this comparison.
