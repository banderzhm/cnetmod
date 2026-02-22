# TCP/UDP Programming

This guide covers low-level TCP and UDP socket programming with cnetmod.

## TCP Sockets

### Server (Accept Connections)

```cpp
import cnetmod;
import cnetmod.protocol.tcp;
using namespace cnetmod;

task<void> handle_client(tcp_socket client, address addr) {
    std::println("Client connected: {}", addr.to_string());
    
    char buffer[1024];
    while (true) {
        int n = co_await client.async_recv(buffer, sizeof(buffer));
        if (n <= 0) break;  // Connection closed or error
        
        co_await client.async_send(buffer, n);
    }
    
    std::println("Client disconnected");
}

task<void> tcp_server(io_context& ctx, uint16_t port) {
    tcp_acceptor acceptor(ctx, port);
    std::println("Server listening on port {}", port);
    
    while (true) {
        auto [client, addr] = co_await acceptor.async_accept();
        spawn(ctx, handle_client(std::move(client), addr));
    }
}

int main() {
    net_init guard;
    io_context ctx;
    spawn(ctx, tcp_server(ctx, 8080));
    ctx.run();
}
```

### Client (Connect to Server)

```cpp
task<void> tcp_client(io_context& ctx) {
    tcp_socket socket(ctx);
    
    // Resolve address
    address addr = co_await async_resolve(ctx, "example.com", 80);
    
    // Connect
    co_await socket.async_connect(addr);
    std::println("Connected to {}", addr.to_string());
    
    // Send request
    const char* request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    co_await socket.async_send(request, std::strlen(request));
    
    // Receive response
    char buffer[4096];
    int n = co_await socket.async_recv(buffer, sizeof(buffer));
    std::println("Received {} bytes", n);
    
    // Socket automatically closed when destroyed
}
```

### TCP Socket API

```cpp
class tcp_socket {
public:
    // Construction
    explicit tcp_socket(io_context& ctx);
    tcp_socket(tcp_socket&& other) noexcept;
    
    // Connection
    task<void> async_connect(const address& addr);
    
    // I/O operations
    task<int> async_send(const void* data, size_t size);
    task<int> async_recv(void* buffer, size_t size);
    
    // Shutdown
    void shutdown(shutdown_type type);  // read, write, or both
    void close();
    
    // Socket options
    void set_nodelay(bool enable);  // Disable Nagle's algorithm
    void set_keepalive(bool enable);
    void set_recv_buffer_size(int size);
    void set_send_buffer_size(int size);
    
    // Status
    bool is_open() const;
    address local_address() const;
    address remote_address() const;
};
```

### TCP Acceptor API

```cpp
class tcp_acceptor {
public:
    // Construction
    tcp_acceptor(io_context& ctx, uint16_t port);
    tcp_acceptor(io_context& ctx, const address& bind_addr);
    
    // Accept connections
    task<std::pair<tcp_socket, address>> async_accept();
    
    // Socket options
    void set_reuse_address(bool enable);
    void set_reuse_port(bool enable);  // Linux only
    
    // Status
    bool is_open() const;
    address local_address() const;
};
```

## UDP Sockets

### Server (Receive Datagrams)

```cpp
import cnetmod.protocol.udp;

task<void> udp_server(io_context& ctx, uint16_t port) {
    udp_socket socket(ctx);
    socket.bind(address::any_ipv4(port));
    
    std::println("UDP server listening on port {}", port);
    
    char buffer[1024];
    while (true) {
        auto [n, from] = co_await socket.async_recvfrom(buffer, sizeof(buffer));
        std::println("Received {} bytes from {}", n, from.to_string());
        
        // Echo back
        co_await socket.async_sendto(buffer, n, from);
    }
}
```

### Client (Send Datagrams)

```cpp
task<void> udp_client(io_context& ctx) {
    udp_socket socket(ctx);
    
    address server_addr = co_await async_resolve(ctx, "example.com", 8080);
    
    // Send datagram
    const char* message = "Hello, UDP!";
    co_await socket.async_sendto(message, std::strlen(message), server_addr);
    
    // Receive response
    char buffer[1024];
    auto [n, from] = co_await socket.async_recvfrom(buffer, sizeof(buffer));
    std::println("Received {} bytes from {}", n, from.to_string());
}
```

### UDP Socket API

```cpp
class udp_socket {
public:
    // Construction
    explicit udp_socket(io_context& ctx);
    
    // Binding
    void bind(const address& addr);
    
    // I/O operations
    task<int> async_sendto(const void* data, size_t size, const address& dest);
    task<std::pair<int, address>> async_recvfrom(void* buffer, size_t size);
    
    // Connected UDP (optional)
    void connect(const address& addr);
    task<int> async_send(const void* data, size_t size);
    task<int> async_recv(void* buffer, size_t size);
    
    // Socket options
    void set_broadcast(bool enable);
    void set_multicast_loop(bool enable);
    void join_multicast_group(const address& group);
    void leave_multicast_group(const address& group);
    
    // Status
    bool is_open() const;
    address local_address() const;
};
```

## Address Resolution

### Async DNS Lookup

```cpp
// Resolve hostname to address
address addr = co_await async_resolve(ctx, "example.com", 80);

// Resolve with service name
address addr = co_await async_resolve(ctx, "example.com", "http");

// IPv4 only
address addr = co_await async_resolve(ctx, "example.com", 80, address_family::ipv4);

// IPv6 only
address addr = co_await async_resolve(ctx, "example.com", 80, address_family::ipv6);
```

### Address Construction

```cpp
// IPv4
address addr = address::from_ipv4("192.168.1.1", 8080);
address addr = address::any_ipv4(8080);  // 0.0.0.0:8080
address addr = address::loopback_ipv4(8080);  // 127.0.0.1:8080

// IPv6
address addr = address::from_ipv6("::1", 8080);
address addr = address::any_ipv6(8080);  // [::]:8080
address addr = address::loopback_ipv6(8080);  // [::1]:8080

// From string
address addr = address::from_string("192.168.1.1:8080");
address addr = address::from_string("[::1]:8080");
```

### Address API

```cpp
class address {
public:
    // Construction
    static address from_ipv4(std::string_view ip, uint16_t port);
    static address from_ipv6(std::string_view ip, uint16_t port);
    static address from_string(std::string_view str);
    static address any_ipv4(uint16_t port);
    static address any_ipv6(uint16_t port);
    static address loopback_ipv4(uint16_t port);
    static address loopback_ipv6(uint16_t port);
    
    // Properties
    std::string to_string() const;
    std::string ip() const;
    uint16_t port() const;
    address_family family() const;  // ipv4 or ipv6
    
    // Comparison
    bool operator==(const address& other) const;
    auto operator<=>(const address& other) const;
};
```

## Error Handling

### Connection Errors

```cpp
task<void> connect_with_error_handling(io_context& ctx) {
    tcp_socket socket(ctx);
    
    try {
        co_await socket.async_connect(addr);
        std::println("Connected");
    } catch (const connection_refused&) {
        std::println("Connection refused");
    } catch (const connection_timeout&) {
        std::println("Connection timed out");
    } catch (const network_unreachable&) {
        std::println("Network unreachable");
    } catch (const std::system_error& e) {
        std::println("Error: {}", e.what());
    }
}
```

### I/O Errors

```cpp
task<void> recv_with_error_handling(tcp_socket& socket) {
    char buffer[1024];
    
    try {
        int n = co_await socket.async_recv(buffer, sizeof(buffer));
        if (n == 0) {
            std::println("Connection closed by peer");
        } else {
            std::println("Received {} bytes", n);
        }
    } catch (const connection_reset&) {
        std::println("Connection reset by peer");
    } catch (const std::system_error& e) {
        std::println("I/O error: {}", e.what());
    }
}
```

## Advanced Patterns

### Connection Pooling

```cpp
class tcp_connection_pool {
    io_context& ctx_;
    address server_addr_;
    std::vector<tcp_socket> connections_;
    semaphore sem_;
    
public:
    tcp_connection_pool(io_context& ctx, address addr, size_t pool_size)
        : ctx_(ctx), server_addr_(addr), sem_(pool_size) {
        for (size_t i = 0; i < pool_size; ++i) {
            connections_.emplace_back(ctx_);
        }
    }
    
    task<tcp_socket*> acquire() {
        co_await sem_.acquire();
        
        for (auto& conn : connections_) {
            if (!conn.is_open()) {
                co_await conn.async_connect(server_addr_);
            }
            co_return &conn;
        }
        
        co_return nullptr;
    }
    
    void release(tcp_socket* conn) {
        sem_.release();
    }
};
```

### Timeout Pattern

```cpp
task<void> connect_with_timeout(io_context& ctx, address addr) {
    tcp_socket socket(ctx);
    
    try {
        co_await with_timeout(ctx, socket.async_connect(addr), 5s);
        std::println("Connected");
    } catch (const timeout_error&) {
        std::println("Connection timed out");
        socket.close();
    }
}
```

### Retry Pattern

```cpp
task<void> connect_with_retry(io_context& ctx, address addr) {
    retry_policy policy{
        .max_attempts = 3,
        .initial_delay = 1s,
        .max_delay = 10s,
        .backoff_multiplier = 2.0
    };
    
    tcp_socket socket(ctx);
    
    co_await retry(ctx, policy, [&]() -> task<void> {
        co_await socket.async_connect(addr);
    });
    
    std::println("Connected after retries");
}
```

### Scatter-Gather I/O

```cpp
task<void> scatter_gather_example(tcp_socket& socket) {
    // Gather: send multiple buffers in one call
    std::array<std::span<const char>, 3> buffers = {
        std::span{"Header", 6},
        std::span{"Body", 4},
        std::span{"Footer", 6}
    };
    
    int total_sent = 0;
    for (const auto& buf : buffers) {
        int n = co_await socket.async_send(buf.data(), buf.size());
        total_sent += n;
    }
    
    std::println("Sent {} bytes total", total_sent);
}
```

### Keep-Alive

```cpp
task<void> keep_alive_connection(io_context& ctx, tcp_socket& socket) {
    socket.set_keepalive(true);
    
    // Send periodic heartbeat
    while (socket.is_open()) {
        co_await async_sleep(ctx, 30s);
        
        try {
            co_await socket.async_send("PING", 4);
        } catch (...) {
            std::println("Keep-alive failed, connection lost");
            break;
        }
    }
}
```

## Performance Tips

### 1. Use Nodelay for Low Latency

```cpp
socket.set_nodelay(true);  // Disable Nagle's algorithm
```

### 2. Adjust Buffer Sizes

```cpp
socket.set_recv_buffer_size(256 * 1024);  // 256 KB
socket.set_send_buffer_size(256 * 1024);
```

### 3. Reuse Addresses

```cpp
acceptor.set_reuse_address(true);
acceptor.set_reuse_port(true);  // Linux only
```

### 4. Use Connection Pooling

Avoid connection overhead by reusing connections.

### 5. Batch Small Writes

```cpp
// BAD: Many small writes
for (const auto& msg : messages) {
    co_await socket.async_send(msg.data(), msg.size());
}

// GOOD: Batch into larger write
std::string batch;
for (const auto& msg : messages) {
    batch += msg;
}
co_await socket.async_send(batch.data(), batch.size());
```

## Platform Differences

### Windows (IOCP)
- Overlapped I/O with completion ports
- Excellent scalability (100K+ connections)
- AcceptEx for high-performance accept

### Linux (io_uring)
- Modern async I/O interface
- Zero-copy operations
- Best performance on recent kernels (5.10+)

### Linux (epoll)
- Fallback when io_uring unavailable
- Edge-triggered mode for efficiency
- Widely supported

### macOS (kqueue)
- BSD kernel event notification
- Good performance
- Similar to epoll

## Next Steps

- **[HTTP Server](http.md)** - Build REST APIs
- **[WebSocket](websocket.md)** - Real-time communication
- **[MQTT](mqtt.md)** - IoT messaging
- **[MySQL](mysql.md)** - Database access
