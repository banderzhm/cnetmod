# cnetmod Rust binding

This crate is a safe wrapper over `include/cnetmod/c_api.h`. It is intentionally
caller-driven: the thread owning `Runtime` calls `poll()` for an existing event
loop or `run_one()` for a dedicated loop. Completion closures run on that same
thread, while `Request::cancel()` is safe from another thread.

`HttpClient` shares (rather than mutably borrows) its caller-owned runtime, so
the normal pattern is valid and safe:

```rust
let mut runtime = cnetmod::Runtime::new()?;
let mut client = runtime.http_client(Default::default())?;
let _request = client.start(cnetmod::Method::Get, "https://example.com/", b"", on_done)?;
while !done {
    runtime.run_one();
    runtime.restart();
}
```

Both values remain deliberately thread-affine (`!Send` / `!Sync`). A client
keeps the native runtime alive until it is dropped, preventing a dangling C ABI
handle if the Rust `Runtime` wrapper is released first.

Build cnetmod first, then point Cargo at the configuration directory:

```powershell
$env:CNETMOD_C_API_LIB_DIR = "E:\github\cnetmod\cmake-build-quic-windows\Debug"
cargo check --manifest-path bindings/rust/cnetmod/Cargo.toml
```

`cargo check` deliberately validates only the Rust wrapper. A native executable
or the loopback integration test must link both static archives and the CMake
target's transitive libraries. The following Windows command runs the real
native test against the Release build produced in this repository:

```powershell
$root = "E:\github\cnetmod\cmake-build-quic-windows"
$env:CNETMOD_C_API_LIB_DIR = "$root\Release"
$env:CNETMOD_C_API_LINK_SEARCH = @(
  "$root\Release",
  "$root\third_party\boringssl\build\Release",
  "$root\3rdparty\pugixml\Release",
  "$root\3rdparty\leveldb\Release",
  "$root\vcpkg_installed\x64-windows\lib",
  "D:\runtime\miniconda3\Library\lib"
) -join ';'
$env:CNETMOD_C_API_LINK_LIBS =
  "cnetmod_core,ssl,crypto,pugixml,zlib,zstd_static,brotlienc,brotlidec,liblz4,leveldb,mswsock,ws2_32"
$env:PATH = "$env:CNETMOD_C_API_LIB_DIR;$env:PATH"
cargo test --manifest-path bindings/rust/cnetmod/Cargo.toml
```

For an installed or differently configured build, obtain the corresponding
dependency list from CMake's exported `cnetmod::cnetmod_c` target and provide
its library directories through `CNETMOD_C_API_LINK_SEARCH`, and its link
libraries through `CNETMOD_C_API_LINK_LIBS`. This keeps the Rust crate free of
hard-coded C++ build-tool assumptions while still making the final native link
fully explicit.
