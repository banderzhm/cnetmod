# OpenAI / Mail / DNS

> OpenAI API 异步客户端（Chat/Embedding/TTS/STT/DALL-E）、SMTP 邮件收发、异步 DNS 客户端与服务端。

**import**:
- `import cnetmod.protocol.openai;`
- `import cnetmod.protocol.mail;`
- `import cnetmod.protocol.dns;`

**CMake**:
- `-DCNETMOD_ENABLE_OPENAI=ON`
- `-DCNETMOD_ENABLE_MAIL=ON`
- `-DCNETMOD_ENABLE_DNS=ON`

**源码**:
- `src/protocol/openai/`
- `src/protocol/mail/`
- `src/protocol/dns/`

---

## Part 1: OpenAI

### 场景导航

- 我要调用 Chat Completions → [看这里](#场景chat-completions)
- 我要流式接收响应（SSE） → [看这里](#场景流式-chat-sse)
- 我要生成 Embedding 向量 → [看这里](#场景embeddings)
- 我要生成图片（DALL-E） → [看这里](#场景dall-e-图片生成)
- 我要语音合成/识别 → [看这里](#场景tts--stt)

### API 参考

#### `connect_options` — 连接配置

**签名**: `export struct connect_options`

```cpp
struct connect_options {
    std::string api_base = "https://api.openai.com/v1";
    std::string api_key;
    bool tls_verify = true;
    std::string tls_ca_file;
    int timeout_seconds = 120;
    std::vector<std::pair<std::string, std::string>> extra_headers;
};
```

#### `message` — 聊天消息

**签名**: `export struct message`

| 方法 | 签名 | 说明 |
|------|------|------|
| `user` | `static auto user(std::string_view text) -> message` | 创建用户消息 |
| `system` | `static auto system(std::string_view text) -> message` | 创建系统消息 |
| `assistant` | `static auto assistant(std::string_view text) -> message` | 创建助手消息 |
| `user_multimodal` | `static auto user_multimodal(std::vector<content_part>) -> message` | 多模态消息（Vision） |

#### `chat_request` / `chat_response` — 请求与响应

**签名**: `export struct chat_request`

```cpp
struct chat_request {
    std::string model = "gpt-4o-mini";
    std::vector<message> messages;
    double temperature = 0.7;
    int max_tokens = 4096;
    bool stream = false;
    std::vector<tool> tools;
    std::string tool_choice; // "auto" | "none" | "required"
    std::string response_format; // "" | "json_object" | "json_schema"
    std::optional<int> seed;
};
```

**签名**: `export struct chat_response`

```cpp
struct chat_response {
    std::string id;
    std::string model;
    std::vector<choice> choices;
    usage token_usage;
    auto content() const -> std::string_view; // choices[0].msg.content
};
```

#### `client` — OpenAI 客户端

**签名**: `export class client`

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit client(io_context&) noexcept` | |
| `connect` | `auto connect(connect_options) -> task<std::expected<void, std::string>>` | 连接 API |
| `chat` | `auto chat(chat_request) -> task<std::expected<chat_response, std::string>>` | Chat Completions |
| `chat_stream` | `auto chat_stream(chat_request, on_chunk_fn) -> task<std::expected<std::string, std::string>>` | SSE 流式 |
| `chat_stream_async` | `auto chat_stream_async(chat_request, async_chunk_fn) -> task<...>` | 异步回调流式 |
| `list_models` | `auto list_models() -> task<std::expected<std::vector<model_info>, std::string>>` | 列出模型 |
| `embeddings` | `auto embeddings(embedding_request) -> task<std::expected<embedding_response, std::string>>` | 向量嵌入 |
| `text_to_speech` | `auto text_to_speech(tts_request) -> task<std::expected<std::vector<std::byte>, std::string>>` | 语音合成 |
| `transcribe` | `auto transcribe(transcription_request) -> task<std::expected<transcription_response, std::string>>` | 语音转文字 |
| `translate` | `auto translate(translation_request) -> task<std::expected<transcription_response, std::string>>` | 语音翻译 |
| `create_image` | `auto create_image(image_generation_request) -> task<std::expected<image_response, std::string>>` | 生成图片 |
| `edit_image` | `auto edit_image(image_edit_request) -> task<std::expected<image_response, std::string>>` | 编辑图片 |
| `create_image_variation` | `auto create_image_variation(image_variation_request) -> task<...>` | 图片变体 |
| `moderate` | `auto moderate(moderation_request) -> task<std::expected<moderation_response, std::string>>` | 内容审核 |

#### 多模态与 Function Calling 类型

```cpp
export struct content_part {
    std::string type;           // "text" | "image_url"
    std::string text;
    image_url_detail image_url;
    static auto make_text(std::string_view) -> content_part;
    static auto make_image_url(std::string_view url, std::string_view detail = "auto") -> content_part;
    static auto make_image_base64(std::string_view data, std::string_view media_type, std::string_view detail) -> content_part;
};

export struct tool_call { std::string id; std::string type; function_call function; };
export struct tool { std::string type; std::string function_name; std::string function_description; json function_parameters; };
```

### 场景：Chat Completions

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.openai;

namespace cn = cnetmod;

auto run(cn::io_context& ctx) -> cn::task<void> {
    cn::openai::client client(ctx);
    co_await client.connect({.api_key = "sk-..."});

    cn::openai::chat_request req{
        .model = "gpt-4o-mini",
        .messages = {
            cn::openai::message::system("You are a helpful assistant."),
            cn::openai::message::user("What is C++23?"),
        },
        .temperature = 0.7,
        .max_tokens = 512,
    };

    auto resp = co_await client.chat(req);
    if (resp) {
        std::println("Reply: {}", resp->content());
        std::println("Tokens: prompt={}, completion={}",
            resp->token_usage.prompt_tokens, resp->token_usage.completion_tokens);
    }
    ctx.stop();
}

auto main() -> int {
    cn::net_init net;
    auto ctx = cn::make_io_context();
    cn::spawn(*ctx, run(*ctx));
    ctx->run();
}
```

### 场景：流式 Chat（SSE）

```cpp
cn::openai::chat_request req{
    .model = "gpt-4o-mini",
    .messages = {cn::openai::message::user("Explain async programming")},
    .stream = true,
};

// 同步回调版本
auto full = co_await client.chat_stream(req, [](const cn::openai::chat_chunk& chunk) {
    std::print("{}", chunk.delta_content);
});

// 异步回调版本（可在回调中 co_await）
auto full = co_await client.chat_stream_async(req,
    [](const cn::openai::chat_chunk& chunk) -> cn::task<bool> {
        std::print("{}", chunk.delta_content);
        co_return true; // return false to abort
    });
```

### 场景：Embeddings

```cpp
cn::openai::embedding_request req{
    .model = "text-embedding-3-small",
    .input = {"Hello world", "C++ modules"},
    .dimensions = 512,
};
auto resp = co_await client.embeddings(req);
if (resp) {
    for (auto& d : resp->data) {
        std::println("Embedding[{}] size={}", d.index, d.embedding.size());
    }
}
```

### 场景：DALL-E 图片生成

```cpp
cn::openai::image_generation_request req{
    .model = "dall-e-3",
    .prompt = "A futuristic cityscape at sunset",
    .quality = "hd",
    .size = "1792x1024",
};
auto resp = co_await client.create_image(req);
if (resp && !resp->data.empty()) {
    std::println("Image URL: {}", resp->data[0].url);
}
```

### 场景：TTS / STT

```cpp
// TTS: 文字转语音
cn::openai::tts_request tts{
    .model = "tts-1",
    .input = "Hello, this is a test.",
    .voice = "alloy",
    .response_format = "mp3",
};
auto audio = co_await client.text_to_speech(tts);

// STT: 语音转文字
cn::openai::transcription_request stt{
    .file = audio_bytes,
    .filename = "audio.mp3",
    .language = "en",
};
auto transcript = co_await client.transcribe(stt);
if (transcript) std::println("Text: {}", transcript->text);
```

---

## Part 2: Mail (SMTP)

### 场景导航

- 我要发送邮件 → [看这里](#场景smtp-发送邮件)
- 我要搭建 SMTP 服务端 → [看这里](#场景smtp-服务端)

### API 参考

#### `message` — 邮件消息

**签名**: `export struct message`（`cnetmod::mail` 命名空间）

```cpp
struct message {
    using header = std::pair<std::string, std::string>;
    std::vector<header> headers;
    std::string body;
    void set_header(std::string name, std::string value);
    auto header_value(std::string_view name) const -> std::optional<std::string_view>;
};
```

#### `envelope` — 邮件信封

**签名**: `export struct envelope`

```cpp
struct envelope {
    std::string sender;
    std::vector<std::string> recipients;
    void add_recipient(std::string recipient);
};
```

#### `client` — SMTP 客户端

**签名**: `export class client`（`cnetmod::mail::client`）

```cpp
struct client_options {
    bool tls = false;       // SMTPS (port 465)
    bool starttls = false;  // STARTTLS 升级
    std::string hostname;
    std::uint16_t port = 25;
    bool verify = true;
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `explicit client(io_context&, client_options = {}) noexcept` | |
| `connect` | `auto connect(string_view host, uint16_t port = 0) -> task<std::expected<void, std::string>>` | 连接并 EHLO |
| `authenticate` | `auto authenticate(string_view user, string_view pass, auth_mechanism = plain) -> task<...>` | 认证 |
| `send` | `auto send(const envelope&, const message&) -> task<std::expected<void, std::string>>` | 发送邮件 |
| `quit` | `auto quit() -> task<std::expected<void, std::string>>` | 退出 |
| `close` | `void close() noexcept` | 关闭连接 |

支持的认证机制：`plain`, `login`, `cram_md5`, `xoauth2`, `oauthbearer`, `external`

#### `server` — SMTP 服务端

**签名**: `export class server`（`cnetmod::mail::server`）

```cpp
struct server_options {
    std::string hostname = "localhost";
    std::size_t max_message_size = 25U * 1024U * 1024U;
    std::size_t max_recipients = 100;
    bool require_auth = false;
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `server(io_context&, server_options = {})` | |
| `listen` | `auto listen(string_view host, uint16_t port) -> std::expected<void, std::error_code>` | 监听端口 |
| `set_message_handler` | `void set_message_handler(recipient_handler)` | 设置消息处理器 |
| `set_authenticator` | `void set_authenticator(authenticator)` | 设置认证回调 |
| `run` | `auto run() -> task<void>` | 启动服务 |
| `stop` | `void stop()` | 停止服务 |

### 场景：SMTP 发送邮件

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.mail;

namespace cn = cnetmod;

auto run(cn::io_context& ctx) -> cn::task<void> {
    cn::mail::client client(ctx, {
        .tls = true,
        .hostname = "smtp.example.com",
        .port = 465,
    });
    co_await client.connect("smtp.example.com", 465);
    co_await client.authenticate("user@example.com", "password");

    cn::mail::envelope env;
    env.sender = "user@example.com";
    env.add_recipient("recipient@example.com");

    cn::mail::message msg;
    msg.set_header("From", "user@example.com");
    msg.set_header("To", "recipient@example.com");
    msg.set_header("Subject", "Hello from cnetmod");
    msg.body = "This is a test email sent via cnetmod SMTP client.";

    auto result = co_await client.send(env, msg);
    if (result) std::println("Email sent successfully!");

    co_await client.quit();
    ctx.stop();
}
```

### 场景：SMTP 服务端

```cpp
auto run_server(cn::io_context& ctx) -> cn::task<void> {
    cn::mail::server server(ctx, {.hostname = "mail.example.com"});
    server.set_message_handler(
        [](const cn::mail::envelope& env, const cn::mail::message& msg)
            -> cn::task<std::expected<void, std::error_code>> {
            std::println("Received mail from {} to {}", env.sender, env.recipients[0]);
            co_return std::expected<void, std::error_code>{};
        });
    server.listen("0.0.0.0", 2525);
    co_await server.run();
}
```

---

## Part 3: DNS

### 场景导航

- 我要异步解析域名 → [看这里](#场景dns-客户端查询)
- 我要搭建 DNS 服务端 → [看这里](#场景dns-服务端)
- 我要使用 DoH / DoT → [看这里](#场景doh--dot)

### API 参考

#### DNS 类型

**签名**: `export enum class record_type : std::uint16_t` — `A`(1), `NS`(2), `CNAME`(5), `SOA`(6), `PTR`(12), `MX`(15), `TXT`(16), `AAAA`(28), `SRV`(33), `HTTPS`(65)

**签名**: `export enum class response_code : std::uint8_t` — `no_error`(0), `format_error`(1), `server_failure`(2), `name_error`(3), `refused`(5)

```cpp
export struct question { std::string name; record_type type; record_class cls; };
export struct resource_record { std::string name; record_type type; record_class cls; std::uint32_t ttl; std::vector<std::byte> data; };
export struct message {
    std::uint16_t id;
    bool query; bool recursion_desired;
    response_code rcode;
    std::vector<question> questions;
    std::vector<resource_record> answers;
    std::vector<resource_record> authorities;
    std::vector<resource_record> additionals;
};
```

#### DNS Codec

```cpp
auto parse_message(std::span<const std::byte>) -> std::expected<message, std::error_code>;
auto serialize_message(const message&) -> std::expected<std::vector<std::byte>, std::error_code>;
auto make_query(std::string_view name, record_type, uint16_t id = 0) -> message;
auto a_record(std::string_view name, const ipv4_address&, uint32_t ttl = 60) -> resource_record;
auto aaaa_record(std::string_view name, const ipv6_address&, uint32_t ttl = 60) -> resource_record;
auto txt_record(std::string_view name, std::string_view text, uint32_t ttl = 60) -> std::expected<resource_record, std::error_code>;
auto cname_record(std::string_view name, std::string_view canonical, uint32_t ttl = 60) -> std::expected<resource_record, std::error_code>;
```

#### `udp_client` / `tcp_client` — DNS 客户端

**签名**: `export class udp_client` / `export class tcp_client`（`cnetmod::dns` 命名空间）

| 方法 | 签名 | 说明 |
|------|------|------|
| `udp_client::query` | `auto query(const endpoint& server, const message&) -> task<std::expected<message, std::error_code>>` | UDP 查询 |
| `tcp_client::query` | `auto query(string_view host, uint16_t port, const message&) -> task<...>` | TCP 查询 |

#### `doh_client` / `dot_client` — 加密 DNS

**签名**: `export class doh_client`（DNS over HTTPS）

```cpp
explicit doh_client(io_context&, std::string endpoint_url = "https://dns.google/dns-query");
auto query(const message&) -> task<std::expected<message, std::error_code>>;
```

**签名**: `export class dot_client`（DNS over TLS，需 `CNETMOD_HAS_SSL`）

```cpp
explicit dot_client(io_context&);
auto query(string_view host, uint16_t port, const message&) -> task<...>;
```

#### `udp_server` / `tcp_server` — DNS 服务端

**签名**: `export class udp_server` / `export class tcp_server`（`cnetmod::dns` 命名空间）

| 方法 | 签名 | 说明 |
|------|------|------|
| `listen` | `auto listen(string_view host, uint16_t port, socket_options) -> std::expected<void, std::error_code>` | 监听 |
| `set_handler` | `void set_handler(query_handler)` | 设置查询处理器 |
| `run` | `auto run() -> task<void>` | 启动服务 |
| `stop` | `void stop() noexcept` | 停止 |

`dot_server` 需额外传入 `dot_server_options{.cert_file, .key_file, .verify_peer}`。

### 场景：DNS 客户端查询

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.dns;

namespace cn = cnetmod;

auto run(cn::io_context& ctx) -> cn::task<void> {
    cn::dns::udp_client client(ctx);
    auto server = cn::endpoint{cn::ip_address{cn::ipv4_address{8,8,8,8}}, 53};

    auto query = cn::dns::make_query("example.com", cn::dns::record_type::A, 1);
    auto resp = co_await client.query(server, query);
    if (resp) {
        for (auto& rr : resp->answers) {
            std::println("Answer: {} TTL={}", rr.name, rr.ttl);
        }
    }
    ctx.stop();
}
```

### 场景：DoH / DoT

```cpp
// DNS over HTTPS
cn::dns::doh_client doh(ctx, "https://dns.google/dns-query");
auto query = cn::dns::make_query("example.com", cn::dns::record_type::A);
auto resp = co_await doh.query(query);

// DNS over TLS (需 -DCNETMOD_ENABLE_SSL=ON)
cn::dns::dot_client dot(ctx);
auto resp2 = co_await dot.query("dns.google", 853, query);
```

### 场景：DNS 服务端

```cpp
auto run_dns_server(cn::io_context& ctx) -> cn::task<void> {
    cn::dns::udp_server server(ctx);
    server.set_handler([](const cn::dns::message& query, const cn::endpoint& peer)
        -> cn::task<cn::dns::message> {
        cn::dns::message resp;
        resp.id = query.id;
        resp.query = false;
        resp.recursion_desired = true;
        resp.recursion_available = true;
        for (auto& q : query.questions) {
            if (q.type == cn::dns::record_type::A && q.name == "example.com") {
                resp.answers.push_back(
                    cn::dns::a_record("example.com", cn::ipv4_address{93,184,216,34}));
            }
        }
        co_return resp;
    });
    server.listen("0.0.0.0", 5353);
    co_await server.run();
}
```

## Do's & Don'ts

- **Do**: OpenAI 客户端支持自动重连，连接断开后下次调用会自动 reconnect
- **Do**: SMTP 发送邮件时根据服务端要求选择 `tls`（端口 465）或 `starttls`（端口 587）
- **Do**: DNS 查询使用 `make_query` 构建标准查询，避免手动构造 message
- **Don't**: 不要在 OpenAI `chat_stream` 回调中执行耗时操作，会阻塞 SSE 解析
- **Don't**: DNS `udp_client` 单次查询限制 512 字节，大响应需用 `tcp_client`
