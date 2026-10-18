# Explicit GSO candidate: system allocator validation and short A/B

Status: implemented candidate, default off. Root retains it temporarily for same-ELF off/on CPU profiles before the final retention/withdrawal decision. Static has consistent short-run gains; baseline does not. No official selector/default or PMTU ceiling was changed, and no performance ranking is claimed.

The applied eight-file patch SHA is0992b679da0d687a4f6c7480e92b90655432c08d1fcc30925caecb79fc257a63. `validation/freeze.json` records actual CRLF source hashes, preserved PMTU phase1 source hashes and the protected interop JSON digest. The source snapshot records actual HEAD/dirty diff and1684 inputs; the candidate is not identified only by HEAD.

## Correctness and allocator boundary

Initial Linux build245/245 exit0,22/22 directed CTest passed,0skipped in88.09s. This initial step still used mimalloc and is correctness evidence only. User then required system allocation: configured the actual CNETMOD_USE_MIMALLOC option OFF in this build, leaving repository default ON untouched. Necessary targets rebuilt successfully, then12/12 directed CTest passed under system allocation. Fault/negative checks and every A/B round below used this system build.

`validation/system-allocator/` contains before/after allocator configuration evidence, exact link command, nm/ldd/readelf, archive members and actual LD_DEBUG bindings. The link has no mimalloc object/dependency; malloc imports bind to libc and operator new imports to the system C++ library; LD_PRELOAD and LD_AUDIT were absent. Arena usage validation exits2 without starting a listener, and LD_BIND_NOW resolved imports. The first proof script expected exit1 incorrectly; its initial trace is retained, then the check was corrected to match source return2. This was a diagnostic expectation error, not a production failure.

System arena ELF SHA256:2cb22a32818646fbf63575370adf6a0c48b3ba5df4bbbc3d3002ab2838003e0a. No ELF/PCM/static archive is copied here; frozen paths are in external-inputs.json.

Eight filtered fault/observation runs passed: real observe,EMSGSIZE,ENOPROTOOPT,EOPNOTSUPP,EINVAL,EIO,EAGAIN,and real positive prefix. The observe marker proves stride1472/16originaliov/23552bytes and real sendmmsg result1. Prefix proves3originaliov/4416bytes,at least2kernel messages and real result1; the exact receiver packet sequence plus distinct final sentinel proves accepted prefix was not resent. Negative errno modes intentionally simulate zero-progress rejection and do not claim a kernel-generated error. Every raw stdout/stderr is retained.

Two external negative controls returned expected exit1: wrong raw-kernel-message prefix count and bypassed native UDP_CORK/UDP_SEGMENT overrides. Normal candidate tests pass. Negative builds used the same PCMs/archives/layout and replaced only one backend object; before/after input integrity is unchanged. The first external link inherited a dependency-file path into a read-only build; its failed command/log remain, and only that output path was redirected externally before retry.

Not covered: a real isolated MTU1500 route causing per-segment EMSGSIZE. Group-only injected EMSGSIZE fallback with valid1472 originals was covered; the real kernel route case remains future work. GSO stays experimental/default off. These checks do not substitute for Windows final builds, platform peers or long-retention runs.

## Same-binary off/on short A/B

12/12 rounds valid,zero failed/errored/timeout/non2xx. One binary,system allocator; each profile3pairs ordered off/on,on/off,off/on. Fresh32worker server,64connections/64streams/64threads,client CPUs16–31,5s,unchanged URI/encoding/gai namespace,32binds+one readiness request+2s. Source/binary hashes checked every round and finally; created server/container cleaned up. Header-only2xx−done recorded separately; completed throughput always uses done/elapsed. Raw printed rates remain recorded separately.

|Profile|Paired completed-rate change(on/off)|Paired CPU/completed change|Median paired changes|
|---|---|---|---|
|baseline|+22.52%,−6.18%,−9.10%|−14.90%,+5.98%,+3.61%|−6.18% rate,+3.61% CPU|
|static|+48.68%,+49.04%,+37.69%|−32.26%,−32.33%,−27.83%|+48.68% rate,−32.26% CPU|

Static off→on completed/elapsed:293405→436241,334622→498705,332097→457256RPS. CPU/complete55.151→37.361,49.885→33.759,50.255→36.268µs. p9933.38→23.45,29.62→21.64,28.28→22.04ms. Static live RSS deltas+952,+140,+1408KiB; these fresh-server post-load snapshots cannot establish idle recovery or long-term retention.

Baseline did not show stable benefit and should not be represented by its best pair. Static's consistent throughput/CPU direction supports retaining an explicit experiment, not enabling it by default. This experiment aggregates submission while keeping1472 PMTU and per-packet encryption/recovery; client datagram counters are not syscall counts or proof of wire packet reduction. Same-binary CPU profiles are assigned to root after this window; no profile ran concurrently here.

AB summary SHA256:1c8d7d78a69caaf273dc8c576b841ab91b3afd6628058bbf1459825e4a10e7ed. Full paired CPU/rate/p99/RSS and printed/completed distinction are in native-ab/analysis.json. Candidate sources remained frozen and the exclusive execution window was released immediately after analysis.
