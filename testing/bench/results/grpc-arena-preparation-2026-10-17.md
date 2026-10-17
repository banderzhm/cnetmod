# gRPC Arena preparation — 2026-10-17

Status: tooling and correctness validated; performance measurement is pending
the shared host's explicit test token. No RPS, latency improvement or CPU
hotspot claim has been measured. No production protocol code has changed.

Baseline fetched from `origin/develop`:
`5c969d4ac3b772963df80ff66c93fc8165901dc1`.
Task branch: `codex/grpc-performance`.
HttpArena reference: `8977be2721a5aba7b320345cddfcd6f6b654e698`.

Environment:

- Windows host; local Arch Linux WSL2, kernel
  `6.18.33.2-microsoft-standard-WSL2`, x86_64.
- Intel Core i9-14900K exposed as 32 logical CPUs by WSL.
- Clang/libc++ 23.1.3, CMake 4.4.3, Ninja, mold, bundled mimalloc.
- Release: `-O3 -DNDEBUG -g -fno-omit-frame-pointer`, portable CPU target,
  no ThinLTO, HTTP and gRPC enabled, ORM and YAML configuration disabled.
- Build parallelism limited to four jobs.
- Independent client: grpcio 1.84.0 in `/opt/cnetmod-ci-venv`.
- Frozen h2load: nghttp2 1.68.0, SHA256
  `e1205b524cb3e048079f95cda068d98612780097c3c5561624426b1ad9fca9f6`.
- Baseline harness server SHA256:
  `20aa07970b5fae3edafaad6c822bcbf7ce97b4b4cf0536de3e8cab6c9cf729f0`.
- `perf stat -e task-clock,cycles true`: task-clock supported, cycles
  unsupported. `perf record -e cpu-clock -- sleep 0.1` succeeded. These are
  capability probes, not application profiles.

Validation: all 11 selected CTest tests passed, none skipped, in 1.74 seconds:

- `test_http2_hpack`, `test_http2_stream` (includes flow-control regression).
- `test_grpc`, `test_grpc_proto`.
- Protobuf Python oracle and two code generation checks.
- grpcio-to-cnetmod and cnetmod-to-grpcio cross-process interop.
- `test_grpc_arena_harness` (three parser/affinity tests).
- `test_grpc_arena_smoke` (both plaintext and TLS, one server worker,
  seven wire vectors plus malformed-input and unknown-method status checks).

`python tools/generate_agents.py --check` and `git diff --check` passed.
The existing build is `/tmp/cnetmod-grpc-release` in the `archlinux` WSL
distribution. Baseline binaries and verification logs are preserved under
`/root/cnetmod-grpc-artifacts` for the later same-host measurements.

Next phase: obtain the exclusive test token, stop builds, run the four
cleartext/TLS × 256/1024-connection profiles, then collect a separate perf
recording. Attribute HTTP/2, HPACK, protobuf, stream lifetime, buffers and
kernel/socket samples before choosing an optimization. Follow
[GRPC_ARENA.md](../GRPC_ARENA.md) for exact commands and comparison limits.
