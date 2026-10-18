# Extended final system-allocator180round retention

All180 original fixed-count h2load logs independently revalidate200,000 total/started/done/succeeded/2xx each and zero failed/errored/timeout/non2xx:36,000,000 successful requests. The full original console contains final-idle-37s and RESULTS, matching summary data. The coordinator confirms tool session40035 exit0; server wait and checked Docker cleanup precede RESULTS. This is a finite correctness/memory observation, not a throughput ranking or proof of unlimited leak freedom.

Settings remain frozen ELF5bf46a... with system allocator, GSO1, send discovery ceiling4096, receive capacity8192,32serverworkers,64clients/streams/threads, client CPU16–31 and the existing isolated loopback/gai setup. No in-process observer, keepalive or forced allocator trim is introduced in the final idle interval.

| Sample | RSS bytes | Private bytes |
|---|---:|---:|
| initial | 27,099,136 | 24,657,920 |
| round-1-idle-2s | 79,859,712 | 77,348,864 |
| round-30-idle-2s | 124,227,584 | 121,716,736 |
| round-60-idle-2s | 131,051,520 | 128,540,672 |
| round-90-idle-2s | 138,362,880 | 135,852,032 |
| round-120-idle-2s | 147,673,088 | 145,162,240 |
| round-150-idle-2s | 146,640,896 | 144,130,048 |
| round-180-idle-2s | 150,642,688 | 148,131,840 |
| final-idle-37s | 150,642,688 | 148,131,840 |

The61samples at rounds120–180 range from145,080,320RSS bytes (round125) to150,642,688 (round180). Round120→150 changes-1,032,192bytes; round150→180 changes+4,001,792bytes. The overall endpoint change is+2,969,600bytes (+2.01%). There are26 adjacent increases,32 decreases and2 unchanged pairs. This late finite window shows a much narrower fluctuating footprint than startup, but its bounds are observations, not a stationary-process or indefinite-boundedness theorem.

The final labeled37s-idle sample is RSS150,642,688/private148,131,840bytes, unchanged from round180. Recorded round180→final sample time is40.0165s: the label reflects2s round cooldown plus35s sleep, while final source/binary verification adds time before sampling. RSS/private alone cannot identify live allocations, prove absence of all leaks or guarantee return to startup memory.

The earlier60round run is a separate fresh-process observation and is not concatenated into this180round series. No larger lifetime conclusion is derived from the two finite runs. Existing ownership tests and historical allocator-bin diagnosis remain separate evidence under their own allocator/snapshot identities.

Source commit36568ef8cb14a1ddba6680a53d5fc67c5692140e was created after these runs. The coordinator independently rechecked all2209 recorded source hashes with zero differences and the same5bf ELF. Original d0d9+dirtydiff metadata is preserved; source-commit-mapping.json records the later mapping without rewriting history.

Raw external directory: `/var/tmp/cnetmod-h3-system-retention-03b6c34bf7154d55ba0c935e0ba9a5d8`. This archive contains180h2load logs, original full console, zero-byte server log, summary, source metadata/dirtydiff, allocator proof, extended runner, offline validator and all182memory samples. ELF/PCM/keys/perfdata are excluded. Descriptive OLS statistics are not forecasts. Final CPU diagnostics are under diagnostic/final-system-mtu-perf; the official comparison remains below agensio, and memory validation does not change that result.
