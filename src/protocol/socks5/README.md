# SOCKS5 Protocol Implementation

A complete SOCKS5 (RFC 1928) proxy protocol implementation for cnetmod.

## Features

- ✅ Full SOCKS5 protocol support (RFC 1928)
- ✅ Multiple authentication methods:
  - No authentication
  - Username/Password authentication (RFC 1929)
- ✅ Address types:
  - IPv4
  - IPv6
  - Domain names
- ✅ CONNECT command support
- ✅ Async/await coroutine-based API
- ✅ Client and Server implementations
- ✅ Connection limiting
- ✅ Bidirectional data relay

## Client Usage

```cpp
import cnetmod.io.io_context;
import cnetmod.protocol.socks5;

using namespace cnetmod;
using namespace cnetmod::socks5;

auto example(io_context& ctx) -> task<void> {
    // Create client
    client socks_client(ctx);
    
    // Connect to proxy
    co_await socks_client.connect("127.0.0.1", 1080);
    
    // Authenticate (if needed)
    co_await socks_client.authenticate("username", "password");
    
    // Connect to target through proxy
    co_await socks_client.connect_target("www.example.com", 80);
    
    // Use the socket for data transfer
    auto& sock = socks_client.socket();
    // ... send/receive data ...
}
```

## Server Usage

```cpp
import cnetmod.io.io_context;
import cnetmod.protocol.socks5;

using namespace cnetmod;
using namespace cnetmod::socks5;

auto example(io_context& ctx) -> task<void> {
    // Configure server
    server_config config;
    config.allow_no_auth = true;
    config.allow_username_password = true;
    config.authenticator = [](std::string_view user, std::string_view pass) {
        return user == "admin" && pass == "secret";
    };
    config.max_connections = 100;
    
    // Create and run server
    server socks_server(ctx, config);
    co_await socks_server.listen("0.0.0.0", 1080);
    co_await socks_server.run();
}
```

## Protocol Details

### Authentication Methods

- `0x00` - No authentication required
- `0x01` - GSSAPI (not implemented)
- `0x02` - Username/Password
- `0xFF` - No acceptable methods

### Commands

- `0x01` - CONNECT (supported)
- `0x02` - BIND (not implemented)
- `0x03` - UDP ASSOCIATE (not implemented)

### Address Types

- `0x01` - IPv4 address (4 bytes)
- `0x03` - Domain name (variable length)
- `0x04` - IPv6 address (16 bytes)

### Reply Codes

- `0x00` - Succeeded
- `0x01` - General SOCKS server failure
- `0x02` - Connection not allowed by ruleset
- `0x03` - Network unreachable
- `0x04` - Host unreachable
- `0x05` - Connection refused
- `0x06` - TTL expired
- `0x07` - Command not supported
- `0x08` - Address type not supported

## Architecture

```
src/protocol/socks5/
├── types.cppm           # Protocol types and structures
├── types_impl.cpp       # Serialization/parsing implementation
├── client.cppm          # SOCKS5 client interface
├── client_impl.cpp      # Client implementation
├── server.cppm          # SOCKS5 server interface
├── server_impl.cpp      # Server implementation
└── README.md            # This file

src/protocol/socks5.cppm # Main module export
```

## Examples

See `examples/socks5/` for complete examples:
- `client_demo.cpp` - SOCKS5 client example
- `server_demo.cpp` - SOCKS5 server example

## Testing

You can test the SOCKS5 implementation with standard tools:

```bash
# Test with curl through SOCKS5 proxy
curl --socks5 127.0.0.1:1080 http://www.example.com

# Test with username/password
curl --socks5 admin:secret@127.0.0.1:1080 http://www.example.com
```

## Limitations

- BIND command not implemented
- UDP ASSOCIATE command not implemented
- GSSAPI authentication not implemented
- DNS resolution in server uses simple IP parsing (no actual DNS lookup)
- Bidirectional relay is simplified (should use concurrent tasks in production)

## References

- [RFC 1928](https://tools.ietf.org/html/rfc1928) - SOCKS Protocol Version 5
- [RFC 1929](https://tools.ietf.org/html/rfc1929) - Username/Password Authentication for SOCKS V5
