# Final system-allocator60round static retention

The frozen5bf46a... candidate completes all60 serial200,000-request static rounds:12,000,000 done/succeeded/2xx and zero failed, errored, timed-out or non2xx requests. Independent offline parsing rechecks every original h2load log. This is correctness/retention evidence, not a throughput ranking.

Settings: system allocator (`CNETMOD_USE_MIMALLOC=OFF` proof), GSO1, ACK-validated send discovery ceiling4096, fixed receive capacity8192,32serverworkers and64clients/streams/threads, client CPU16–31, isolated Docker-anchor loopback with the existing gai wrapper. Production defaults are not changed by this run. Source metadata/dirtydiff and allocator proof are retained and match their recorded hashes.

| Sample | RSS bytes | Private bytes |
|---|---:|---:|
| initial | 27,131,904 | 24,690,688 |
| round-1-idle-2s | 77,897,728 | 75,378,688 |
| round-30-idle-2s | 128,802,816 | 126,283,776 |
| round-40-idle-2s | 126,066,688 | 123,547,648 |
| round-50-idle-2s | 131,231,744 | 128,712,704 |
| round-60-idle-2s | 138,674,176 | 136,155,136 |
| final-idle-37s | 138,039,296 | 135,520,256 |

Rounds30–60 (31samples) have RSS minimum125,833,216bytes at round41 and maximum138,903,552bytes at round59. RSS changes by9,871,360bytes (7.66%) from round30 to round60, with19 adjacent increases and11 decreases. Later endpoints are higher: round40→50 rises5,165,056bytes and round50→60 rises7,442,432bytes. The finite trajectory fluctuates and retains an upward tendency; it must not be described as proven saturation or infinite long-run boundedness.

The final sample labeled final-idle-37s is RSS138,039,296/private135,520,256bytes, down634,880RSS bytes from the round60 sample but still above round30. The configured2s cooldown+35s final sleep is followed by source/binary hashing before sampling; recorded sample timestamps are42.335seconds apart. No forced allocator trim, inbound keepalive or in-process observer is added in that final interval.

RSS/private pages alone do not identify live ownership, distinguish allocator page retention from a leak, or show that memory returns to its startup level. Historical mimalloc ownership/bin results belong to another allocator/snapshot and are not interchangeable with this system-allocator series. A60round observation provides evidence over this workload/window only.

The coordinator reports tool session45922 ended with exit0 and RESULTS after the final sample. The runner's finally path waits for its server and removes its own Docker anchor before RESULTS; this establishes that cleanup path completed. No separate raw console file or additional process inventory exists. `coordinator-execution-note.json` records that provenance rather than inventing raw console output or summary cleanup fields.

External raw directory: `/var/tmp/cnetmod-h3-system-retention-6d27bba41cc64c7e8983ad48f9e6968f`. All60h2load logs, zero-byte server log, summary, source metadata/dirtydiff, allocator proof, runner and offline archive/analyzer are copied. The39MB ELF, any keys and unrelated payloads are excluded. Full62point samples and descriptive statistics are in memory-samples.csv and memory-analysis.json. The OLS slope is descriptive and must not be extrapolated as a future leak rate.
