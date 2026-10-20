# Final native 60-round retention

Binary SHA256 `05fe27108dd833f86493aa2d00eefe7efef9df47805be198ef3eacd09042c479`.

60 serial static rounds of 200,000 requests: 12,000,000 completed/succeeded/2xx, zero failed/errored/timeouts. The runner requires all four request totals to equal 200,000 on every round. Original request list, Accept-Encoding, 32 server workers, 64 client connections/streams/threads and client CPU affinity 16-31 are preserved. Each round is separated by 2 seconds; the final additional 35 seconds gives 37 seconds without ingress.

**Correctness passed; memory is not accepted.** RSS starts at 53,137,408 bytes, reaches 267,735,040 after round 60, and remains 267,325,440 after final idle (private 264,900,608). Later rounds still grow; a plateau is not established. This run contains no GDB attachment, in-process observer or forced allocator collection. Counts and RSS do not establish whether remaining retention is live ownership or allocator pages. Separate diagnostics are required.

This is a native correctness/retention run, not the official framework throughput comparison. The executable stays outside the repository; hashes and complete logs are archived.
