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

The two required directions are cnetmod server → curl HTTP/3 and aioquic
server → cnetmod client.  The JSON record identifies each peer as passed,
failed, or skipped.  A release needs both cases passed; a skipped case is not
release evidence.  nghttp3 distributions do not expose one portable CLI, so
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
