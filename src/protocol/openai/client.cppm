/// cnetmod.protocol.openai:client — 异步 OpenAI Chat Completions 客户端
/// 支持 SSL/TLS、流式 SSE、连接复用
/// 复用 cnetmod 的 ssl_context / ssl_stream / http::request / http::response_parser

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:client;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.protocol.http;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :types;
import nlohmann.json;

namespace cnetmod::openai {

// =============================================================================
// client — 异步 OpenAI 客户端
// =============================================================================

export class client {
public:
    explicit client(io_context& ctx) noexcept : ctx_(ctx) {}

    ~client() { close(); }

    client(const client&) = delete;
    auto operator=(const client&) -> client& = delete;

    // ── 连接 ──────────────────────────────────────────────────

    /// 连接到 OpenAI API endpoint (TCP + TLS 握手)
    auto connect(connect_options opts) -> task<std::expected<void, std::string>> {
        opts_ = std::move(opts);

        // 解析 API base URL
        auto url_r = http::url::parse(opts_.api_base);
        if (!url_r)
            co_return std::unexpected("invalid api_base URL: " + url_r.error());

        url_ = std::move(*url_r);
        bool use_tls = (url_.scheme == "https");

        // DNS 解析 — 先尝试直接 IP，失败则异步 getaddrinfo
        auto addr_r = ip_address::from_string(url_.host);
        if (!addr_r) {
            auto dns_r = co_await async_resolve(ctx_, url_.host);
            if (!dns_r || dns_r->empty())
                co_return std::unexpected("cannot resolve host: " + url_.host);
            addr_r = ip_address::from_string((*dns_r)[0]);
            if (!addr_r)
                co_return std::unexpected("invalid resolved address");
        }

        auto family = addr_r->is_v4() ? address_family::ipv4 : address_family::ipv6;
        auto sock_r = socket::create(family, socket_type::stream);
        if (!sock_r)
            co_return std::unexpected(std::string("socket create failed"));
        sock_ = std::move(*sock_r);

        auto cr = co_await async_connect(ctx_, sock_, endpoint{*addr_r, url_.port});
        if (!cr) {
            sock_.close();
            co_return std::unexpected("connect failed: " + cr.error().message());
        }

        // TLS 握手
#ifdef CNETMOD_HAS_SSL
        if (use_tls) {
            auto ssl_ctx_r = ssl_context::client();
            if (!ssl_ctx_r) {
                sock_.close();
                co_return std::unexpected("ssl context: " + ssl_ctx_r.error().message());
            }
            ssl_ctx_ = std::make_unique<ssl_context>(std::move(*ssl_ctx_r));
            ssl_ctx_->set_verify_peer(opts_.tls_verify);

            if (!opts_.tls_ca_file.empty()) {
                auto r = ssl_ctx_->load_ca_file(opts_.tls_ca_file);
                if (!r) { sock_.close(); co_return std::unexpected("ssl ca: " + r.error().message()); }
            } else if (opts_.tls_verify) {
                (void)ssl_ctx_->set_default_ca();
            }

            ssl_ = std::make_unique<ssl_stream>(*ssl_ctx_, ctx_, sock_);
            ssl_->set_connect_state();
            ssl_->set_hostname(url_.host);

            auto hs = co_await ssl_->async_handshake();
            if (!hs) {
                sock_.close();
                co_return std::unexpected("ssl handshake: " + hs.error().message());
            }
        }
#else
        if (use_tls) {
            sock_.close();
            co_return std::unexpected(std::string("SSL not available (compiled without OpenSSL)"));
        }
#endif

        connected_ = true;
        co_return std::expected<void, std::string>{};
    }

    /// 是否已连接
    [[nodiscard]] auto is_connected() const noexcept -> bool {
        return connected_ && sock_.is_open();
    }

    /// 关闭连接
    void close() noexcept {
#ifdef CNETMOD_HAS_SSL
        ssl_.reset();
        ssl_ctx_.reset();
#endif
        sock_.close();
        connected_ = false;
    }

    // ── 非流式对话 ────────────────────────────────────────────

    /// 非流式 Chat Completions — 发送请求，等待完整响应
    auto chat(chat_request req)
        -> task<std::expected<chat_response, std::string>>
    {
        if (auto r = co_await ensure_connected(); !r) co_return std::unexpected(r.error());

        req.stream = false;
        auto body = req.to_json();

        http::request http_req(http::http_method::POST, build_path("/chat/completions"));
        apply_common_headers(http_req, "application/json");
        http_req.set_body(std::move(body));

        auto send_r = co_await send_http_request(http_req);
        if (!send_r)
            co_return std::unexpected(send_r.error());

        // 读取完整响应
        auto resp_r = co_await read_full_response();
        if (!resp_r)
            co_return std::unexpected(resp_r.error());

        auto& [status, resp_body] = *resp_r;
        if (status != 200) {
            auto err = error_response::from_json(resp_body);
            co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
        }

        co_return chat_response::from_json(resp_body);
    }

    // ── 流式对话 (SSE) ───────────────────────────────────────

    /// 流式 Chat Completions — SSE 逐块回调，返回完整拼接结果
    auto chat_stream(chat_request req, on_chunk_fn on_chunk)
        -> task<std::expected<std::string, std::string>>
    {
        if (auto r = co_await ensure_connected(); !r) co_return std::unexpected(r.error());

        req.stream = true;
        auto body = req.to_json();

        http::request http_req(http::http_method::POST, build_path("/chat/completions"));
        apply_common_headers(http_req, "text/event-stream");
        http_req.set_body(std::move(body));

        auto send_r = co_await send_http_request(http_req);
        if (!send_r)
            co_return std::unexpected(send_r.error());

        // 读取 HTTP 响应头
        auto header_r = co_await read_response_header();
        if (!header_r)
            co_return std::unexpected(header_r.error());

        auto& [status, content_type] = *header_r;
        if (status != 200) {
            auto err_body = co_await read_remaining_body();
            auto err = error_response::from_json(err_body);
            co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
        }

        // SSE 流处理: 增量读取 + 逐行解析
        std::string full_content;
        std::string line_buf;

        for (;;) {
            auto read_r = co_await do_read_some();
            if (!read_r || read_r->empty()) break;

            rbuf_.append(*read_r);

            // 从 rbuf_ 中逐行提取 SSE 事件
            while (true) {
                auto nl = rbuf_.find('\n');
                if (nl == std::string::npos) break;

                auto line = rbuf_.substr(0, nl);
                // 去除 \r
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                rbuf_.erase(0, nl + 1);

                if (line.empty()) continue;  // SSE 事件间的空行

                if (line.starts_with("data: ")) {
                    auto data = line.substr(6);

                    if (data == "[DONE]") {
                        co_return full_content;
                    }

                    auto chunk = chat_chunk::from_json(data);
                    if (!chunk.delta_content.empty()) {
                        full_content += chunk.delta_content;
                    }
                    if (on_chunk)
                        on_chunk(chunk);

                    if (chunk.finish_reason == "stop" ||
                        chunk.finish_reason == "length") {
                        co_return full_content;
                    }
                }
            }
        }

        co_return full_content;
    }

    // ── 流式对话 (异步回调) ──────────────────────────

    /// 异步回调类型: (chunk) -> task<bool>, 返回 false 中止流
    using async_chunk_fn = std::function<task<bool>(const chat_chunk&)>;

    /// 流式 Chat Completions — 异步回调版本，支持在回调中 co_await
    auto chat_stream_async(chat_request req, async_chunk_fn on_chunk)
        -> task<std::expected<std::string, std::string>>
    {
        if (auto r = co_await ensure_connected(); !r) co_return std::unexpected(r.error());

        req.stream = true;
        auto body = req.to_json();

        http::request http_req(http::http_method::POST, build_path("/chat/completions"));
        apply_common_headers(http_req, "text/event-stream");
        http_req.set_body(std::move(body));

        auto send_r = co_await send_http_request(http_req);
        if (!send_r)
            co_return std::unexpected(send_r.error());

        auto header_r = co_await read_response_header();
        if (!header_r)
            co_return std::unexpected(header_r.error());

        auto& [status, content_type] = *header_r;
        if (status != 200) {
            auto err_body = co_await read_remaining_body();
            auto err = error_response::from_json(err_body);
            co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
        }

        std::string full_content;

        for (;;) {
            auto read_r = co_await do_read_some();
            if (!read_r || read_r->empty()) break;

            rbuf_.append(*read_r);

            while (true) {
                auto nl = rbuf_.find('\n');
                if (nl == std::string::npos) break;

                auto line = rbuf_.substr(0, nl);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                rbuf_.erase(0, nl + 1);

                if (line.empty()) continue;

                if (line.starts_with("data: ")) {
                    auto data = line.substr(6);

                    if (data == "[DONE]") {
                        co_return full_content;
                    }

                    auto chunk = chat_chunk::from_json(data);
                    if (!chunk.delta_content.empty()) {
                        full_content += chunk.delta_content;
                    }

                    if (on_chunk) {
                        bool cont = co_await on_chunk(chunk);
                        if (!cont) co_return full_content;
                    }

                    if (chunk.finish_reason == "stop" ||
                        chunk.finish_reason == "length") {
                        co_return full_content;
                    }
                }
            }
        }

        co_return full_content;
    }

    // ── Models API ─────────────────────────────────────

    /// 列出可用模型
    auto list_models()
        -> task<std::expected<std::vector<model_info>, std::string>>
    {
        if (auto r = co_await ensure_connected(); !r) co_return std::unexpected(r.error());

        http::request http_req(http::http_method::GET, build_path("/models"));
        apply_common_headers(http_req, "application/json");

        auto send_r = co_await send_http_request(http_req);
        if (!send_r) co_return std::unexpected(send_r.error());

        auto resp_r = co_await read_full_response();
        if (!resp_r) co_return std::unexpected(resp_r.error());

        auto& [status, resp_body] = *resp_r;
        if (status != 200) {
            auto err = error_response::from_json(resp_body);
            co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
        }

        std::vector<model_info> models;
        auto j = nlohmann::json::parse(resp_body, nullptr, false);
        if (!j.is_discarded() && j.contains("data") && j["data"].is_array()) {
            for (auto& m : j["data"]) {
                model_info info;
                info.id       = m.value("id", "");
                info.owned_by = m.value("owned_by", "");
                info.created  = m.value("created", 0);
                models.push_back(std::move(info));
            }
        }
        co_return models;
    }

    // ── Embeddings API ─────────────────────────────────────

    /// 创建嵌入向量
    auto embeddings(embedding_request req)
        -> task<std::expected<embedding_response, std::string>>
    {
        if (auto r = co_await ensure_connected(); !r) co_return std::unexpected(r.error());

        auto body = req.to_json();

        http::request http_req(http::http_method::POST, build_path("/embeddings"));
        apply_common_headers(http_req, "application/json");
        http_req.set_body(std::move(body));

        auto send_r = co_await send_http_request(http_req);
        if (!send_r) co_return std::unexpected(send_r.error());

        auto resp_r = co_await read_full_response();
        if (!resp_r) co_return std::unexpected(resp_r.error());

        auto& [status, resp_body] = *resp_r;
        if (status != 200) {
            auto err = error_response::from_json(resp_body);
            co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
        }

        co_return embedding_response::from_json(resp_body);
    }

    // ── TTS (Text-to-Speech) API ────────────────────────────

    /// 文本转语音 — 返回音频二进制数据
    auto text_to_speech(tts_request req)
        -> task<std::expected<std::vector<std::byte>, std::string>>
    {
        if (auto r = co_await ensure_connected(); !r) co_return std::unexpected(r.error());

        auto body = req.to_json();

        http::request http_req(http::http_method::POST, build_path("/audio/speech"));
        apply_common_headers(http_req, "audio/mpeg");
        http_req.set_body(std::move(body));

        auto send_r = co_await send_http_request(http_req);
        if (!send_r) co_return std::unexpected(send_r.error());

        // 读取二进制音频数据
        auto resp_r = co_await read_binary_response();
        if (!resp_r) co_return std::unexpected(resp_r.error());

        auto& [status, audio_data] = *resp_r;
        if (status != 200) {
            std::string err_text(reinterpret_cast<const char*>(audio_data.data()),
                                 std::min(audio_data.size(), std::size_t{1024}));
            auto err = error_response::from_json(err_text);
            co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
        }

        co_return audio_data;
    }

    // ── STT (Speech-to-Text / Whisper) API ────────────────

    /// 语音转文本 (Transcription)
    auto transcribe(transcription_request req)
        -> task<std::expected<transcription_response, std::string>>
    {
        if (auto r = co_await ensure_connected(); !r) co_return std::unexpected(r.error());

        // 构建 multipart/form-data
        auto [boundary, body] = build_multipart_form(req);

        http::request http_req(http::http_method::POST, build_path("/audio/transcriptions"));
        http_req.set_header("Host", url_.host);
        http_req.set_header("Authorization", "Bearer " + opts_.api_key);
        http_req.set_header("Content-Type", "multipart/form-data; boundary=" + boundary);
        http_req.set_header("Connection", "keep-alive");
        http_req.set_body(std::move(body));

        auto send_r = co_await send_http_request(http_req);
        if (!send_r) co_return std::unexpected(send_r.error());

        auto resp_r = co_await read_full_response();
        if (!resp_r) co_return std::unexpected(resp_r.error());

        auto& [status, resp_body] = *resp_r;
        if (status != 200) {
            auto err = error_response::from_json(resp_body);
            co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
        }

        co_return transcription_response::from_json(resp_body);
    }

    /// 语音翻译为英文 (Translation)
    auto translate(translation_request req)
        -> task<std::expected<transcription_response, std::string>>
    {
        if (auto r = co_await ensure_connected(); !r) co_return std::unexpected(r.error());

        auto [boundary, body] = build_translation_form(req);

        http::request http_req(http::http_method::POST, build_path("/audio/translations"));
        http_req.set_header("Host", url_.host);
        http_req.set_header("Authorization", "Bearer " + opts_.api_key);
        http_req.set_header("Content-Type", "multipart/form-data; boundary=" + boundary);
        http_req.set_header("Connection", "keep-alive");
        http_req.set_body(std::move(body));

        auto send_r = co_await send_http_request(http_req);
        if (!send_r) co_return std::unexpected(send_r.error());

        auto resp_r = co_await read_full_response();
        if (!resp_r) co_return std::unexpected(resp_r.error());

        auto& [status, resp_body] = *resp_r;
        if (status != 200) {
            auto err = error_response::from_json(resp_body);
            co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
        }

        co_return transcription_response::from_json(resp_body);
    }

    // ── DALL-E (Image Generation) API ─────────────────────

    /// 生成图像 (DALL-E 2/3)
    auto create_image(image_generation_request req)
        -> task<std::expected<image_response, std::string>>
    {
        if (auto r = co_await ensure_connected(); !r) co_return std::unexpected(r.error());

        auto body = req.to_json();

        http::request http_req(http::http_method::POST, build_path("/images/generations"));
        apply_common_headers(http_req, "application/json");
        http_req.set_body(std::move(body));

        auto send_r = co_await send_http_request(http_req);
        if (!send_r) co_return std::unexpected(send_r.error());

        auto resp_r = co_await read_full_response();
        if (!resp_r) co_return std::unexpected(resp_r.error());

        auto& [status, resp_body] = *resp_r;
        if (status != 200) {
            auto err = error_response::from_json(resp_body);
            co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
        }

        co_return image_response::from_json(resp_body);
    }

    /// 编辑图像 (DALL-E 2)
    auto edit_image(image_edit_request req)
        -> task<std::expected<image_response, std::string>>
    {
        if (auto r = co_await ensure_connected(); !r) co_return std::unexpected(r.error());

        auto [boundary, body] = build_image_edit_form(req);

        http::request http_req(http::http_method::POST, build_path("/images/edits"));
        http_req.set_header("Host", url_.host);
        http_req.set_header("Authorization", "Bearer " + opts_.api_key);
        http_req.set_header("Content-Type", "multipart/form-data; boundary=" + boundary);
        http_req.set_header("Connection", "keep-alive");
        http_req.set_body(std::move(body));

        auto send_r = co_await send_http_request(http_req);
        if (!send_r) co_return std::unexpected(send_r.error());

        auto resp_r = co_await read_full_response();
        if (!resp_r) co_return std::unexpected(resp_r.error());

        auto& [status, resp_body] = *resp_r;
        if (status != 200) {
            auto err = error_response::from_json(resp_body);
            co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
        }

        co_return image_response::from_json(resp_body);
    }

    /// 图像变体 (DALL-E 2)
    auto create_image_variation(image_variation_request req)
        -> task<std::expected<image_response, std::string>>
    {
        if (auto r = co_await ensure_connected(); !r) co_return std::unexpected(r.error());

        auto [boundary, body] = build_image_variation_form(req);

        http::request http_req(http::http_method::POST, build_path("/images/variations"));
        http_req.set_header("Host", url_.host);
        http_req.set_header("Authorization", "Bearer " + opts_.api_key);
        http_req.set_header("Content-Type", "multipart/form-data; boundary=" + boundary);
        http_req.set_header("Connection", "keep-alive");
        http_req.set_body(std::move(body));

        auto send_r = co_await send_http_request(http_req);
        if (!send_r) co_return std::unexpected(send_r.error());

        auto resp_r = co_await read_full_response();
        if (!resp_r) co_return std::unexpected(resp_r.error());

        auto& [status, resp_body] = *resp_r;
        if (status != 200) {
            auto err = error_response::from_json(resp_body);
            co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
        }

        co_return image_response::from_json(resp_body);
    }

    // ── Moderation API ───────────────────────────────────

    /// 内容审核
    auto moderate(moderation_request req)
        -> task<std::expected<moderation_response, std::string>>
    {
        if (auto r = co_await ensure_connected(); !r) co_return std::unexpected(r.error());

        auto body = req.to_json();

        http::request http_req(http::http_method::POST, build_path("/moderations"));
        apply_common_headers(http_req, "application/json");
        http_req.set_body(std::move(body));

        auto send_r = co_await send_http_request(http_req);
        if (!send_r) co_return std::unexpected(send_r.error());

        auto resp_r = co_await read_full_response();
        if (!resp_r) co_return std::unexpected(resp_r.error());

        auto& [status, resp_body] = *resp_r;
        if (status != 200) {
            auto err = error_response::from_json(resp_body);
            co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
        }

        co_return moderation_response::from_json(resp_body);
    }

private:
    // ── 传输层 ──

    auto do_write(const_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>
    {
#ifdef CNETMOD_HAS_SSL
        if (ssl_) co_return co_await ssl_->async_write(buf);
#endif
        co_return co_await async_write(ctx_, sock_, buf);
    }

    auto do_read(mutable_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>
    {
#ifdef CNETMOD_HAS_SSL
        if (ssl_) co_return co_await ssl_->async_read(buf);
#endif
        co_return co_await async_read(ctx_, sock_, buf);
    }

    auto do_read_some() -> task<std::optional<std::string>> {
        std::array<std::byte, 8192> buf{};
        auto r = co_await do_read(mutable_buffer{buf.data(), buf.size()});
        if (!r || *r == 0) co_return std::nullopt;
        co_return std::string(reinterpret_cast<const char*>(buf.data()), *r);
    }

    // ── 路径 / 通用头部 ──

    [[nodiscard]] auto build_path(std::string_view suffix) const -> std::string {
        auto base = url_.path;
        if (base.empty() || base == "/") base = "/v1";
        return std::string(base) + std::string(suffix);
    }

    void apply_common_headers(http::request& req, std::string_view accept) {
        req.set_header("Host", url_.host);
        req.set_header("Authorization", "Bearer " + opts_.api_key);
        req.set_header("Content-Type", "application/json");
        req.set_header("Accept", std::string(accept));
        req.set_header("Connection", "keep-alive");
        for (auto& [k, v] : opts_.extra_headers)
            req.set_header(k, v);
    }

    // ── 自动重连 ──

    auto ensure_connected() -> task<std::expected<void, std::string>> {
        if (is_connected())
            co_return std::expected<void, std::string>{};
        close();
        co_return co_await connect(opts_);
    }

    // ── HTTP 请求/响应辅助 ──

    auto send_http_request(const http::request& req)
        -> task<std::expected<void, std::string>>
    {
        auto data = req.serialize();
        auto wr = co_await do_write(const_buffer{data.data(), data.size()});
        if (!wr) co_return std::unexpected("write failed: " + wr.error().message());
        co_return std::expected<void, std::string>{};
    }

    /// 读取完整 HTTP 响应（非流式）
    auto read_full_response()
        -> task<std::expected<std::pair<int, std::string>, std::string>>
    {
        http::response_parser parser;

        while (!parser.ready()) {
            auto chunk = co_await do_read_some();
            if (!chunk)
                co_return std::unexpected(std::string("connection closed during response"));

            rbuf_ += *chunk;
            auto consumed = parser.consume(rbuf_.data(), rbuf_.size());
            if (!consumed)
                co_return std::unexpected("HTTP parse error: " + consumed.error().message());
            rbuf_.erase(0, *consumed);
        }

        co_return std::pair{parser.status_code(), std::string(parser.body())};
    }

    /// 读取 HTTP 响应头（流式用 — 只读到 header 结束）
    auto read_response_header()
        -> task<std::expected<std::pair<int, std::string>, std::string>>
    {
        // 读取直到遇到 \r\n\r\n
        while (true) {
            auto header_end = rbuf_.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                // 解析 status line
                auto first_line_end = rbuf_.find("\r\n");
                auto status_line = rbuf_.substr(0, first_line_end);
                // "HTTP/1.1 200 OK"
                int status = 0;
                auto sp1 = status_line.find(' ');
                if (sp1 != std::string::npos) {
                    auto sp2 = status_line.find(' ', sp1 + 1);
                    auto code_str = status_line.substr(sp1 + 1,
                        (sp2 != std::string::npos ? sp2 - sp1 - 1 : std::string::npos));
                    std::from_chars(code_str.data(), code_str.data() + code_str.size(), status);
                }

                // 提取 Content-Type
                std::string ct;
                auto ct_pos = rbuf_.find("Content-Type:");
                if (ct_pos == std::string::npos)
                    ct_pos = rbuf_.find("content-type:");
                if (ct_pos != std::string::npos) {
                    auto val_start = ct_pos + 13;
                    while (val_start < rbuf_.size() && rbuf_[val_start] == ' ') ++val_start;
                    auto val_end = rbuf_.find("\r\n", val_start);
                    ct = rbuf_.substr(val_start, val_end - val_start);
                }

                // 移除已处理的头部（保留 body 部分在 rbuf_）
                rbuf_.erase(0, header_end + 4);
                co_return std::pair{status, ct};
            }

            auto chunk = co_await do_read_some();
            if (!chunk)
                co_return std::unexpected(std::string("connection closed during header"));
            rbuf_ += *chunk;
        }
    }

    /// 读取剩余 body（错误响应用）
    auto read_remaining_body() -> task<std::string> {
        // 尝试再读一些数据
        for (int i = 0; i < 5; ++i) {
            auto chunk = co_await do_read_some();
            if (!chunk || chunk->empty()) break;
            rbuf_ += *chunk;
        }
        auto result = std::move(rbuf_);
        rbuf_.clear();
        co_return result;
    }

    /// 读取二进制响应 (TTS 用)
    auto read_binary_response()
        -> task<std::expected<std::pair<int, std::vector<std::byte>>, std::string>>
    {
        http::response_parser parser;

        while (!parser.ready()) {
            auto chunk = co_await do_read_some();
            if (!chunk)
                co_return std::unexpected(std::string("connection closed during response"));

            rbuf_ += *chunk;
            auto consumed = parser.consume(rbuf_.data(), rbuf_.size());
            if (!consumed)
                co_return std::unexpected("HTTP parse error: " + consumed.error().message());
            rbuf_.erase(0, *consumed);
        }

        auto body = parser.body();
        std::vector<std::byte> data(body.size());
        std::memcpy(data.data(), body.data(), body.size());
        co_return std::pair{parser.status_code(), std::move(data)};
    }

    // ── Multipart form 构建辅助 ──

    [[nodiscard]] static auto generate_boundary() -> std::string {
        static std::atomic<std::uint64_t> counter{0};
        auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::format("----CnetmodBoundary{:016x}{:04x}", ts, counter.fetch_add(1));
    }

    static void append_form_field(std::string& body, std::string_view boundary,
                                   std::string_view name, std::string_view value) {
        body += "--";
        body += boundary;
        body += "\r\n";
        body += std::format("Content-Disposition: form-data; name=\"{}\"\r\n\r\n", name);
        body += value;
        body += "\r\n";
    }

    static void append_form_file(std::string& body, std::string_view boundary,
                                  std::string_view name, std::string_view filename,
                                  std::string_view content_type,
                                  const std::vector<std::byte>& data) {
        body += "--";
        body += boundary;
        body += "\r\n";
        body += std::format("Content-Disposition: form-data; name=\"{}\"; filename=\"{}\"\r\n", name, filename);
        body += std::format("Content-Type: {}\r\n\r\n", content_type);
        body.append(reinterpret_cast<const char*>(data.data()), data.size());
        body += "\r\n";
    }

    [[nodiscard]] static auto get_audio_content_type(std::string_view filename) -> std::string {
        if (filename.ends_with(".mp3"))  return "audio/mpeg";
        if (filename.ends_with(".mp4"))  return "audio/mp4";
        if (filename.ends_with(".m4a"))  return "audio/mp4";
        if (filename.ends_with(".wav"))  return "audio/wav";
        if (filename.ends_with(".webm")) return "audio/webm";
        if (filename.ends_with(".ogg"))  return "audio/ogg";
        if (filename.ends_with(".flac")) return "audio/flac";
        return "application/octet-stream";
    }

    [[nodiscard]] auto build_multipart_form(const transcription_request& req)
        -> std::pair<std::string, std::string>
    {
        auto boundary = generate_boundary();
        std::string body;
        body.reserve(req.file.size() + 1024);

        // file (required)
        append_form_file(body, boundary, "file", req.filename,
                         get_audio_content_type(req.filename), req.file);

        // model (required)
        append_form_field(body, boundary, "model", req.model);

        // optional fields
        if (!req.language.empty())
            append_form_field(body, boundary, "language", req.language);
        if (!req.prompt.empty())
            append_form_field(body, boundary, "prompt", req.prompt);
        if (req.response_format != "json")
            append_form_field(body, boundary, "response_format", req.response_format);
        if (req.temperature != 0.0)
            append_form_field(body, boundary, "temperature", std::format("{}", req.temperature));

        // end boundary
        body += "--";
        body += boundary;
        body += "--\r\n";

        return {boundary, body};
    }

    [[nodiscard]] auto build_translation_form(const translation_request& req)
        -> std::pair<std::string, std::string>
    {
        auto boundary = generate_boundary();
        std::string body;
        body.reserve(req.file.size() + 1024);

        append_form_file(body, boundary, "file", req.filename,
                         get_audio_content_type(req.filename), req.file);
        append_form_field(body, boundary, "model", req.model);

        if (!req.prompt.empty())
            append_form_field(body, boundary, "prompt", req.prompt);
        if (req.response_format != "json")
            append_form_field(body, boundary, "response_format", req.response_format);
        if (req.temperature != 0.0)
            append_form_field(body, boundary, "temperature", std::format("{}", req.temperature));

        body += "--";
        body += boundary;
        body += "--\r\n";

        return {boundary, body};
    }

    [[nodiscard]] auto build_image_edit_form(const image_edit_request& req)
        -> std::pair<std::string, std::string>
    {
        auto boundary = generate_boundary();
        std::string body;
        body.reserve(req.image.size() + req.mask.size() + 2048);

        // image (required)
        append_form_file(body, boundary, "image", req.image_filename, "image/png", req.image);

        // mask (optional)
        if (!req.mask.empty())
            append_form_file(body, boundary, "mask", req.mask_filename, "image/png", req.mask);

        // prompt (required)
        append_form_field(body, boundary, "prompt", req.prompt);

        // optional fields
        if (req.model != "dall-e-2")
            append_form_field(body, boundary, "model", req.model);
        if (req.n != 1)
            append_form_field(body, boundary, "n", std::to_string(req.n));
        if (req.size != "1024x1024")
            append_form_field(body, boundary, "size", req.size);
        if (req.response_format != "url")
            append_form_field(body, boundary, "response_format", req.response_format);
        if (!req.user.empty())
            append_form_field(body, boundary, "user", req.user);

        body += "--";
        body += boundary;
        body += "--\r\n";

        return {boundary, body};
    }

    [[nodiscard]] auto build_image_variation_form(const image_variation_request& req)
        -> std::pair<std::string, std::string>
    {
        auto boundary = generate_boundary();
        std::string body;
        body.reserve(req.image.size() + 1024);

        append_form_file(body, boundary, "image", req.image_filename, "image/png", req.image);

        if (req.model != "dall-e-2")
            append_form_field(body, boundary, "model", req.model);
        if (req.n != 1)
            append_form_field(body, boundary, "n", std::to_string(req.n));
        if (req.size != "1024x1024")
            append_form_field(body, boundary, "size", req.size);
        if (req.response_format != "url")
            append_form_field(body, boundary, "response_format", req.response_format);
        if (!req.user.empty())
            append_form_field(body, boundary, "user", req.user);

        body += "--";
        body += boundary;
        body += "--\r\n";

        return {boundary, body};
    }

    // ── 成员 ──

    io_context&     ctx_;
    socket          sock_;
    connect_options opts_;
    http::url       url_;
    std::string     rbuf_;          // 读缓冲
    bool            connected_ = false;

#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> ssl_ctx_;
    std::unique_ptr<ssl_stream>  ssl_;
#endif
};

} // namespace cnetmod::openai
