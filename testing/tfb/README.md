# cnetmod — TechEmpower Framework Benchmarks

[cnetmod](https://github.com/banderzhm/cnetmod) is a cross-platform asynchronous network library using C++23 modules and native coroutines.

## Infrastructure

**Language**: C++23

**Compiler**: clang-21 + libc++ (C++23 modules require latest toolchain)

**I/O Backend**: io_uring (Linux)

**Server**: Built-in multi-core HTTP server (`server_context` with accept thread + N worker `io_context` threads)

**Database**: MySQL (async client with connection pool)

## Test URLs

| Test Type | URL |
|-----------|-----|
| JSON | `/json` |
| Plaintext | `/plaintext` |
| DB | `/db` |
| Queries | `/queries?queries=` |
| Fortunes | `/fortunes` |
| Updates | `/updates?queries=` |

## Key Features

- Zero-overhead C++20 coroutines (`task<T>`, `co_await`)
- io_uring for syscall-free async I/O on Linux
- Multi-core architecture with lock-free connection dispatch
- Cached `Date` header (re-rendered once per second)
- No middleware overhead in benchmark configuration
- No disk logging
