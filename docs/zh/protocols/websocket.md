# WebSocket

使用 WebSocket 支持实现实时双向通信。

## 服务端 WebSocket

### 基础回显服务器

```cpp
import cnetmod;
import cnetmod.protocol.http;
import cnetmod.protocol.websocket;
using namespace cnetmod;

int main() {
    net_init guard;
    io_context ctx;
    
    http::server srv(ctx);
    
    srv.get("/ws", [](http::request_context& ctx) -> task<void> {
        // Check if WebSocket upgrade request
        if (!ctx.is_websocket_upgrade()) {
            ctx.resp().set_status(http::status::bad_request);
            co_return;
        }
        
        // Upgrade to WebSocket
        auto ws = co_await ctx.upgrade_to_websocket();
        std::println("WebSocket client connected");
        
        // Echo loop
        while (true) {
            auto msg = co_await ws.recv();
            if (!msg) break;  // Connection closed
            
            std::println("Received: {}", *msg);
            co_await ws.send(*msg);
        }
        
        std::println("WebSocket client disconnected");
    });
    
    srv.listen(8080);
    std::println("WebSocket server on ws://localhost:8080/ws");
    ctx.run();
}
```

### 广播服务器

```cpp
class broadcast_server {
    std::vector<ws::connection*> clients_;
    mutex clients_mtx_;
    
public:
    task<void> handle_client(ws::connection ws) {
        // Add to clients
        {
            auto guard = co_await clients_mtx_.scoped_lock();
            clients_.push_back(&ws);
        }
        
        // Receive messages
        while (true) {
            auto msg = co_await ws.recv();
            if (!msg) break;
            
            // Broadcast to all clients
            co_await broadcast(*msg);
        }
        
        // Remove from clients
        {
            auto guard = co_await clients_mtx_.scoped_lock();
            std::erase(clients_, &ws);
        }
    }
    
    task<void> broadcast(std::string_view msg) {
        auto guard = co_await clients_mtx_.scoped_lock();
        
        for (auto* client : clients_) {
            try {
                co_await client->send(msg);
            } catch (...) {
                // Client disconnected
            }
        }
    }
};
```

## WebSocket API

### 连接

```cpp
class ws::connection {
public:
    // Receive message
    task<std::optional<std::string>> recv();
    
    // Send text message
    task<void> send(std::string_view text);
    
    // Send binary message
    task<void> send_binary(std::span<const uint8_t> data);
    
    // Ping/Pong
    task<void> ping(std::string_view data = "");
    task<void> pong(std::string_view data = "");
    
    // Close connection
    task<void> close(uint16_t code = 1000, std::string_view reason = "");
    
    // Status
    bool is_open() const;
};
```

### 消息类型

```cpp
srv.get("/ws", [](http::request_context& ctx) -> task<void> {
    auto ws = co_await ctx.upgrade_to_websocket();
    
    while (true) {
        auto frame = co_await ws.recv_frame();
        if (!frame) break;
        
        switch (frame->opcode) {
        case ws::opcode::text:
            std::println("Text: {}", frame->payload);
            break;
            
        case ws::opcode::binary:
            std::println("Binary: {} bytes", frame->payload.size());
            break;
            
        case ws::opcode::ping:
            co_await ws.pong(frame->payload);
            break;
            
        case ws::opcode::pong:
            std::println("Pong received");
            break;
            
        case ws::opcode::close:
            std::println("Close frame received");
            co_return;
        }
    }
});
```

## 高级特性

### 心跳（Ping/Pong）

```cpp
task<void> heartbeat_loop(ws::connection& ws, io_context& ctx) {
    while (ws.is_open()) {
        co_await async_sleep(ctx, 30s);
        
        try {
            co_await ws.ping();
        } catch (...) {
            std::println("Heartbeat failed, connection lost");
            break;
        }
    }
}

// Usage
spawn(ctx, heartbeat_loop(ws, ctx));
```

### Per-Message Deflate

```cpp
// Enable compression
ws::options opts{
    .enable_compression = true,
    .compression_level = 6
};

auto ws = co_await ctx.upgrade_to_websocket(opts);
```

### 子协议

```cpp
srv.get("/ws", [](http::request_context& ctx) -> task<void> {
    auto requested = ctx.header("Sec-WebSocket-Protocol");
    
    ws::options opts;
    if (requested == "chat") {
        opts.subprotocol = "chat";
    } else if (requested == "binary") {
        opts.subprotocol = "binary";
    }
    
    auto ws = co_await ctx.upgrade_to_websocket(opts);
    // ...
});
```

### 连接限制

```cpp
class rate_limited_ws_server {
    semaphore connection_limit_;
    
public:
    rate_limited_ws_server(size_t max_connections)
        : connection_limit_(max_connections) {}
    
    task<void> handle_client(http::request_context& ctx) {
        auto guard = co_await connection_limit_.scoped_acquire();
        
        auto ws = co_await ctx.upgrade_to_websocket();
        
        // Handle connection...
        while (auto msg = co_await ws.recv()) {
            co_await ws.send(*msg);
        }
    }
};
```

## 聊天室示例

```cpp
class chat_room {
    struct client {
        ws::connection* conn;
        std::string username;
    };
    
    std::vector<client> clients_;
    mutex mtx_;
    
public:
    task<void> join(ws::connection& ws, std::string username) {
        {
            auto guard = co_await mtx_.scoped_lock();
            clients_.push_back({&ws, username});
        }
        
        co_await broadcast(std::format("{} joined", username));
        
        while (auto msg = co_await ws.recv()) {
            co_await broadcast(std::format("{}: {}", username, *msg));
        }
        
        {
            auto guard = co_await mtx_.scoped_lock();
            std::erase_if(clients_, [&](const client& c) {
                return c.conn == &ws;
            });
        }
        
        co_await broadcast(std::format("{} left", username));
    }
    
private:
    task<void> broadcast(std::string_view msg) {
        auto guard = co_await mtx_.scoped_lock();
        
        for (auto& client : clients_) {
            try {
                co_await client.conn->send(msg);
            } catch (...) {}
        }
    }
};
```

## 客户端（浏览器）

```javascript
const ws = new WebSocket('ws://localhost:8080/ws');

ws.onopen = () => {
    console.log('Connected');
    ws.send('Hello, Server!');
};

ws.onmessage = (event) => {
    console.log('Received:', event.data);
};

ws.onerror = (error) => {
    console.error('Error:', error);
};

ws.onclose = () => {
    console.log('Disconnected');
};
```

## 性能提示

1. **使用二进制帧**处理非文本数据
2. **启用压缩**处理大消息
3. **批量发送小消息**以减少开销
4. **使用心跳**检测死连接
5. **使用信号量限制并发连接**
6. **高效使用广播**（单锁，批量发送）

## 下一步

- **[HTTP 服务器](http.md)** - HTTP/WebSocket 集成
- **[MQTT](mqtt.md)** - 替代消息协议
- **[多核](../advanced/multi-core.md)** - 扩展 WebSocket 服务器
