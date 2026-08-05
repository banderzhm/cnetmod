# HTTP/3 / QUIC

> 基于 UDP、QUIC 和 TLS 1.3 的 HTTP/3 客户端与服务端，使用 BoringSSL QUIC API。

**imports**:

```cpp
import cnetmod.protocol.http.v3.client;
import cnetmod.protocol.http.v3.server;
import cnetmod.coro;
```

**CMake**:

```text
-DCNETMOD_ENABLE_SSL=ON
-DCNETMOD_ENABLE_QUIC=ON
-DCNETMOD_ENABLE_BORINGSSL_QUIC=ON
```

`CNETMOD_ENABLE_QUIC` 需要 BoringSSL QUIC 后端。若后端不可用，配置阶段会禁用 QUIC；不要用 OpenSSL 代替它。

## 使用边界

- HTTP/3 监听 UDP endpoint，不能与 HTTP/1.1/HTTP/2 的 TCP listener 混用。
- `http3_server` 同时支持保留兼容的同步 `server_request_handler` 与协程 `async_server_request_handler`。涉及 MySQL、Redis、gRPC 等异步 I/O 时必须使用后者，并将传入的 `cancel_token` 继续传给下游调用；禁止在任一 handler 中阻塞线程。
- 客户端放弃请求 stream（`RESET_STREAM` / `STOP_SENDING`）或连接关闭时，异步 handler 的 token 会被取消。handler 应尽快收束并返回；已取消的请求不会再写入响应。
- HTTP/3 使用 ALPN `h3`；生产环境必须使用受信任证书，并保持 `verify_certificate = true`。

## 服务端

```cpp
import std;
import cnetmod.core.address;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.protocol.http.v3.server;

namespace cn = cnetmod;
namespace h3 = cn::http::v3;

auto make_server(cn::io_context& io, cn::ssl_context& tls)
    -> std::unique_ptr<h3::http3_server>
{
    return h3::make_http3_server(io, tls,
        cn::endpoint{cn::ipv4_address::any(), 443},
        [](h3::http3_request& request, h3::http3_response& response) -> std::error_code {
            if (request.path != "/health") {
                response.status = cn::http::status::not_found;
                response.body = R"({"code":404})";
                return {};
            }
            response.headers.emplace("content-type", "application/json");
            response.body = R"({"ok":true})";
            return {};
        });
}

// 服务器对象必须在调用 start() 后保持存活。
```

## 异步业务服务端

缓存、数据库和内部 gRPC 调用使用协程 handler。请求取消会沿 `token` 传到下游，因此业务层不应忽略它。

```cpp
auto server = h3::make_http3_server(io, tls,
    cn::endpoint{cn::ipv4_address::any(), 443},
    [&profiles](h3::http3_request& request, h3::http3_response& response,
        cn::cancel_token& token)
        -> cn::task<std::expected<void, std::error_code>> {
        auto profile = co_await profiles.fetch(request.path, token);
        if (!profile)
            co_return std::unexpected(profile.error());

        response.status = cn::http::status::ok;
        response.headers.emplace("content-type", "application/json");
        response.body = encode_profile_json(*profile);
        co_return {};
    });
```

同步 handler 仍保留，适合静态或纯内存响应；两种 handler 只选其一，以 lambda 的返回类型区分。

## 客户端

```cpp
h3::http3_client client(io, tls, {
    .connect_timeout = std::chrono::seconds{5},
    .request_timeout = std::chrono::seconds{30},
    .verify_certificate = true,
    .tls_sni_host = "api.example.com",
});

if (auto connected = co_await client.connect("api.example.com", 443); connected) {
    h3::http3_request request;
    request.method = cn::http::http_method::GET;
    request.scheme = "https";
    request.host = "api.example.com";
    request.path = "/health";
    if (auto response = co_await client.send_request(request); response)
        std::println("HTTP/3 status={}, body={}", response->status, response->body);
}
co_await client.close();
```

## HTTP/3 deadline 与流取消

`http3_client` 提供普通、`cancel_token` 与 `deadline` 三种请求形式：

```cpp
cnetmod::cancel_token token;
auto by_token = co_await client.send_request(request, token);
auto by_deadline = co_await client.send_request(request,
    cnetmod::deadline::after(std::chrono::seconds{2}));
```

deadline 到期会取消本次 stream：发送 QUIC `RESET_STREAM` 和 `STOP_SENDING`，并唤醒可能被 QPACK 阻塞的等待者；连接本身仍可复用。超时标准化为 `std::errc::timed_out`，调用方取消则保留 `std::errc::operation_canceled`。

不传 token/deadline 的 `send_request(request)` 仍走原有普通热路径，不注册取消回调，也不为每个请求增加 deadline 状态。高并发基准应使用该形式；只有确有业务时间预算时才选择 deadline 重载。

## 性能要点

1. 复用同一 origin 的 `http3_client`，避免每个请求重新握手。
2. 控制 header 体积；默认 QPACK 动态表为 64 KiB、阻塞流为 100。
3. 自动重试只适用于幂等请求，避免网络抖动时重复写入。
4. HTTP/3 适合高并发、可复用连接的边缘流量；缓存旁路业务示例见 `docs/zh/http-high-performance-cache-aside.md`。
