# HTTP/3 / QUIC

基于 UDP、QUIC 和 TLS 1.3 的 HTTP/3 客户端与服务端，使用 BoringSSL QUIC API。

```cpp
import cnetmod.protocol.http.v3.client;
import cnetmod.protocol.http.v3.server;
import cnetmod.coro;
```

配置需要：

```text
-DCNETMOD_ENABLE_SSL=ON
-DCNETMOD_ENABLE_QUIC=ON
-DCNETMOD_ENABLE_BORINGSSL_QUIC=ON
```

`CNETMOD_ENABLE_QUIC` 需要 BoringSSL QUIC 后端；不要用 OpenSSL 代替它。

## 使用边界

- HTTP/3 listener 使用 UDP，不能与 HTTP/1.1/HTTP/2 的 TCP listener 混用。
- HTTP/3 使用 ALPN `h3`；生产环境保持 `verify_certificate = true`。
- 同一个 QUIC 连接可以并发承载多个 HTTP/3 request stream。
- `RESET_STREAM`、`STOP_SENDING` 或连接关闭会取消异步 handler 的 token；handler
  必须把 token 继续传给 MySQL、Redis、gRPC 等下游 I/O，不能阻塞线程。

## 服务端

```cpp
import std;
import cnetmod.core.address;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.protocol.http.v3.server;

namespace cn = cnetmod;
namespace h3 = cn::http::v3;

auto server = h3::make_http3_server(io, tls,
    cn::endpoint{cn::ipv4_address::any(), 443},
    [](h3::http3_request& request,
        h3::http3_response& response) -> std::error_code {
        if (request.path != "/health") {
            response.status = cn::http::status::not_found;
            response.body = R"({"code":404})";
            return {};
        }
        response.headers.emplace("content-type", "application/json");
        response.body = R"({"ok":true})";
        return {};
    });
```

同步 handler 适合纯内存、快速响应；涉及数据库、缓存或 RPC 时使用协程 handler：

```cpp
h3::async_server_request_handler handler =
    [&profiles](h3::http3_request& request, h3::http3_response& response,
        cn::cancel_token& token) -> cn::task<std::expected<void, std::error_code>> {
        auto profile = co_await profiles.fetch(request.path, token);
        if (!profile)
            co_return std::unexpected(profile.error());
        response.status = cn::http::status::ok;
        response.headers.emplace("content-type", "application/json");
        response.body = encode_profile_json(*profile);
        co_return {};
    };
```

### 请求体流式处理

普通 `async_server_request_handler` 保持兼容语义：handler 启动时
`request.body` 已经完整。大文件上传使用显式 `streaming_server_request_handler`；
它在 HEADERS 后启动，并通过有界 `request_body_stream` 提供背压：

```cpp
h3::streaming_server_request_handler handler =
    [](h3::http3_request& request, h3::http3_response& response,
        cn::http::request_body_stream& body, cn::cancel_token& token)
        -> cn::task<std::expected<void, std::error_code>> {
        while (!token.is_cancelled()) {
            auto chunk = co_await body.receive();
            if (!chunk)
                break; // peer FIN、取消或 stream 错误
            process_chunk(*chunk);
        }
        response.status = cn::http::status::ok;
        co_return {};
    };
```

handler 提前返回会关闭 body pump；消费者慢时，传输层不会无限制缓存上传数据。

### 响应体流式发送

服务端可设置 `response.body_source`。服务端先发送 HEADERS，再按 producer 发送
DATA，最后发送 trailers 和 FIN：

```cpp
auto index = std::make_shared<std::size_t>(0);
response.body_source = std::make_shared<cn::http::response_body_source>(
    [index](cn::cancel_token& token)
        -> cn::task<std::optional<cn::http::request_body_chunk>> {
        if (token.is_cancelled() || *index == 3)
            co_return std::nullopt;
        constexpr std::array parts{"part-a", "part-b", "part-c"};
        const auto text = parts[(*index)++];
        cn::http::request_body_chunk chunk;
        chunk.insert(chunk.end(),
            reinterpret_cast<const std::byte*>(text.data()),
            reinterpret_cast<const std::byte*>(text.data()) + text.size());
        co_return chunk;
    }, 18);
```

`body` 与 `body_source` 不能同时设置。声明的长度必须与实际发送长度一致；HEAD、
1xx、204、304 响应禁止发送 DATA。

### 显式 Server Push

Server Push 默认关闭，客户端必须在 `http3_client_options` 中设置 `max_push_id`，并提供
`on_server_push` 回调。服务端把 promise 与响应放进父响应的 `pushes`；promise 在父响应
HEADERS 前发送，push 响应可使用静态 `body` 或带背压的 `body_source`。回调只在完整 push
响应通过 QPACK、Content-Length 和 trailers 语义校验后执行。

```cpp
options.max_push_id = 0U;
options.on_server_push = [](h3::http3_request promise,
    h3::http3_response response)
    -> cn::task<std::expected<void, std::error_code>> {
    cache_asset(promise.path, response.body);
    co_return {};
};

h3::http3_push asset;
asset.request.method = cn::http::http_method::GET;
asset.request.scheme = "https";
asset.request.host = request.host;
asset.request.path = "/app.js";
asset.response = std::make_shared<h3::http3_response>(
    h3::http3_response{.status = 200, .body = "..."});
response.pushes.push_back(std::move(asset));
```

客户端可在 Push body producer 开始前检查 `PUSH_PROMISE`。回调返回错误会自动
发送 RFC 9114 `CANCEL_PUSH`，不会影响父响应；需要自行决定时，可保存回调给出的
`push_id` 并调用 `client.cancel_server_push(push_id)`。取消同时会停止本地交付，并
取消服务端 `body_source` 的 token。

```cpp
options.max_push_id = 8U;
options.on_server_push_promise = [](h3::server_push_promise promise)
    -> cn::task<std::expected<void, std::error_code>> {
    if (!cache_wants(promise.request.path))
        co_return std::unexpected(std::make_error_code(
            std::errc::operation_canceled)); // automatic CANCEL_PUSH
    co_return {};
};
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

普通 `send_request()` 在返回前收集完整 `response.body`，保持旧 API 和小响应热路径。

### 客户端响应体流式消费

大响应或文件下载使用显式 `send_request_streaming()`。收到 HEADERS 后立即调用
handler，DATA 通过有界 `request_body_stream` 交付；消费者变慢会形成 QUIC 背压：

```cpp
h3::http3_request request;
request.host = "api.example.com";
request.port = 443;
request.path = "/large-export";

std::uint64_t bytes = 0;
auto result = co_await client.send_request_streaming(
    request,
    [&bytes](h3::http3_response& response,
        cn::http::request_body_stream& body, cn::cancel_token& token)
        -> cn::task<std::expected<void, std::error_code>> {
        if (response.status != 200)
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
        while (!token.is_cancelled()) {
            auto chunk = co_await body.receive();
            if (!chunk)
                break;
            bytes += chunk->size();
            consume_chunk(*chunk);
        }
        co_return {};
    });
```

`Content-Length` 会逐块校验，trailer 保留在 `result->trailers`。handler 返回错误或
取消后，当前 stream 会被取消；流式 handler 不会在 0-RTT 被拒绝后自动重放，避免
应用已经消费响应前缀后再次执行回调。

### deadline 与取消

```cpp
cn::cancel_token token;
auto by_token = co_await client.send_request(request, token);
auto by_deadline = co_await client.send_request(request,
    cn::deadline::after(std::chrono::seconds{2}));
```

deadline 会发送 QUIC `RESET_STREAM` / `STOP_SENDING`，连接本身仍可复用。普通
`send_request(request)` 不注册取消回调，适合高并发热路径。

## WebTransport 与 HTTP Datagram

WebTransport 需要两端同时启用 QUIC Datagram、Extended CONNECT 和 WebTransport
SETTINGS。服务端使用 `async_webtransport_handler`；客户端调用
`connect_webtransport()`，然后通过 `webtransport_session` 管理双向/单向子流、
Datagram 和 `close()`。不要直接调用底层 QUIC Datagram API 绕过 session context ID。

## 0-RTT 与持久化

客户端在 TLS 实际安装 0-RTT write key 后才会创建 HTTP/3 控制、QPACK 和 request
streams；这避免 Initial flight 与应用帧抢占同一个握手阶段。该等待不需要应用层
轮询，也不会把 0-RTT 降级为先完成 1-RTT 再发送。

握手完成后延迟抵达的 QUIC Initial/Handshake UDP 包会按 RFC 9001 丢弃；它们不能
重新初始化 TLS 或覆盖已建立的 1-RTT 状态。对于被 replay cache 拒绝的 0-RTT，
仅幂等的完整响应 API 会在新的 1-RTT 连接上重放一次。

客户端可以通过 `resumption_ticket` 和 `enable_early_data` 显式启用受控 0-RTT；
自动重试仅适用于幂等请求。服务端必须配置应用拥有的 ticket AEAD 与共享 replay
cache，默认拒绝 0-RTT。Alt-Svc、ticket 文件均为 opt-in，并应放在权限受限目录。

## 性能与验证

1. 复用同一 origin 的 `http3_client`，避免每个请求重新握手。
2. 控制 header 体积；默认 QPACK 动态表为 64 KiB、阻塞流为 100。
3. 使用 `send_batch` 时同一连接上的 request stream 会并发提交；冷连接只串行化
   初始连接建立。
4. HTTP/3 E2E 覆盖 GET/POST、请求/响应流式传输、HEAD/OPTIONS、batch、取消、
   `close_async()`、0-RTT ticket 和 trailer/Content-Length 语义。

## 连接迁移与 Multipath 边界

当前实现保留 RFC 9000 的单活跃路径迁移，并提供受显式配置保护的
`draft-ietf-quic-multipath-12` 实验实现。它不是最终 RFC，浏览器和通用 QUIC 对端
通常不会协商它；未同时启用的对端始终走原有单路径语义。

Multipath 已实现并只在双端均宣告 `initial_max_path_id` 后启用：

- 每个 Path ID 独立持有 1-RTT packet number、ACK、RTT/loss、拥塞控制与 pacing；
  AEAD nonce 也包含 Path ID，Path 0 保持 RFC 9001 既有字节语义。
- 服务端对未验证的非零路径执行逐路径 3× anti-amplification 额度；PATH_CHALLENGE
  与 PATH_RESPONSE 同样受该额度约束，验证完成后才解除。
- 短头包在认证前以本地 DCID 选择路径；每条路径的 CID 序列独立，支持
  `PATH_NEW_CONNECTION_ID`、`PATH_RETIRE_CONNECTION_ID`、`MAX_PATH_ID`、
  `PATH_ACK`、状态帧和 `PATH_ABANDON` 的控制面规则。
- `quic_connection::async_probe_path(path_id, endpoint)` 只在匹配 CID 已交换后发起
  PATH_CHALLENGE，并在最多 3 PTO 内对同一 challenge 重传；其 task 只会在收到匹配的
  PATH_RESPONSE 后成功，超时返回 `timed_out`。PATH_RESPONSE 前不会在新路径调度应用数据。验证成功后调度器在
  可用、非 backup 路径间轮转；备路径仍可作为故障切换候选。`async_abandon_path()`
  发送 PATH_ABANDON、退役该路径的 peer CID，并回退到仍存活的路径；本地 CID 路由和
  未确认包保留 3 PTO 后才回收，期间未确认应用帧会重传到存活路径。
- `PATH_ACK` 始终携带被确认的 Path ID。若该路径已进入 `PATH_ABANDON` 的 3 PTO
  回收期，确认帧会由另一条存活、已验证路径承载，避免对端仅因路径切换而等待 PTO
  才释放可确认数据。
- 若当前路径的拥塞窗口暂时不足，发送器会在同一轮尝试其余已验证路径；帧会保留原有
  RFC 9218 优先级，不会因路径切换降级为 FIFO。
- 对端的 early `PATH_ABANDON` 会建立不可复用的路径 tombstone 并回送确认；迟到的
  PATH CID/ACK 帧不会重新打开路径或关闭整个连接。Path ID 的消费状态独立于 3 PTO
  资源回收而永久保留，符合 nonce 不可复用要求。
- HTTP/3 可通过 `http3_client_options::multipath_initial_max_path_id` 和
  `http3_server::set_multipath_initial_max_path_id()` 协商；客户端随后调用
  `http3_client::async_probe_path()` 指定额外远端 UDP endpoint。
- 若需要真实多本地网卡/端口路径，使用三参数重载：
  `co_await client.async_probe_path(path_id, remote, local)`。`local` 会绑定独立
   UDP socket；HTTP/3 client 持有该 socket、接收协程、取消令牌和 completion，并在
   `close()` 中先取消、关闭、等待所有 receiver 退出后才释放 QUIC connection。应用层
  不需要也不应自行启动捕获连接裸指针的 receive loop。正在进行的
  `async_probe_path()` 也持有 connection 所有权；`close()` 会使其以取消结果收束，
  不会让悬挂 probe 访问已释放的 transport。多个协程同时调用 `close()` 会由 client
  内部协程锁串行化：只有第一个调用回收 driver/receiver，后续调用在回收完成后幂等返回。
  请求、流式响应、Push 取消和 WebTransport 建立同样会在跨 `co_await` 前保留 session
  与 transport 所有权；因此可与 `close()` 并发执行并以连接关闭错误收束。这个保障不会把
  HTTP/3 request stream 串行化，正常多路复用的吞吐不受生命周期锁影响。
- 逐路径 Datagram PLPMTUD 为显式 opt-in：客户端设置
  `http3_client_options::enable_path_mtu_discovery`，服务端在 `start()` 前调用
  `set_path_mtu_discovery()`。每条已验证路径从 1200 字节开始，只有 padded PING
  被 ACK 后才以 128 字节步长提升发送上限；探测丢失不会降低其他路径的上限。
- Windows IOCP 上，未监听 UDP endpoint 的 ICMP port-unreachable 会作为异步完成错误返回。
  cnetmod 将其归类为候选路径丢失，不会关闭整条 QUIC 连接；`async_probe_path()` 仍会在
  3 PTO 后返回 `timed_out`，已验证的其他路径继续服务。E2E 覆盖成功验证、独立本地
  socket、PMTU 提升、PMTU 黑洞（维持 1200 字节并继续传输），以及验证超时后的 Path 0
  回退。
- `http3_client_options::path_mtu_initial_probe_delay` 与服务端
  `set_path_mtu_discovery(maximum_payload, probe_interval, initial_probe_delay)` 可为新验证
  路径延后第一次 PMTU 探测；默认 `0ms`，维持立即探测语义。延迟只影响首个 padded-PING
  探测，后续成功或失败重试仍由 `path_mtu_probe_interval` 控制。纯 PMTU 探测和不可靠
  DATAGRAM 在 PTO 后都会从恢复状态中退役，避免没有可重传帧时重复触发同一个已到期
  deadline。

当前实现支持同一 socket 的多远端路径，也支持通过三参数 `async_probe_path()` 创建并
管理独立本地 UDP socket 的真实多四元组路径。自动网卡选择策略、长期弱网 soak 及第三方
Multipath 草案对端互操作仍需独立完成，不能宣称为通用浏览器互操作能力。
