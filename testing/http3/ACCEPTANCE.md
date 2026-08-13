# HTTP/3 / QUIC release acceptance

No script in this directory treats a missing peer, privileges, or a failed
request as a pass.  CTest uses exit status `77` for an environmental skip;
only an actual response validated by the test is a pass.

## Interoperability

Build `h3_interop_server` and `h3_interop_client`, install `aioquic`, and use
a curl build whose `curl --version` lists HTTP3:

```sh
python3 testing/http3/h3_acceptance.py \
  --server build/bin/h3_interop_server \
  --client build/bin/h3_interop_client \
  --results h3-interop-results.json
```

On Windows with the Visual Studio 2026 build, run the same gate from
PowerShell (the executable paths are configuration-specific):

```powershell
python testing/http3/h3_acceptance.py `
  --server .\cmake-build-quic-windows\bin\Release\h3_interop_server.exe `
  --client .\cmake-build-quic-windows\bin\Release\h3_interop_client.exe `
  --port 45433 `
  --results .\cmake-build-quic-windows\h3-interop-release.json
```

The same command can be run with `bin\Debug` and a different UDP port. The
verified Windows Debug and Release runs each passed the aioquic server to
cnetmod client request and the aioquic WebTransport to cnetmod server probe.
The generated JSON records each pass/skip status and should be kept with CI
artifacts rather than committed as a source fixture.

The required HTTP/3 directions are cnetmod server → curl HTTP/3 and aioquic
server → cnetmod client. When aioquic is installed, the same gate also runs
aioquic WebTransport → cnetmod server, covering Extended CONNECT, child
streams, HTTP Datagrams, and Close Capsules. The JSON record identifies each
peer as passed, failed, or skipped. A release needs the applicable cases
passed; a skipped case is not release evidence. nghttp3 distributions do not expose one portable CLI, so
CI must pin its invocation and pass it explicitly; `{url}`, `{port}`, `{cert}`
`{key}`, and `{root}` are expanded by the harness. The client command must
write the response body to stdout so the gate can validate the expected
`ok` payload:

```sh
python3 testing/http3/h3_acceptance.py ... \
  --nghttp3-client-command 'your-nghttp3-client {url}' \
  --nghttp3-server-command 'your-nghttp3-server --port {port} --cert {cert} --key {key}'
```

The verified Arch Linux gate uses the official ngtcp2 examples linked with
libnghttp3 1.18.0. These commands validate both directions and the response
body (ngtcp2 1.25.0 binary paths shown as examples):

```sh
python3 testing/http3/h3_acceptance.py ... \
  --nghttp3-client-command \
    'osslclient --timeout=1s --no-quic-dump --exit-on-first-stream-close 127.0.0.1 4433 {url}' \
  --nghttp3-server-command \
    'osslserver -q --htdocs {root} 127.0.0.1 {port} {key} {cert}'
```

### Rust `wtransport` WebTransport peer

The repository includes an independent Rust [`wtransport`](https://crates.io/crates/wtransport)
client at `testing/bench/crosslang/rust/src/bin/webtransport_probe.rs`.  It
validates Extended CONNECT, bidirectional and unidirectional child streams,
and an HTTP Datagram echo against the cnetmod fixture. Add it to the same
acceptance run with an explicit command template:

```sh
cargo build --manifest-path testing/bench/crosslang/rust/Cargo.toml --bin webtransport_probe
python3 testing/http3/h3_acceptance.py ... \
  --wtransport-probe testing/bench/crosslang/rust/target/debug/webtransport_probe
```

To include the same probe in CTest, configure with
`-DCNETMOD_WTRANSPORT_PROBE=/absolute/path/to/webtransport_probe`.

When the probe is supplied, the gate runs three independent connections by
default (`--wtransport-repeats 3`). This catches ordering-sensitive regressions
that can pass a single WebTransport connection; use a larger value for a
release soak test. Each repetition uses a fresh fixture process and UDP port,
so a cancelled session cannot contaminate the next result.

If Rust/Cargo is not available, this case is recorded as `skipped`, not
`passed`.

### Connection migration regression

The CTest target `http3_connection_migration` runs the cnetmod client and
server through a loopback UDP proxy. After the first successful request, the
proxy changes the source UDP port used toward the server. The second request
must complete on the same QUIC connection, proving `PATH_CHALLENGE` /
`PATH_RESPONSE`, CID routing, and post-validation endpoint switching. The
fixture is built and run in both Visual Studio Debug and Release configurations
on Windows CI; it is not a reconnect test.

## Weak network

Run this only in a disposable Linux network namespace/veth interface.  The
gate refuses to replace a non-default host qdisc and always removes the qdisc
it installed:

```sh
sudo python3 testing/http3/h3_weaknet_gate.py --interface veth-h3 \
  --delay-ms 80 --jitter-ms 20 --loss-percent 2 \
  --command './your-http3-client-command'
```

## Fuzzing and malformed input

Use a Clang/libFuzzer configuration:

```sh
cmake -S . -B build-fuzz -DCNETMOD_ENABLE_QUIC=ON -DCNETMOD_BUILD_FUZZERS=ON \
  -DCMAKE_CXX_COMPILER=clang++
cmake --build build-fuzz --target fuzz_quic_packet
./build-fuzz/testing/fuzz_quic_packet -max_total_time=300 corpus/quic_packet
```

The target feeds arbitrary untrusted bytes to packet type, long/short-header,
and coalesced-datagram parsing.

## Performance record

Start a known HTTP/3 server first, pin hardware/OS/curl version, then record
real serial request latencies.  The command fails if any request fails.

```sh
python3 testing/http3/h3_performance_gate.py \
  --url https://127.0.0.1:4433/health --requests 1000 \
  --output h3-linux-baseline.json
```
