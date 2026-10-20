# Deferred bulk plaintext experiment — REJECTED and reverted

**Decision: rejected.** The exact experimental patch was reversed and was verified to match HEAD ac465be6 and SHA50cd8c3b… before the next independent experiment began. This archive preserves the rejected experiment, not a retained production optimization. Base guard commit: ac465be68f2b89ec54df344dde3e33b3e7760e2f. The patch keeps ACK/PING and cold reserve behavior and defers only bulk frame/DATAGRAM copy and padding until existing whole-packet admission succeeds. No in-place seal, mixed-prefix change, credit fastpath, allocator option change or workload change is included.

## Identity and correctness

- Base production connection SHA: `50cd8c3ba2cc226c45e83a483ba4c04665e229b76aae69ebf4523170abb7f1c2`; candidate: `d2a8a62e278ea04b2ea2350c459dce595b409a17a8483ee94114b4e1e0fa731a`.
- Guard baseline ELF: `2fa51db8652cf8a0d166d79ab4d36241c9a5c789aa8ed4c0cb5e57eb56bb7d28`; candidate ELF: `729c62a8a3e54cbd619ccfd3d369dd9bbf5d752e14fc23d6de9256ec64332dd9`.
- Candidate Linux build succeeded; selected full Linux suite **41/41 PASS, zero skipped, 107.68 seconds**. Includes DATAGRAM admission/PTO, PMTU initial/interval deadline, large response after loss, priority/alias/credit unit/integration and lifetime regressions. Coordinator Windows candidate verification also passed **42/42, zero skipped, 111.86 seconds**, with **26 PE imports/link OFF checks**. Its complete text records are in `windows42/`; the archived native-binary hashes were checked against all26 frozen PE files without copying those files. Recorded Windows source hashes are identical before/after. Correctness passing does not override the rejected performance result.
- Real external fault injection: **6/6 PASS**, observer/private × AEAD/HP/AEAD-empty-error. Each fixture reports two response candidates, one sealed prefix, one actual backend failure, immediate nonzero terminal error and no discarded prefix delivered to the peer. Test execution exit0. Empty native error yields generic EIO; actual AEAD error is openssl; HP invalid key is generic EINVAL. Dedicated link proof verifies the unique external archive argument, actual trace member and replaced-member SHA. The initial historical object-count assertion failed before link; its failure/integrity records and narrowly corrected builder are retained under `fault/`. No production fix was needed for that tool failure.
- Filtered source manifests cover **1274** relevant source files, excluding historical results and protected interop JSON. The historical guard snapshot has an extra trailing blank line in `deadline_frame_parser.py`; the runner verifies its complete bytes equal the new parser after stripping only trailing CR/LF. Exact differences are recorded: this formatting-only Python input and the sole production change, connection.cpp. Historical baseline proof was not rewritten.
- OFF proof and all seven independent backing files were verified before/after freeze and A/B; ELF dynamic symbols/loader binding prove system malloc. Backing archives/ELFs remain external, not in this archive. Source, executable, libraries, fixture files and scripts passed pre/post hashes. The protected interop JSON remained SHA `ca41c4cb5adcc34ee16042e00ea9c6dfb248b9a17649995dbacfeb43b64d8b66` and was not staged by this agent.

## Static results: all twelve rounds

Original static URIs and encoding, 32 workers, 64 connections ×64 streams, 64 h2load threads, 5s duration, client CPUs16–31, GSO1, system allocator. Two configurations, three matched AB/BA/AB blocks each. Each timed round used a fresh server and the same one-request readiness check followed by 2s settling. All twelve rounds had failed/errored/timeout/non2xx=0; inputs remained unchanged, exit0, owned server/container cleanup succeeded. Raw h2load argv/logs, ready checks, server logs, before/after CPU/RSS/TID/host snapshots and summary are retained in `static-ab/`.

| send/receive ceilings | median complete/elapsed baseline → candidate | change of medians | median CPU us/complete | median p99 ms | median short-run RSS MiB |
|---|---:|---:|---:|---:|---:|
|4096/8192|612183 →602508|**−1.58%**|28.419 →28.648|16.44 →17.27|112.99 →113.96|
|65507/65507|358663 →394567|+10.01%|54.209 →47.360|37.02 →40.19|281.38 →276.71|

Paired completion changes for 4096: +0.48%, −2.89%, −0.30%; CPU changes −1.50%, +1.26%, +1.72%; paired p99 changes −1.68%, +5.05%, −2.16%. No stable 4K benefit is established.

Paired completion changes for 65507: +9.87%, +11.15%, +9.85%; CPU changes −10.92%, −15.23%, −12.29%. Paired p99 changes **−13.68%, +8.56%, +9.44%**: two of three tail-latency pairs regressed. Do not characterize this as an accepted overall improvement or claim target throughput was reached.

`all-rounds.csv` lists every round, including header-completion deltas and h2load's printed RPS. Headline throughput above is completed requests divided by actual elapsed time. The load generator's printed timed rate is different and is not substituted for completion rate; 2xx/header rate is recorded separately. RSS is sampled after each short round (65K candidate spans 261.31–300.48MiB); **this is not a retention experiment or evidence of a memory saving**. Whole-host busy spans25.88–27.63 CPU equivalents and idle2.17–3.12; it includes the pinned load generator and unrelated work. TID skew is an observation, not proof of spare capacity.

Configured ceilings are not evidence every application datagram reached the ceiling. No rejection counter was added, so this A/B cannot partition successful versus rejected plaintext-copy costs. A separate 4K baseline-H3 six-round follow-up completed; see below. It is not silently folded into these twelve static rounds. The candidate was rejected and the production change reverted after all18 performance rounds.

## Baseline-H3 follow-up: all six rounds

Same frozen2fa51db baseline versus729c62 candidate, 4096/8192, GSO1/system allocator; original `/baseline2?a=1&b=1`, 32workers/64c64m64t/5s/client16–31. Three AB/BA/AB pairs. Raw commands/logs, all CPU/RSS/host/TID snapshots, source/proof pre/post hashes and full summary are retained in `baseline-ab/`. All six rounds had zero failed/errored/timeout/non2xx, 2xx==done, inputs unchanged, exit0 and successful owned cleanup.

|pair|complete/elapsed baseline → candidate|throughput change|CPU us/complete baseline → candidate|p99 ms baseline → candidate|
|---|---:|---:|---:|---:|
|1|2981357 →2740153|−8.09%|6.579 →6.862|5.22 →5.78|
|2|2888373 →2781982|−3.68%|6.724 →6.823|5.96 →6.21|
|3|2865063 →2936434|+2.49%|6.675 →6.673|5.39 →6.21|

Medians: completed/elapsed **2888373 →2781982 (−3.68%)**, CPU **6.6748 →6.8227us/complete (+2.22%)**, p99 **5.39 →6.21ms (+15.21%)**. All three paired p99 changes regress: +10.73%, +4.19%, +15.21%. Short-run median RSS68.80 →71.84MiB remains an observation, not retention evidence. These small-response results and the static4K result prevent acceptance of a global second descriptor traversal merely in exchange for the static65K throughput gain.

`rollback.json` records the decision, exact reversed patch and restored source hash. No changes were committed or staged by this agent. At rollback time the mutable build directories still contained candidate executables even though production source was reverted; use the independently frozen guard baseline or rebuild before subsequent claims.

## Reproduction and external references

`candidate-proof/variants.json` identifies frozen ELF/proof/source roots; `static-ab/runner.py` is the exact executed runner and `static-ab/console.log` records its complete output and valid=True result. Execute only in an exclusive window through the private mount/gai wrapper shown in `scripts/`; never use a mutable current build as the baseline. The absolute `/var/tmp`, `/tmp`, `/mnt/c` and Windows paths are original environment/provenance references and can be remapped explicitly when reconstructing inputs. Missing external references on another host are not missing archive members. JSON files named `inputs-before`, `inputs-after`, `outputs`, proof files and source manifests contain hashes of those external inputs; the local `manifest.json` below hashes only actual archive members.

No ELF, object, archive, PCM, perf data, certificate, private key or keylog is committed here. Generated fixture certificate/key files were deliberately excluded. See `proposal/review.md` for migration invariants and the broader required matrix; that review's pending tests describe preparation-time scope and are not a claim that every proposed differential test has since run.
