# TCP/UDP 编程

本指南涵盖使用 cnetmod 进行低级 TCP 和 UDP 套接字编程。

## TCP 套接字

### 服务器（接受连接）

```cpp
import cnetmod;
import cnetmod.protocol.tcp;
using namespace cnetmod;

task<void> handle_client(tcp_socket client, address addr) {
    std::println("Client connected: {}", addr.to_string());
    
    char buffer[1024];
    while (true) {
        int n = co_await client.async_recv(buffer, sizeof(buffer));
        if (n <= 0) break;  // 连接关闭或错误
        
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

### 客户端（连接到服务器）

```cpp
task<void> tcp_client(io_context& ctx) {
    tcp_socket socket(ctx);
    
    // 解析地址
    address addr = co_await async_resolve(ctx, "example.com", 80);
    
    // 连接
    co_await socket.async_connect(addr);
    std::println("Connected to {}", addr.to_string());
    
    // 发送请求
    const char* request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    co_await socket.async_send(request, std::strlen(request));
    
    // 接收响应
    char buffer[4096];
    int n = co_await socket.async_recv(buffer, sizeof(buffer));
    std::println("Received {} bytes", n);
    
    // Socket 在销毁时自动关闭
}
```

### TCP Socket API

```cpp
class tcp_socket {
public:
    // 构造
    explicit tcp_socket(io_context& ctx);
    tcp_socket(tcp_socket&& other) noexcept;
    
    // 连接
    task<void> async_connect(const address& addr);
    
    // I/O 操作
    task<int> async_send(const void* data, size_t size);
    task<int> async_recv(void* buffer, size_t size);
    
    // 关闭
    void shutdown(shutdown_type type);  // read、write 或 both
    void close();
    
    // Socket 选项
    void set_nodelay(bool enable);  // 禁用 Nagle 算法
    void set_keepalive(bool enable);
    void set_recv_buffer_size(int size);
    void set_send_buffer_size(int size);
    
    // 状态
    bool is_open() const;
    address local_address() const;
    address remote_address() const;
};
```

### TCP Acceptor API

```cpp
class tcp_acceptor {
public:
    // 构造
    tcp_acceptor(io_context& ctx, uint16_t port);
    tcp_acceptor(io_context& ctx, const address& bind_addr);
    
    // 接受连接
    task<std::pair<tcp_socket, address>> async_accept();
    
    // Socket 选项
    void set_reuse_address(bool enable);
    void set_reuse_port(bool enable);  // 仅 Linux
    
    // 状态
    bool is_open() const;
    address local_address() const;
};
```

## UDP 套接字

### 服务器（接收数据报）

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
        
        // 回显
        co_await socket.async_sendto(buffer, n, from);
    }
}
```

### 客户端（发送数据报）

```cpp
task<void> udp_client(io_context& ctx) {
    udp_socket socket(ctx);
    
    address server_addr = co_await async_resolve(ctx, "example.com", 8080);
    
    // 发送数据报
    const char* message = "Hello, UDP!";
    co_await socket.async_sendto(message, std::strlen(message), server_addr);
    
    // 接收响应
    char buffer[1024];
    auto [n, from] = co_await socket.async_recvfrom(buffer, sizeof(buffer));
    std::println("Received {} bytes from {}", n, from.to_string());
}
```

### UDP Socket API

```cpp
class udp_socket {
public:
    // 构造
    explicit udp_socket(io_context& ctx);
    
    // 绑定
    void bind(const address& addr);
    
    // I/O 操作
    task<int> async_sendto(const void* data, size_t size, const address& dest);
    task<std::pair<int, address>> async_recvfrom(void* buffer, size_t size);
    
    // 连接的 UDP（可选）
    void connect(const address& addr);
    task<int> async_send(const void* data, size_t size);
    task<int> async_recv(void* buffer, size_t size);
    
    // Socket 选项
    void set_broadcast(bool enable);
    void set_multicast_loop(bool enable);
    void join_multicast_group(const address& group);
    void leave_multicast_group(const address& group);
    
    // 状态
    bool is_open() const;
    address local_address() const;
};
```

## 地址解析

### 异步 DNS 查找

```cpp
// 将主机名解析为地址
address addr = co_await async_resolve(ctx, "example.com", 80);

// 使用服务名称解析
address addr = co_await async_resolve(ctx, "example.com", "http");

// 仅 IPv4
address addr = co_await async_resolve(ctx, "example.com", 80, address_family::ipv4);

// 仅 IPv6
address addr = co_await async_resolve(ctx, "example.com", 80, address_family::ipv6);
```

### 地址构造

```cpp
// IPv4
address addr = address::from_ipv4("192.168.1.1", 8080);
address addr = address::any_ipv4(8080);  // 0.0.0.0:8080
address addr = address::loopback_ipv4(8080);  // 127.0.0.1:8080

// IPv6
address addr = address::from_ipv6("::1", 8080);
address addr = address::any_ipv6(8080);  // [::]:8080
address addr = address::loopback_ipv6(8080);  // [::1]:8080

// 从字符串
address addr = address::from_string("192.168.1.1:8080");
address addr = address::from_string("[::1]:8080");
```

### Address API

```cpp
class address {
public:
    // 构造
    static address from_ipv4(std::string_view ip, uint16_t port);
    static address from_ipv6(std::string_view ip, uint16_t port);
    static address from_string(std::string_view str);
    static address any_ipv4(uint16_t port);
    static address any_ipv6(uint16_t port);
    static address loopback_ipv4(uint16_t port);
    static address loopback_ipv6(uint16_t port);
    
    // 属性
    std::string to_string() const;
    std::string ip() const;
    uint16_t port() const;
    address_family family() const;  // ipv4 或 ipv6
    
    // 比较
    bool operator==(const address& other) const;
    auto operator<=>(const address& other) const;
};
```

## 错误处理

### 连接错误

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

### I/O 错误

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

## 高级模式

### 连接池

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

### 超时模式

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

### 重试模式

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

### 分散-聚集 I/O

```cpp
task<void> scatter_gather_example(tcp_socket& socket) {
    // 聚集：一次调用发送多个缓冲区
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

### 保持连接

```cpp
task<void> keep_alive_connection(io_context& ctx, tcp_socket& socket) {
    socket.set_keepalive(true);
    
    // 发送定期心跳
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

## 性能提示

### 1. 使用 Nodelay 实现低延迟

```cpp
socket.set_nodelay(true);  // 禁用 Nagle 算法
```

### 2. 调整缓冲区大小

```cpp
socket.set_recv_buffer_size(256 * 1024);  // 256 KB
socket.set_send_buffer_size(256 * 1024);
```

### 3. 重用地址

```cpp
acceptor.set_reuse_address(true);
acceptor.set_reuse_port(true);  // 仅 Linux
```

### 4. 使用连接池

通过重用连接避免连接开销。

### 5. 批量小写入

```cpp
// 不好：许多小写入
for (const auto& msg : messages) {
    co_await socket.async_send(msg.data(), msg.size());
}

// 好：批量为更大的写入
std::string batch;
for (const auto& msg : messages) {
    batch += msg;
}
co_await socket.async_send(batch.data(), batch.size());
```

## 平台差异

### Windows (IOCP)
- 带完成端口的重叠 I/O
- 出色的可扩展性（100K+ 连接）
- AcceptEx 用于高性能接受

### Linux (io_uring)
- 现代异步 I/O 接口
- 零拷贝操作
- 在最新内核上性能最佳（5.10+）

### Linux (epoll)
- io_uring 不可用时的后备方案
- 边缘触发模式以提高效率
- 广泛支持

### macOS (kqueue)
- BSD 内核事件通知
- 良好的性能
- 类似于 epoll

## 下一步

- **[HTTP 服务器](http.md)** - 构建 REST API
- **[WebSocket](websocket.md)** - 实时通信
- **[MQTT](mqtt.md)** - IoT 消息传递
- **[MySQL](mysql.md)** - 数据库访问
