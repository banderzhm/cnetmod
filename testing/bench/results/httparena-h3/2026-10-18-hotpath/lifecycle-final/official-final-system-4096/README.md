# Final official HTTP/3 run: system allocator, send 4096 / receive 8192 / GSO 1

All six framework/profile entries passed three serial rounds (18/18). All raw h2load exits were zero; failed, errored, timeout and non-2xx counts were zero. Four official validation entries passed before timing. The final runner exited zero, verified all 2,209 source hashes and frozen inputs unchanged, and removed all containers carrying its unique run label. This is a successful benchmark run, not proof that cnetmod matches agensio throughput.

The original official workload and best-of-three selection are unchanged: 5 seconds, 32 server workers, 64 connections, 64 maximum concurrent streams, 64 h2load threads, client CPUs 16–31, and the original baseline URI/static request list and Accept-Encoding. The same cnetmod configuration serves both profiles. Framework/profile order is the order in the table below. Peer images are pinned in summary.json. These are local Linux/WSL isolated-loopback results.

## All rounds and official selection

Rates below are requests/second, rounded to the nearest integer except the official selected value, which retains the original integer truncation. `complete` is successful completed requests / printed elapsed time. Official selection uses HTTP 2xx count / elapsed time; a 2xx header can be counted before the response body completes. The raw h2load RPS divides by its measurement interval and differs from both rates; all three metrics, counts and elapsed times are retained in all-rounds.csv and the full raw logs. The official ranking was not changed to select by completion rate.

| Framework | Profile | Complete round 1 / 2 / 3 | Official 2xx round 1 / 2 / 3 | Official best | 2xx-minus-complete counts 1 / 2 / 3 |
|---|---|---:|---:|---:|---:|
| cnetmod-official-optimized | baseline-h3 | 2,837,194 / 2,864,516 / 2,817,714 | 2,837,194 / 2,864,516 / 2,817,714 | 2,864,516 | 0 / 0 / 0 |
| cnetmod-official-static | static-h3 | 628,119 / 608,958 / 619,836 | 628,129 / 608,965 / 619,845 | 628,129 | 57 / 40 / 50 |
| agensio-official-h3 | baseline-h3 | 7,042,284 / 7,079,203 / 6,946,345 | 7,042,284 / 7,079,203 / 6,946,345 | 7,079,202 | 0 / 0 / 0 |
| agensio-official-h3 | static-h3 | 1,046,255 / 1,032,380 / 1,046,174 | 1,046,455 / 1,032,571 / 1,046,365 | 1,046,455 | 1133 / 1082 / 1085 |
| genhttp-ioxide-official-h3 | baseline-h3 | 1,274,978 / 1,549,396 / 1,545,038 | 1,275,010 / 1,549,413 / 1,545,057 | 1,549,413 | 185 / 95 / 109 |
| genhttp-ioxide-official-h3 | static-h3 | 193,274 / 217,288 / 215,052 | 193,812 / 217,840 / 215,671 | 217,840 | 3077 / 3129 / 3505 |

cnetmod remains behind agensio in both profiles:

- baseline-h3: official best 2,864,516 versus 7,079,202; cnetmod is 59.54% lower, agensio is 2.471× cnetmod. Separately, the best complete rate is 2,864,516 versus 7,079,203 (59.54% lower).
- static-h3: official best 628,129 versus 1,046,455; cnetmod is 39.98% lower, agensio is 1.666× cnetmod. Separately, the best complete rate is 628,119 versus 1,046,255 (39.96% lower).

## Frozen inputs and reproduction

- Linux arena ELF SHA256: `5bf46aeeee227934742969972dfa3e4705d5fb87e66289c9109f41d89f353470`.
- Runner SHA256: `0a75c277297e573bd375f6defa141898b066b11fba3f167c347f414d5ccc7a17`.
- Official benchmark source revision: `8977be2721a5aba7b320345cddfcd6f6b654e698`.
- h2load SHA256: `9a35cefa52e809a32f1898d65575a2cbbf54555031604830b852e2f6b5a4484f`.
- `records/system-allocator/` retains OFF cache/link/ELF binding evidence. `records/inputs-before.json` and `inputs-after.json` cover sources, frozen binary, runtime/proof dependencies, driver and request data. `source-metadata.json` and `source.diff` describe the dirty source snapshot; staging changes in the Git index did not alter those bytes.
- The Dockerfiles explicitly set `CNETMOD_QUIC_MAX_PATH_MTU=4096`, `CNETMOD_QUIC_MAX_UDP_PAYLOAD_SIZE=8192`, `CNETMOD_QUIC_GSO=1`; SSLKEYLOGFILE, LD_PRELOAD and LD_AUDIT were removed. Image inspect evidence is in summary.json.

Command (the original external absolute paths must exist):

```text
wsl -d archlinux -u root -- unshare --mount bash /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-isolated.sh python /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-final-system-official.py --execute --path-mtu 4096 --gso 1
```

Run root: `/var/tmp/cnetmod-h3-final-system-official-mtu4096-gso1-4c178de9d76f4c63adb15a4ebe898833`. Absolute paths are provenance, not a portable single-command package. A fresh machine must supply matching build/runtime dependencies, pinned peer images, the original official dataset and fresh local TLS certificates. This archive deliberately excludes executables, libraries, module caches, certificates and private keys.

`prepared-scripts/` retains helper preparation output before task-specific changes; `staged-scripts/` retains executed scripts. The three patch files preserve resource-name isolation, explicit child exit/argv recording, and the common isolated loopback. No global Docker prune or unowned process cleanup ran. The process-group cleanup records refer only to inherited validator watchdogs with the unique nonce in their environment; all recorded groups were empty afterward.

Official selected CPU/memory values remain unmodified in results JSON. `records/resource-samples.jsonl` is a supplemental once-per-second sample of the owned server process; it does not replace official measurements, establish allocator live memory, or provide a long-idle retention result.

## Invalid first attempt retained separately

`invalid-first-pipe/` is excluded from the 18 successful rounds. It failed in the external capture tool during the first validator: the validator parent ended but its sleep-800 watchdog retained the stdout PIPE, causing communicate() timeout. No timed profile ran. Its summary remains valid=false/exit1, and the raw validator output was not persisted by that tool version; no validator pass is claimed for it. The retained cleanup JSON shows only the verified task-owned process group was terminated and no task-labelled container remained. The authorized fix writes long-command stdout/stderr directly to the raw log file, waits on the parent, and retires only its verified unique process group. A new unique run then performed all validators and all 18 timed rounds successfully.

## Archive safety and limits

Only UTF-8 text evidence is copied. The archiver rejects binary magic, NUL bytes, PEM blocks and TLS key-log records. No ELF/PE/archive/PCM/private key/certificate is included. SHA256SUMS.json covers every archived file except itself. Raw source and tool text include local paths for provenance. The successful one-configuration run does not isolate the benefit of individual code changes, provide randomized cross-run confidence intervals, or establish parity with agensio.
