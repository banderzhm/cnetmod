# CoAP

cnetmod 提供面向受限设备和网关场景的 UDP CoAP 核心栈。实现按 `cnetmod.protocol.coap` 模块拆成协议类型、数据报编解码、客户端、服务端/路由器。

## 公开入口

应用代码优先导入聚合模块：

```cpp
import std;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.protocol.coap;
```

常用入口集中在 `cnetmod::coap`：

- `coap::client`：UDP CoAP 客户端，提供 `get()`、`post()`、`put()`、`delete_()`、`observe()`、`cancel_observe()`、`get_blockwise()`、`post_blockwise()`、`put_blockwise()`
- `coap::server`：UDP CoAP 服务端，提供 `listen()`、`route()`、`register_resource()`、`run()`、`notify_observers()`、`stop()`
- `coap::text_response()` / `coap::json_response()`：快速创建文本或 JSON 响应
- `coap::payload_text()` / `coap::to_bytes()`：payload 和字符串互转

`cnetmod.protocol.coap` 会 re-export 协议类型、codec、client、server 和 facade helper。需要看接口时直接打开对应 `.cppm`；实现按同名 `.cpp` 拆分。

## 当前范围

- RFC 7252 数据报 header、token、option delta/length、payload marker 的解析和序列化
- Confirmable、non-confirmable、acknowledgement、reset 消息类型
- 标准 method 和 response code 枚举
- URI path/query、content-format、accept、observe、Block1、Block2、size 等 option 编号
- Confirmable 客户端请求重传，按 token/message-id 匹配响应
- Observe 注册、注销、通知序列号、max-age 处理和服务端订阅清理
- UDP 服务端循环，资源路由，请求 path/query 提取，ACK 规范化，对异常 confirmable 消息返回 reset
- `/.well-known/core` 资源发现，支持 `rt`、`if` 和 `obs` link-format 属性
- 服务端 Block1 上传重组、Block2 响应切片，客户端 Block2 下载组装和 Block1 上传分片
- `Proxy-Uri` 基础转发，支持 `coap://host:port/path?query`、默认端口、主机名解析和 IPv6 bracket 地址
- 未知 critical option 返回 `4.02 Bad Option`，路径存在但 method 不匹配返回 `4.05 Method Not Allowed`
- 通过 `cnetmod.core.ssl` 里的 OpenSSL DTLS client/server context 提供 CoAPS 基础能力

OSCORE 对象安全不在当前模块内。CoAP UDP 路由、Observe、资源发现、Blockwise、Proxy-Uri 和重传都由 `cnetmod.protocol.coap` 提供。

## 服务端

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

路由器会规范化路径，`sensors/temp` 和 `/sensors/temp` 都会映射为 `/sensors/temp`。未安装 handler 时返回 `5.01 Not Implemented`；路由不存在时返回 `4.04 Not Found`。

## Observe

```cpp
auto initial = co_await client.observe(*remote, "/sensors/temp",
    [](const coap::message& notification) {
        std::string body(reinterpret_cast<const char*>(notification.payload.data()),
            notification.payload.size());
    },
    std::chrono::seconds(30));
```

服务端路由会自动注册带 `Observe: 0` 的 `GET` 请求，并对 `Observe: 1` 执行注销。应用通过 `udp_server::notify_observers(path, message)` 发布更新。

## CoAPS / DTLS

OpenSSL 支持的 DTLS context 可通过 `ssl_context::dtls_client()` 和 `ssl_context::dtls_server()` 创建。CoAPS endpoint 使用 `coap::default_secure_port`（`5684`）。

`testing/coap_interop.py` 的 Python 互操作测试覆盖 UDP CoAP，因为 Python 标准库不提供 DTLS。DTLS context 创建和构建覆盖由 C++ 构建验证；线级 DTLS 互操作需要 aiocoap 等 Python 包并安装可用的 DTLS backend。

## 客户端

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

`udp_client` 会在调用方未提供时自动填充 message ID 和 8 字节 token。Confirmable 请求会按 `client_config::ack_timeout` 和 `client_config::max_retransmit` 重传。

## 原始编解码

自定义 transport、抓包处理或 fuzz harness 可以直接使用 codec：

```cpp
auto request = coap::make_request({
    .method_code = coap::method::post,
    .path = "/telemetry",
    .content_type = coap::content_format::json,
});

auto encoded = coap::serialize_message(request);
auto decoded = encoded ? coap::parse_message(*encoded) : std::unexpected(encoded.error());
```

codec 返回 `std::expected<..., std::error_code>`，会拒绝非法 version、token length、option extension、payload marker 和 empty-message 组合。

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

这些辅助函数实现 Block1/Block2 option 的 wire 表示。服务端会按 Block1 重组上传 payload、按 Block2 切片大响应；客户端提供 `get_blockwise()`、`post_blockwise()` 和 `put_blockwise()` 完成常用 blockwise 传输。
