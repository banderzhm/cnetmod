# HTTP/3 / QUIC

> 基于 UDP、QUIC 和 TLS 1.3 的 HTTP/3 客户端与服务端，使用 BoringSSL QUIC API。

**imports**:

```cpp
import cnetmod.protocol.http.v3.client;
import cnetmod.protocol.http.v3.server;
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
- `http3_server` 的 `server_request_handler` 是同步回调：适合纯内存、快速响应的路径；不能直接 `co_await` MySQL、Redis 等异步 I/O，也不能阻塞。
- 需要缓存或数据库的业务 API 当前应继续由 `http::server` 承载，直至 HTTP/3 异步业务适配层就绪。
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

## 性能要点

1. 复用同一 origin 的 `http3_client`，避免每个请求重新握手。
2. 控制 header 体积；默认 QPACK 动态表为 64 KiB、阻塞流为 100。
3. 自动重试只适用于幂等请求，避免网络抖动时重复写入。
4. HTTP/3 适合高并发、可复用连接的边缘流量；缓存旁路业务示例见 `docs/zh/http-high-performance-cache-aside.md`。
