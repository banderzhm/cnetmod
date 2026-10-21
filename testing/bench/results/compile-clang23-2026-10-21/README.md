# Clang 23 compilation investigation — 2026-10-21

Baseline: `5aa6efd6c09a483bf9a89ebab77495ef330dcc96`. The candidate changes
four runtime source files; the tests and analyzer are additional validation tools.
Candidate source/test/analyzer commit: `6cf35fbda6c9028a72eda97ba6869e9aa1e0d9c4`.

## Scope and measurement limits

Clang 23.1.3, libc++, CMake 4.4.3, Ninja 1.13.2, Linux under WSL2 on the same
i9-14900K host. Sources and build outputs reside in the native Linux filesystem.
Release uses `-O3 -DNDEBUG`, portable CPU settings, mimalloc, no ThinLTO, and
**at most four build jobs**. The executable linker is lld with four threads and
one concurrent link job. Full arguments are retained in the configuration JSON.

HTTP, gRPC, PostgreSQL, MySQL and Redis are enabled; ORM, YAML configuration and
other optional protocols are disabled. The target is `cnetmod_core`, including
its enabled component and Application dependencies: **227 PCM files**. This is
not a measurement of every optional protocol or a native Windows/MSVC build.

Other tasks ran HTTP/3 benchmarks and an Agensio build on this host. The user
explicitly accepted continuing under that load. Thus wall-clock differences are
**observations under shared load, not an isolated causal speedup claim**. Monitor
files retain process-name samples; those include this experiment's own compiler
processes and do not measure all Windows activity.

There are two measurement sets:

1. One fresh build directory per version with `-ftime-trace`. These locate
   expensive phases and measure PCM sizes. Baseline/candidate wall times were
   85.933/101.125 seconds. The candidate's slower complete diagnostic build is
   retained, not discarded; unrelated load makes these unsuitable for a speedup
   claim. Configure time is excluded from these two timings.
2. Tracing is then disabled for both versions. Flag-change rebuilds warm both
   trees; their labels contain `untraced-full`, but they are **not fresh clean
   builds** because C/assembly dependencies can remain warm. They are excluded
   from the paired result. Three measured pairs run AB/BA/AB. Before each run,
   cnetmod objects, PCMs and dependency-scan outputs are removed and the Ninja
   command log is cleared. The missing command hashes force third-party commands
   to rerun too, as shown in the logs. These are complete recompilations of the
   selected target graph, including third-party libraries, with existing CMake
   configuration and warm filesystem caches. CPU time is child-process user plus
   system time, not wall time. Output removal and CMake configuration time are
   excluded from each paired timer; no OS page-cache flush is performed.

| Round | Baseline elapsed, s | Candidate elapsed, s |
|---|---:|---:|
| 1 | 68.250 | 66.546 |
| 2 | 70.843 | 68.538 |
| 3 | 76.979 | 67.922 |

Elapsed median: **70.843 → 67.922 seconds**, observed reduction **4.12%**.

Child-process CPU median: **242.533 → 235.582 seconds**, observed reduction **2.87%**.

All six rounds return zero and execute the same command/output set, including third-party recompilation. Full measurements and the command-count audit are in `paired-summary.json`. These figures do not establish an isolated speedup under shared load.

## Findings and changes

`import std` has **339 direct import sites** in the baseline scanned scope, but
`std.cppm` is built once. Its PCM is 35,203,812 bytes in both versions. Its sampled
baseline compiler execution takes 2.124 s, including 1.702 s of frontend work and
0.405 s of AST serialization. Aggregated `ReadAST` totals across all baseline
compiler traces are 1.644 s. That measures direct AST-reading events, not all
semantic work or template instantiation caused by imports.

Clang 23 already defaults to [reduced BMI](https://clang.llvm.org/docs/StandardCPlusPlusModules.html#reduced-bmi). Enabling that flag again cannot be
counted as an optimization. CMake 4.4.3 does not recognize the repository's
`CXX_MODULE_GENERATION_MODE` property: its existing `SEPARATE` setting does not
establish a two-phase pipeline. This investigation does not patch CMake's
compiler rules or switch BMI formats.

The measured waste is repeated textual parsing and unnecessary declarations:

* `converter.cppm` and `converter_impl.cpp` now directly import `std` instead of
  textually including eight/nine standard-library headers. Integer and size
  aliases use explicit `std::` qualification. Public signatures retain their
  canonical types; all **58 external strong converter definitions** have
  identical mangled names in both objects (`converter-symbols.json`).
* `ssl.cppm` uses BoringSSL's opaque-type `base.h` instead of full `ssl.h` and
  `bio.h`. Concrete TLS operations continue to include their required headers
  in implementation units. No hand-written vendor type declarations or
  `BORINGSSL_NO_CXX` workaround is introduced.
* `io_uring_async_op.cpp` drops an unused `exec/static_thread_pool.hpp` include.
  Actual native thread-pool integration remains in `server_context_impl.cpp`.

| Sampled translation unit | Before, seconds | After, seconds |
|---|---:|---:|
| converter interface | 0.493 | 0.010 |
| converter implementation | 0.633 | 0.236 |
| SSL interface | 0.788 | 0.330 |
| io_uring async operations | 3.128 | 1.540 |

These are individual diagnostic trace observations under shared load, not
three-round medians or an estimate of whole-build speedup.

| PCM storage | Before, bytes | After, bytes | Reduction |
|---|---:|---:|---:|
| converter | 6,607,356 | 41,784 | 99.37% |
| SSL | 9,657,620 | 2,862,128 | 70.36% |
| All 227 PCMs | 143,650,516 | 130,177,952 | 9.38% |
| std | 35,203,812 | 35,203,812 | 0% |

The remaining baseline hotspots include BoringSSL's `bcm.cc` (12.561 s),
HTTP server implementation (5.996 s), PostgreSQL connection (4.179 s), Redis
template implementation (4.014 s), and HTTP client (3.903 s). Much of their
cost is optimization/code generation rather than PCM loading. Release
optimization levels and protocol behavior are preserved.

The analyzer also reports direct module fan-in and a weighted module dependency
chain. The baseline chain has 19 modules from `std` through SSL/HTTP/telemetry
to Application, with 6.672 s of summed interface-command durations. This is a
module-dependency lower bound, not the complete build's critical path: static
library ordering, implementation units and scheduling are outside that graph.
Removing public aggregate exports merely to shorten it would change the API.

## Validation

All **23 selected Release CTest suites pass** (4.30 seconds), including the new converter ABI/CRC/register cases, coroutine/task/locking primitives, stream writing, HTTP parser/compression/HTTP2, gRPC/protobuf, Redis/TLS/template, MySQL pool, PostgreSQL transport, Application integration and JSON templates. See `validation.json`, the build/test logs and JUnit XML. These are local protocol/unit fixtures; this run does not repeat external real-database interoperability, ASan or runtime throughput benchmarks. No runtime algorithm or Release optimization level is changed; converter body edits only qualify standard-library aliases.

All six paired builds execute **1,240 identical command slots / 1,883 outputs**, including **407 third-party commands**. Both no-op probes report `ninja: no work to do` (1.630/1.553 seconds including the CMake/Ninja invocation). The generated AGENTS check and source whitespace check pass. Of 939 runtime source files, exactly the four documented files differ; the rest are byte-identical in the native snapshots.

## Reproduction and evidence

Use the baseline revision above and the candidate revision in the final report
in separate checkouts with identical pinned third-party sources. The original
machine-specific drivers are retained in `reproduce/`; their source/archive and
build paths need adapting on another machine. Diagnostic builds use:

```sh
cmake -S /path/to/source -B build-profile -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ \
  '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -ftime-trace' \
  -DCNETMOD_USE_MOLD=OFF -DCNETMOD_USE_MIMALLOC=ON \
  -DCNETMOD_ENABLE_HTTP=ON -DCNETMOD_ENABLE_GRPC=ON \
  -DCNETMOD_ENABLE_POSTGRESQL=ON -DCNETMOD_ENABLE_MYSQL=ON \
  -DCNETMOD_ENABLE_REDIS=ON -DCNETMOD_ENABLE_ORM=OFF \
  -DCNETMOD_ENABLE_YAML_CONFIGURATION=OFF \
  -DCNETMOD_BUILD_TESTS=ON -DCNETMOD_BUILD_EXAMPLES=OFF \
  '-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -Wl,--threads=4' \
  -DCMAKE_JOB_POOLS=compile_link=1 -DCMAKE_JOB_POOL_LINK=compile_link
cmake --build build-profile --target cnetmod_core -j4
python tools/analyze_compile_time.py build-profile --output profile.json
```

Remove `-ftime-trace` for timing comparisons, and use the same target, flags,
dependency state and ordering in both checkouts. No compiler cache is used.
Time-trace totals overlap and must not be summed as wall time. Ninja command
totals are deduplicated when an object and a PCM share one compiler invocation.
Use a fresh build/log for a single-build analyzer summary; Ninja logs accumulate.

`baseline-profile.json` and `candidate-profile.json` retain aggregate traces,
largest PCMs, slow commands and module graphs. `traces/` retains the original
Clang JSON for `std` and the changed units. Build logs, Ninja timings, process
monitors, environment, source hashes, test logs and reproduction scripts are
included. The full build caches and remaining trace files stay under
`/root/cnetmod-compile-20261021` outside Git. `manifest.json` hashes the evidence.

The test token was released at **2026-10-21T12:49:56.028727+08:00** after all builds and tests completed. No owned build/test process remains, and this task started no containers. Build caches are retained for inspection, as recorded in `resource-release.json`.
