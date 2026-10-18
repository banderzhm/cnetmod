# Prepared larger-MTU CPU diagnostic (not run)

Use the final guard/pending-scheduling repair freeze, its exact ELF SHA, matching source snapshot and system-allocator proof. Do not reuse the rejected RX mutable build or older fc6e50 snapshot. The coordinator chooses two configurations; there is no implicit sweep.

```
wsl -d archlinux -u root -- unshare --mount bash /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-isolated.sh python /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-large-mtu-perf-prepared/large_mtu_perf.py --binary FROZEN_ELF --binary-sha EXACT_SHA --source-snapshot MATCHING_SNAPSHOT --allocator-proof MATCHING_PROOF --gso on --path-mtu 4096 --receive-cap 8192
```

The alternatives are send16384/32768/65507 with receive65507. Run a second selected configuration serially by changing both explicit arguments. Same32workers/64clients/64streams/64threads, original static URI and encoding, client CPU16-31,5seconds;99Hz cpu-clock, frame-pointer stacks and CPU IDs. Profiles are diagnostics, not official rankings. No packet size is inferred from the configured ceiling.

The runner retains process CPU/completed, per-TID user/system ticks, affinity and last-CPU snapshots, host/per-CPU tick deltas, exact source/binary/proof/loadgen/gai/fixture hashes, raw h2load counts, perf reports/header and original stop code. Host busy includes clients and unrelated work. Last processor is not residency; TID CPU time cannot identify ownership, queue delay or why a thread did not run. Lost samples must be explicitly0; unresolved symbols remain an offline-analysis limitation.

Private namespace/server/profiler are uniquely created; server and profiler have independent owned process groups. Finally terminates/waits the owned server and removes only the unique Docker anchor, retaining errors rather than claiming pass. Source/hash gates repeat after load. Timed validation requires done==succeeded>0,2xx>=done, non2xx/failed/errored/timeout0; headers are not completed throughput. No compilation/load/perf was executed during preparation.
