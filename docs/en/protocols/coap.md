# CoAP

cnetmod provides a UDP CoAP core for constrained-device and gateway workloads. The implementation is split into protocol types, datagram codec, client, and server/router modules under `cnetmod.protocol.coap`.

## Public Entry Point

Application code should start from the aggregation module:

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.coap;
```

The common API is concentrated under `cnetmod::coap`:

- `coap::client`: UDP CoAP client with `get()`, `post()`, `put()`, `delete_()`, `observe()`, `cancel_observe()`, `get_blockwise()`, `post_blockwise()`, and `put_blockwise()`
- `coap::server`: UDP CoAP server with `listen()`, `route()`, `register_resource()`, `run()`, `notify_observers()`, and `stop()`
- `coap::text_response()` / `coap::json_response()`: response helpers for text and JSON payloads
- `coap::payload_text()` / `coap::to_bytes()`: payload conversion helpers

`cnetmod.protocol.coap` re-exports protocol types, codec, client, server, and facade helpers. Interfaces live in the matching `.cppm` files; implementations are split into same-name `.cpp` files.

## Current Scope

- RFC 7252 datagram header, token, option delta/length, and payload marker parsing/serialization
- Confirmable and non-confirmable messages, acknowledgement and reset message types
- Standard method and response code enums
- URI path/query, content-format, accept, observe, Block1, Block2, and size option identifiers
- Confirmable client request retransmission with token/message-id response matching
- Observe registration, deregistration, notification sequence numbers, max-age handling, and server-side subscription cleanup
- UDP server loop with resource router, request path/query extraction, ACK normalization, and reset replies for unexpected confirmable messages
- `/.well-known/core` resource discovery with `rt`, `if`, and `obs` link-format attributes
- Server-side Block1 upload reassembly and Block2 response slicing, plus client-side Block2 download assembly and Block1 upload slicing
- Basic `Proxy-Uri` forwarding for UDP CoAP upstream requests with `coap://host:port/path?query`, default ports, hostname resolution, and IPv6 bracket addresses
- Unknown critical options return `4.02 Bad Option`; existing paths with unsupported methods return `4.05 Method Not Allowed`
- CoAPS foundation through OpenSSL DTLS client/server contexts in `cnetmod.core.ssl`

OSCORE object security is not part of this module. CoAP UDP routing, Observe, resource discovery, Blockwise, Proxy-Uri, and retransmission are provided by `cnetmod.protocol.coap`.

## Server

```cpp
import cnetmod.protocol.coap;

using namespace cnetmod;

task<void> run_coap_server(io_context& ctx) {
    coap::server server(ctx);
    auto listen = server.listen("0.0.0.0", coap::default_port);
    if (!listen) {
        throw std::system_error(listen.error());
    }

    server.route(coap::method::get, "/sensors/temp",
        [](const coap::inbound_request& req, const endpoint&) -> task<coap::message> {
            co_return coap::text_response(req.request, "22.5");
        });

    co_await server.run();
}
```

The router normalizes paths so both `sensors/temp` and `/sensors/temp` map to `/sensors/temp`. If no handler is installed, the server returns `5.01 Not Implemented`; if a route is missing, it returns `4.04 Not Found`.

## Observe

```cpp
auto initial = co_await client.observe(*remote, "/sensors/temp",
    [](const coap::message& notification) {
        std::string body(reinterpret_cast<const char*>(notification.payload.data()),
            notification.payload.size());
    },
    std::chrono::seconds(30));
```

Server routes automatically register `GET` requests with `Observe: 0` and remove registrations for `Observe: 1`. Applications publish updates with `udp_server::notify_observers(path, message)`.

## CoAPS / DTLS

OpenSSL-backed DTLS contexts are available through `ssl_context::dtls_client()` and `ssl_context::dtls_server()`. Use `coap::default_secure_port` (`5684`) for CoAPS endpoints.

The Python interop test in `testing/coap_interop.py` intentionally exercises UDP CoAP because Python's standard library does not ship DTLS. DTLS context creation and build coverage are validated by the C++ build; wire-level DTLS interop requires a Python DTLS package such as aiocoap with an installed DTLS backend.

## Client

```cpp
import cnetmod.protocol.coap;

using namespace cnetmod;

task<void> read_temperature(io_context& ctx) {
    coap::client client(ctx);

    auto remote = co_await client.resolve_endpoint("127.0.0.1", coap::default_port);
    if (!remote) {
        throw std::system_error(remote.error());
    }

    auto response = co_await client.get(*remote, "/sensors/temp");
    if (!response) {
        throw std::system_error(response.error());
    }

    if (response->response() == coap::response_code::content) {
        std::string body = coap::payload_text(*response);
    }
}
```

`udp_client` fills message IDs and 8-byte tokens when they are not supplied. Confirmable requests are retransmitted using `client_config::ack_timeout` and `client_config::max_retransmit`.

## Raw Codec

Use the codec directly when integrating with a custom transport, packet capture pipeline, or fuzz harness:

```cpp
auto request = coap::make_request({
    .method_code = coap::method::post,
    .path = "/telemetry",
    .content_type = coap::content_format::json,
});

auto encoded = coap::serialize_message(request);
auto decoded = encoded ? coap::parse_message(*encoded) : std::unexpected(encoded.error());
```

The codec returns `std::expected<..., std::error_code>` and rejects malformed version, token length, option extension, payload marker, and empty-message combinations.

## Block Options

```cpp
coap::block_option block{
    .number = 0,
    .more = true,
    .size_exponent = 6,
};

auto bytes = coap::encode_block_option(block);
auto parsed = coap::decode_block_option(bytes);
```

The helpers implement the Block1/Block2 option wire representation. The server reassembles Block1 upload payloads and slices large Block2 responses; the client provides `get_blockwise()`, `post_blockwise()`, and `put_blockwise()` for common blockwise transfers.
