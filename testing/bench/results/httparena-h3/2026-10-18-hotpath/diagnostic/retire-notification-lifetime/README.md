# Retire-credit notification ownership regression

The normal `io_context::post` callback posted by `quic_connection::retire_stream`
did not release its heap-owned `std::function<void()>`. `callback_cleanup` is the
**never-dispatched** cleanup path; normal dispatch does not invoke it. The
function retained the copied deadline observer and its captures. The correction
adopts the function in a `unique_ptr` inside the normal callback before invoking
it. The discarded-post cleanup remains unchanged.

This directory records a real old-failing/new-passing retire regression, a
separate post cleanup contract test, the old cold heap diagnosis, and a new
uninstrumented 60-round correctness/retention run. No GSO change was included.

## Frozen identities

| Artifact | SHA256 |
| --- | --- |
| Old native ELF | `05fe27108dd833f86493aa2d00eefe7efef9df47805be198ef3eacd09042c479` |
| Corrected native ELF | `f9718cf6d075e99e13c54055ce31568ed925cc74e2aaafb41a9519e268f376a8` |
| Corrected `quic_connection.cpp` | `eed001f58285451ae216f67aaa687787fb327643e1240fe204af66a01b09afef` |
| Old-runtime/new-fixture ELF | `3224dac156013e41c52f93a860909f0de304f4bbbe4ee8231e71c2e45f0a809a` |
| Shared old/new fixture source | `ee5366e38fbf8b3d51229f0455a1853ffb52ef2255ead0d9e7689bb1484fe651` |

`corrected-source-sha256.json` records the production and permanent regression
sources. `artifact-sha256.json` hashes the archived evidence. Large ELF/archive
files remain outside this repository; their identities and the old link input
proof are retained here. This archive does not replace the final full Linux and
Windows test results maintained in the parent report.

## Real retire regression

The permanent `quic_retire_stream_server` fixture attaches a sentinel-owning
deadline observer through the public connection API. The client opens stream0,
receives the response, and verifies the request FIN was acknowledged. The
fixture waits for the response ACK and cleared PTO state, then an external file
trigger explicitly calls `retire_stream(0)` while the proxy blocks all further
client ingress. Existing assertions still require a real MAX_STREAMS1→2 update
before the old idle wakeup. The observer lifetime report is additional evidence,
not a replacement for this real protocol path.

| Runtime | Process result | Capture count before → after dispatch | Expired after observer removal |
| --- | --- | --- | --- |
| Old frozen runtime | exit1, expected assertion failure | 1 → 2 | false |
| Corrected runtime | exit0 | 1 → 1 | true |

Both runs observed the actual request-FIN ACK and MAX_STREAMS update; the old
failure was not a handshake, timeout, proxy, or fixture startup failure. The new
credit update arrived in approximately6.05ms, with zero client datagrams
forwarded after the gate. See the complete old-negative and new-positive JSON.

The old fixture was compiled with matching module interfaces and linked against
the immutable pre-fix QUIC archive, not a rebuilt positive archive. The exact
QUIC archive argument was replaced once; mold's actual input trace and archive
member SHA identify the linked `quic_connection.cpp.o`. ThinLTO's output map is
not treated as input provenance. Input hashes were unchanged across compilation
and linking. See `old-negative-build/` and `cnetmod-quic-notification-build.py`.

New CTest: **`quic_retire_stream_notification_lifetime`**. Its invocation is the
existing Python fixture with `--scenario notification-lifetime`. The separate
`test_quic_stream` case
`posted_resource_cleanup_separates_dispatch_from_discard` exercises the public
post API: normal dispatch destroys the resource once without discarded cleanup;
destroying the context before dispatch invokes cleanup once. This second case
is an API-contract test, not a claim of a real QUIC retire/discard integration
test. All17 `test_quic_stream` cases passed after the correction.

## Old cold heap evidence and rejected node-cache hypothesis

`old-05fe-cold60/` is the completed frozen05fe diagnostic:60 serial rounds of
200,000 requests,64 connections/64 concurrent streams/64 load-generator threads,
32 workers,1500ms connection idle and2s between rounds. All12M requests passed.
Five cold GDB snapshots read existing memory only: no inferior calls, target
writes, forced collection, hot counters, or page scans outside bounded owned
lists.941 source/layout inputs and the ELF were unchanged.

| Cold point | RSS bytes | Owned used upper bytes | 48B-class blocks | 80B-class blocks |
| --- | ---: | ---: | ---: | ---: |
| Initial | 48,431,104 | 3,207,632 | 289 | 1 |
| Round1 +2s | 177,692,672 | 4,271,616 | 8,284 | 3,969 |
| Round30 +2s | 234,180,608 | 25,059,008 | 238,478 | 119,041 |
| Round60 +2s | 264,314,880 | 46,124,256 | 476,556 | 238,081 |
| Round60 +37s | 264,314,880 | 46,048,288 | 476,556 | 238,081 |

With3125 requests per connection and the configured credit threshold50,
there are62 retire-credit notifications per connection. The observed80B-class
increase is exactly `60 × 64 × 62 = 238,080` blocks. The48B-class increase is
approximately twice this count. The true callback lifetime negative control
identifies an ownership defect consistent with that slope; the histogram alone
would not identify allocation types.

Cold connections, sessions, published/generation map sizes, and timer heap sizes
were zero. Timer vector high-water capacity was524,288 bytes at round60.
Reset-route entries expired to zero after37s. Small UDP pool cached56 blocks;
the large pool was not constructed. These cannot explain the linear used-block
increase. Owned-empty capacity was116,360,320 bytes after37s; **capacity is not
RSS**. Owned used is an upper bound including deferred thread frees. Abandoned
page maintained counts were zero with complete traversal, but abandoned pages
were not themselves enumerated. See snapshot scope/limits in the archived helper.

The72B loss-detector map node initially matched the80B class but was not enough
to assign blame. Offline DWARF identifies loss nodes72B and recovery sent nodes
216B. Disassembly of the actual05fe loss-detector destructor showed three
64-entry loops freeing non-null cached nodes with sized delete72. An external
allocation-balance probe then tested detector/recovery separately with1/2/64/128
packets,32 scoped owners per case, full send→ACK→destruction. All8 cases passed:
no outstanding tracked blocks/bytes, overflow, or unknown frees. Scalar/array,
aligned, sized/unsized new/delete wrappers actually captured the expected72B
and216B allocations, so passing was not inferred from an unused wrapper.
The probe output was observed in the execution tool transcript; this archive
retains its source, commands, link trace, immutable input hashes, and ELF SHA
(`7c60cd239c2f5d743204d1eb376eee52db5fe32f0259c9ce0d3c0fbbc5833128`),
not an invented raw stdout file. Copy/move/discard cache variants were not tested
after the real callback defect was identified.

## Corrected uninstrumented60 rounds

`new-f971-native60/summary.json` has `valid:true`: all60 rounds each completed
200,000 started/done/succeeded/2xx requests with zero failed/errored/timeout,
for12,000,000 requests total. Each raw h2load log is retained. The server stderr
log is empty. No debugger, collector, or hot-path diagnostic counters were used.

| Point | RSS bytes | Private bytes |
| --- | ---: | ---: |
| Initial | 53,133,312 | 50,733,056 |
| Round1 +2s | 179,572,736 | 177,127,424 |
| Round30 +2s | 210,919,424 | 208,474,112 |
| Round60 +2s | 216,453,120 | 214,007,808 |
| Round60 +37s | 216,322,048 | 213,876,736 |

Round30–60 RSS ranged209,653,760–224,841,728 bytes, rather than the old linear
used-block increase. This supports improved retention behavior; it does not
prove all live allocations returned to baseline. That60-round run has no cold
allocation census. The old run included five debugger stops, so these runs are
not a controlled throughput comparison. The separate matching-f97112-round
cold census below directly checks the48B/80B slope.

The actual successful command was:

```text
wsl -d archlinux -u root -- unshare --mount bash /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-isolated.sh python /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-final-retention.py f9718cf6d075e99e13c54055ce31568ed925cc74e2aaafb41a9519e268f376a8
```

It preserved the established IPv4-first private `gai.conf` wrapper and
nonce-owned isolated network namespace, then stopped its own server and
namespace on completion. Output originated at
`/var/tmp/cnetmod-h3-final-retention-1792325783`.

## Corrected12-round cold census

`new-f971-cold12/` is a second real run, with five bounded read-only GDB
snapshots: initial, rounds1/6/12 after2s, and round12 after37s. All2,400,000
requests passed. `summary.json` records `valid:true` and unchanged inputs.
The frozen ELF was copied from the successfulf97160-round output, with941
matching source files copied outside the mutable checkout. Relative to the old
05fe source ledger, only `quic_connection.cpp` changed, to the approved RAII fix
SHA above; the other940 inputs matched. `layout.json` preserves both original
and copied source identities. Offline GDB had inferior PID0 and checked current
DWARF types, fields, atomic widths, and link symbols. The actual snapshot helper
re-resolved those symbols after ASLR; no old numeric addresses were reused.

| Cold point | RSS bytes | Owned used upper bytes | Owned-empty reserved capacity bytes | 48B-class blocks | 80B-class blocks |
| --- | ---: | ---: | ---: | ---: | ---: |
| Initial | 48,394,240 | 3,207,632 | 0 | 289 | 1 |
| Round1 +2s | 177,700,864 | 3,601,072 | 100,880,256 | 351 | 1 |
| Round6 +2s | 202,809,344 | 3,838,576 | 113,528,288 | 394 | 1 |
| Round12 +2s | 211,750,912 | 3,931,712 | 114,589,120 | 411 | 1 |
| Round12 +37s | 211,750,912 | 3,858,112 | 120,148,800 | 411 | 1 |

The former80B growth of3968 blocks per round is absent: the absolute count
remained1 throughout. The48B class increased by122 total blocks across12 rounds,
rather than approximately7936 each round. This closes the measured leak's
counter-evidence together with the real old-failing/new-passing observer test.
It does not assert zero growth in every object class or a process-wide exact
live-byte count. The old census sampled rounds1/30/60, so only initial/round1
are exactly matching round checkpoints; do not invent old round6/12 samples.

After37s, active connections, connection/session maps, published route
generations, timer generations, timer heap sizes, reset routes, and pending
route expirations were all zero. Timer vectors retained434,176 bytes of
capacity. The small UDP pool cached64×2048 bytes of payload, in addition to its
fixed2MiB pointer queue; the large pool was never constructed. Maintained
abandoned page counts were zero and the declared owned-page traversal completed
at every checkpoint. The48B and8B classes each had one page with deferred
thread frees, which remain included in the upper-bound counts.

Final mimalloc committed was213,778,432 bytes, versus owned used upper3,858,112
bytes and owned-empty reserved block capacity120,148,800 bytes. The large gap
between residency and live objects therefore persists after repairing the
linear leak. Empty capacity, committed accounting, deferred frees, metadata,
and RSS describe different quantities; none may be substituted for another.
These measurements support allocator retention as a substantial remaining
factor, not a claim that all212MB of RSS is empty pages. No comparison with
another library's private allocator accounting is made.

All five debugger stops took0.67–0.79s, with `inferior_function_calls:false`
and `inferior_writes:false`. No `mi_collect`, target allocation, or inferior
function was invoked. The runner validated nonce/PID identity for cleanup and
terminated its own server and isolated namespace. Raw process maps, smaps,
GDB output, bounded allocator/owner counts, commands, and input hashes are
retained. `cold-count-summary.json` collects the absolute counts without
replacing the raw evidence. Source output:
`/var/tmp/cnetmod-h3-cold12-f971-9cc730bcb3864c21a6f628a8bf50f9fa`.
