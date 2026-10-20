# agensio reference, shared-prefix comparison and pending-PMTU regression

Reference downloaded from https://github.com/YiannisBourkelis/agensio at
v0.1.0-alpha.23 / e60c61c7643e47c046fbb5136f8b46f7f7f9289b.
The downloaded 130 src files and CMakeLists match the retained builder image;
the builder and measured runtime image contain the same ELF (8c0da17c...).
The source archive is external: C:/Users/25007/.codex/tmp/agensio-e60c61c7-reference.zip,
SHA256 4b3ce0128ef07ac466ef7c59ecaba4042da41ceec276e877fd9386d3ac720697.

## Measured reference and bounded comparison

The original static URI set, encoding, 32 server workers (32 actual socket FDs),
64 connections/streams/client threads, client CPUs16-31 and 5-second load were
retained. No agensio MTU/GSO configuration was changed. One unprofiled diagnostic
completed 6,256,445 requests in 5.67 seconds: 1,103,429 completed requests/second,
11.417 CPU microseconds/completion, p99 11.76ms, zero errors/timeouts.
This is one observation, not a three-round acceptance comparison.

A separate DWARF99Hz diagnostic had 6455 samples and zero lost samples; its
throughput is not the unprofiled result. The exact retained libc instruction
0x162b87 is rep movsb; all 519 samples there unwind to build_packet+0x3247.
Raw perf data, mapped binaries and full stacks remain external under
/var/tmp/agensio-reference-observation-dd7463c36c79453ca4d8fd9099ae2cf9.
The archive contains commands, identities, reports and bounded leaf analysis.

The separate 54-round guard/mixed/owned-fast experiment was completed before
pending-PMTU integration. All rounds were valid and error-free. Owned-fast
static4K median was 711,010 completed/s versus guard655,684; static65K615,845
versus366,132. Baseline paired results fluctuate; no stable baseline improvement
is claimed. Actual packer disassembly shows owned-fast lost append_to inlining,
so a standalone helper's tail jump is not proof of caller optimization.
These six-round medians and the one agensio observation are not a paired test.

The reference reviews identify direct final-packet construction/in-place AEAD,
worker-wide batching and compact recovery records as concrete implementation
differences. cnetmod already uses scatter/gather GSO: there is no extra whole-packet
copy into a GSO slab. Agensio's EAGAIN dropping and PMTU flight accounting differ;
they must not be copied as silent semantic changes. Source differences alone do
not establish the CPU/RPS cause. The 1.1M cnetmod goal remains unmet.

## Accepted source correctness identity

The current source adds mixed owned-prefix/shared-body STREAM representation,
with codec/owner/PTO/loss/discard coverage, and two pending-PMTU fixes:
ordinary probes wait for full congestion credit without blocking validated-size
work, and a valid application ACK requests one writer retry for a pending target.
Genuine PTO/CLOSE exceptions and the final packet admission gate remain intact.

Linux43/43 passed in109.21s in a private mount+network namespace; Windows44/44
passed in116.39s. No skips. Windows27 PE/import/link proofs and Linux seven-backing
loader/link proofs confirm mimallocOFF. Accepted Linux ELF:
880eb2a9886dd956deb325138b493162cb7354db90e64e7f19b2c5cf441ca4b4.
Windows arena ELF-equivalent PE SHA:
5c1cf2c58c5196945ef9cb0d125088522bcab901f03c87f93add72997babfae2.
Source metadata truthfully records HEAD61ff3144 plus dirty inputs before this
commit; it was not rewritten to manufacture a clean-build identity.

The independent pending wire matrix uses old/deferral-only/full and diagnostic/
normal identities:12 expected outcomes, including deliberate old-version negative
controls. It predates the owned-fast append layout; final combined production is
covered by Linux43 and Windows44. Earlier setup/driver attempts remain external
under /var/tmp/cnetmod-pmtu-pending-mixed-20261019/wire-recovered.

The newly combined PMTU baseline has not yet received a formal performance rerun
or the final180-round retention test. Independent PTO-role separation, in-place
AEAD and early congestion-rejection diagnostics remain prepared experiments, not
accepted fixes or performance claims. Protected h3-interop-results.json remains
unchanged and untracked.
