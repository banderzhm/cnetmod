module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:server;

import std;
import :types;
import :parser;
import :request;
import :response;
import :router;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.file;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.tcp;

namespace cnetmod::http {

// =============================================================================
// MIME 类型推断
// =============================================================================

export auto guess_mime_type(std::string_view ext) noexcept -> std::string_view {
    // 转小写首字母比较即可（扩展名通常很短）
    if (ext.empty()) return "application/octet-stream";
    if (ext[0] == '.') ext.remove_prefix(1);

    // 文本
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css")   return "text/css; charset=utf-8";
    if (ext == "js")    return "application/javascript; charset=utf-8";
    if (ext == "json")  return "application/json; charset=utf-8";
    if (ext == "xml")   return "application/xml; charset=utf-8";
    if (ext == "txt")   return "text/plain; charset=utf-8";
    if (ext == "csv")   return "text/csv; charset=utf-8";
    if (ext == "md")    return "text/markdown; charset=utf-8";

    // 图片
    if (ext == "png")   return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif")   return "image/gif";
    if (ext == "svg")   return "image/svg+xml";
    if (ext == "ico")   return "image/x-icon";
    if (ext == "webp")  return "image/webp";
    if (ext == "bmp")   return "image/bmp";

    // 音视频
    if (ext == "mp3")   return "audio/mpeg";
    if (ext == "mp4")   return "video/mp4";
    if (ext == "webm")  return "video/webm";
    if (ext == "ogg")   return "audio/ogg";
    if (ext == "wav")   return "audio/wav";

    // 压缩/二进制
    if (ext == "pdf")   return "application/pdf";
    if (ext == "zip")   return "application/zip";
    if (ext == "gz" || ext == "gzip")  return "application/gzip";
    if (ext == "tar")   return "application/x-tar";
    if (ext == "wasm")  return "application/wasm";

    // 字体
    if (ext == "woff")  return "font/woff";
    if (ext == "woff2") return "font/woff2";
    if (ext == "ttf")   return "font/ttf";
    if (ext == "otf")   return "font/otf";

    return "application/octet-stream";
}

// =============================================================================
// 静态文件服务
// =============================================================================

export struct static_file_options {
    std::filesystem::path root;
    std::string index_file = "index.html";
};

/// 创建静态文件服务 handler
/// 路由应注册为 /prefix/*filepath 形式
export auto serve_dir(static_file_options opts) -> handler_fn {
    return [opts = std::move(opts)](request_context& ctx) -> task<void> {
        auto rel_path = ctx.wildcard();
        if (rel_path.empty()) rel_path = opts.index_file;

        // 安全检查：禁止 .. 穿越
        auto rel_str = std::string(rel_path);
        if (rel_str.find("..") != std::string::npos) {
            ctx.text(status::forbidden, "403 Forbidden");
            co_return;
        }

        auto full_path = opts.root / rel_str;

        // 如果是目录，尝试 index 文件
        std::error_code ec;
        if (std::filesystem::is_directory(full_path, ec)) {
            full_path /= opts.index_file;
        }

        if (!std::filesystem::exists(full_path, ec)) {
            ctx.not_found();
            co_return;
        }

        // 打开文件
        auto f = file::open(full_path, open_mode::read);
        if (!f) {
            ctx.text(status::internal_server_error, "500 Cannot open file");
            co_return;
        }

        auto file_size_r = f->size();
        if (!file_size_r) {
            ctx.text(status::internal_server_error, "500 Cannot stat file");
            co_return;
        }
        auto file_size = *file_size_r;

        // MIME 类型
        auto ext = full_path.extension().string();
        auto mime = guess_mime_type(ext);

        // 检查 Range 头
        auto range_hdr = ctx.get_header("Range");
        std::uint64_t range_start = 0;
        std::uint64_t range_end = file_size - 1;
        bool is_range = false;

        if (!range_hdr.empty() && range_hdr.starts_with("bytes=")) {
            auto spec = range_hdr.substr(6);
            auto dash = spec.find('-');
            if (dash != std::string_view::npos) {
                auto start_str = spec.substr(0, dash);
                auto end_str = spec.substr(dash + 1);
                if (!start_str.empty()) {
                    std::from_chars(start_str.data(),
                        start_str.data() + start_str.size(), range_start);
                }
                if (!end_str.empty()) {
                    std::from_chars(end_str.data(),
                        end_str.data() + end_str.size(), range_end);
                }
                if (range_start <= range_end && range_end < file_size) {
                    is_range = true;
                }
            }
        }

        // 构建响应头
        auto& resp = ctx.resp();
        if (is_range) {
            resp.set_status(status::partial_content);
            auto content_len = range_end - range_start + 1;
            resp.set_header("Content-Range",
                std::format("bytes {}-{}/{}", range_start, range_end, file_size));
            resp.set_header("Content-Length", std::to_string(content_len));
        } else {
            resp.set_status(status::ok);
            resp.set_header("Content-Length", std::to_string(file_size));
            range_start = 0;
            range_end = file_size - 1;
        }
        resp.set_header("Content-Type", mime);
        resp.set_header("Accept-Ranges", "bytes");

        // 先发送响应头（不含 body，body 流式发送）
        // set_body 留空，server 发送头后我们自己流式写 body
        // 用特殊标记让 server 知道我们要流式发送
        // 方案：直接在 handler 里发送全部数据（头+body）

        auto header_data = resp.serialize();  // 无 body 的响应头
        auto wr = co_await async_write(ctx.io_ctx(), ctx.raw_socket(),
            const_buffer{header_data.data(), header_data.size()});
        if (!wr) co_return;

        // 流式发送文件内容
        constexpr std::size_t CHUNK_SIZE = 65536;
        std::vector<std::byte> buf(CHUNK_SIZE);
        std::uint64_t offset = range_start;
        std::uint64_t remaining = range_end - range_start + 1;

        while (remaining > 0) {
            auto to_read = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, CHUNK_SIZE));
            auto rd = co_await async_file_read(ctx.io_ctx(), *f,
                mutable_buffer{buf.data(), to_read}, offset);
            if (!rd || *rd == 0) break;

            auto wf = co_await async_write(ctx.io_ctx(), ctx.raw_socket(),
                const_buffer{buf.data(), *rd});
            if (!wf) break;

            offset += *rd;
            remaining -= *rd;
        }

        // 标记响应已经发送（server 不再重复发送）
        resp.set_header("X-Streamed", "1");
        co_return;
    };
}

// =============================================================================
// 文件上传保存
// =============================================================================

export struct upload_options {
    std::filesystem::path save_dir;
    std::string default_filename = "upload.bin";
    std::size_t max_size = 32 * 1024 * 1024;  // 32MB
};

/// 创建文件上传处理 handler
/// 支持 multipart/form-data 和 raw body 两种模式
export auto save_upload(upload_options opts) -> handler_fn {
    return [opts = std::move(opts)](request_context& ctx) -> task<void> {
        auto body = ctx.body();
        if (body.empty()) {
            ctx.text(status::bad_request, "Empty body");
            co_return;
        }

        if (body.size() > opts.max_size) {
            ctx.text(status::payload_too_large, "File too large");
            co_return;
        }

        // 确保目录存在
        std::error_code ec;
        std::filesystem::create_directories(opts.save_dir, ec);

        // 检查是否为 multipart/form-data
        auto ct = ctx.get_header("Content-Type");
        auto ct_parsed = parse_content_type(ct);

        if (ct_parsed.mime == "multipart/form-data") {
            // === multipart 模式 ===
            auto form_r = ctx.parse_form();
            if (!form_r) {
                ctx.text(status::bad_request,
                    std::format("Multipart parse error: {}",
                                form_r.error().message()));
                co_return;
            }
            auto& form = **form_r;

            // 保存所有上传文件
            std::string json_files = "[";
            bool first = true;

            for (auto& ff : form.all_files()) {
                auto fname = ff.filename;
                // 安全检查
                if (fname.empty()) fname = opts.default_filename;
                if (fname.find("..") != std::string::npos ||
                    fname.find('/') != std::string::npos ||
                    fname.find('\\') != std::string::npos) {
                    fname = opts.default_filename;
                }

                auto full_path = opts.save_dir / fname;
                auto f = file::open(full_path,
                    open_mode::write | open_mode::create | open_mode::truncate);
                if (!f) continue;

                auto wr = co_await async_file_write(ctx.io_ctx(), *f,
                    const_buffer{ff.data.data(), ff.data.size()});
                if (!wr) continue;

                if (!first) json_files += ",";
                json_files += std::format(
                    R"({{"field":"{}","filename":"{}","size":{},"type":"{}"}})",
                    ff.field_name, fname, ff.data.size(), ff.content_type);
                first = false;
            }
            json_files += "]";

            // 将表单字段也包含在响应中
            std::string json_fields = "[";
            first = true;
            for (auto& field : form.all_fields()) {
                if (!first) json_fields += ",";
                json_fields += std::format(
                    R"({{"name":"{}","value":"{}"}})",
                    field.name, field.value);
                first = false;
            }
            json_fields += "]";

            ctx.json(status::ok, std::format(
                R"({{"files":{},"fields":{},"file_count":{},"field_count":{}}})",
                json_files, json_fields,
                form.file_count(), form.field_count()));
            co_return;
        }

        // === raw body 模式 ===
        std::string filename = opts.default_filename;
        auto qs = ctx.query_string();
        auto name_pos = qs.find("name=");
        if (name_pos != std::string_view::npos) {
            auto start = name_pos + 5;
            auto end = qs.find('&', start);
            auto name = (end != std::string_view::npos)
                ? qs.substr(start, end - start) : qs.substr(start);
            if (!name.empty()) {
                auto name_str = std::string(name);
                if (name_str.find("..") == std::string::npos &&
                    name_str.find('/') == std::string::npos &&
                    name_str.find('\\') == std::string::npos) {
                    filename = std::move(name_str);
                }
            }
        }

        auto full_path = opts.save_dir / filename;
        auto f = file::open(full_path,
            open_mode::write | open_mode::create | open_mode::truncate);
        if (!f) {
            ctx.text(status::internal_server_error, "Cannot create file");
            co_return;
        }

        auto wr = co_await async_file_write(ctx.io_ctx(), *f,
            const_buffer{body.data(), body.size()});
        if (!wr) {
            ctx.text(status::internal_server_error, "Write failed");
            co_return;
        }

        ctx.json(status::ok,
            std::format(R"({{"filename":"{}","size":{}}})", filename, body.size()));
        co_return;
    };
}

// =============================================================================
// http::server — 高级 HTTP 服务器
// =============================================================================

export class server {
public:
    /// 单线程模式：所有连接在同一个 io_context 上处理
    explicit server(io_context& ctx)
        : ctx_(ctx) {}

    /// 多核模式：accept 在 sctx.accept_io()，连接分发到 worker io_context
    explicit server(server_context& sctx)
        : ctx_(sctx.accept_io()), sctx_(&sctx) {}

    /// 监听指定地址和端口
    auto listen(std::string_view host, std::uint16_t port)
        -> std::expected<void, std::error_code>
    {
        auto addr_r = ip_address::from_string(host);
        if (!addr_r) return std::unexpected(addr_r.error());

        acc_ = std::make_unique<tcp::acceptor>(ctx_);
        auto ep = endpoint{*addr_r, port};
        auto r = acc_->open(ep, {.reuse_address = true});
        if (!r) return std::unexpected(r.error());

        host_ = std::string(host);
        port_ = port;
        return {};
    }

    /// 设置路由
    void set_router(router r) { router_ = std::move(r); }

    /// 添加中间件
    void use(middleware_fn mw) { middlewares_.push_back(std::move(mw)); }

    /// 运行服务器（accept 循环，协程）
    auto run() -> task<void> {
        running_ = true;
        while (running_) {
            auto r = co_await async_accept(ctx_, acc_->native_socket());
            if (!r) {
                if (!running_) break;
                continue;
            }
            if (sctx_) {
                // 多核模式：round-robin 分发到 worker io_context
                auto& worker = sctx_->next_worker_io();
                spawn_on(worker, handle_connection(
                    std::move(*r), worker));
            } else {
                // 单线程模式：在当前 io_context 处理
                spawn(ctx_, handle_connection(
                    std::move(*r), ctx_));
            }
        }
    }

    /// 停止服务器
    void stop() {
        running_ = false;
        if (acc_) acc_->close();
    }

private:
    /// 处理单个连接（支持 keep-alive）
    /// @param io 该连接绑定的 io_context（可能是 worker）
    auto handle_connection(socket client, io_context& io) -> task<void> {
        bool keep_alive = true;

        while (keep_alive) {
            // 解析请求
            request_parser parser;
            std::array<std::byte, 8192> buf{};

            while (!parser.ready()) {
                auto rd = co_await async_read(io, client, mutable_buffer{buf.data(), buf.size()});
                if (!rd || *rd == 0) { client.close(); co_return; }

                auto consumed = parser.consume(
                    reinterpret_cast<const char*>(buf.data()), *rd);
                if (!consumed) { client.close(); co_return; }
            }

            // 提取 path（去除 query string）
            auto uri = parser.uri();
            auto qpos = uri.find('?');
            auto path = (qpos != std::string_view::npos)
                ? uri.substr(0, qpos) : uri;

            // 路由匹配
            auto mr = router_.match(parser.method(), path);

            response resp(status::ok);
            resp.set_header("Server", "cnetmod");

            route_params rp;
            handler_fn handler;

            if (mr) {
                handler = std::move(mr->handler);
                rp = std::move(mr->params);
            } else {
                // 404
                handler = [](request_context& ctx) -> task<void> {
                    ctx.not_found();
                    co_return;
                };
            }

            request_context rctx(io, client, parser, resp, std::move(rp));

            // 执行中间件链 + handler
            co_await execute_chain(rctx, handler);

            // 检查是否已流式发送（X-Streamed 标记）
            if (resp.get_header("X-Streamed") != "1") {
                // 正常发送响应
                auto data = resp.serialize();
                auto wr = co_await async_write(io, client,
                    const_buffer{data.data(), data.size()});
                if (!wr) { client.close(); co_return; }
            }

            // 检查 keep-alive
            auto conn_hdr = parser.get_header("Connection");
            if (parser.version() == http_version::http_1_1) {
                keep_alive = (conn_hdr != "close");
            } else {
                keep_alive = false;
            }
        }

        client.close();
    }

    /// 执行中间件链，最后调用 handler
    auto execute_chain(request_context& ctx, handler_fn& handler)
        -> task<void>
    {
        if (middlewares_.empty()) {
            co_await handler(ctx);
            co_return;
        }

        // 构建洋葱调用链
        std::function<task<void>()> chain;

        // 递归构建
        auto build_chain = [&](auto& self, std::size_t i) -> std::function<task<void>()> {
            if (i >= middlewares_.size()) {
                // 最内层：调用 handler
                return [&ctx, &handler]() -> task<void> {
                    co_await handler(ctx);
                };
            }
            return [&ctx, &mw = middlewares_[i], next_fn = self(self, i + 1)]() -> task<void> {
                co_await mw(ctx, next_fn);
            };
        };

        chain = build_chain(build_chain, 0);
        co_await chain();
    }

    io_context& ctx_;
    server_context* sctx_ = nullptr;  // 非 null 时为多核模式
    std::unique_ptr<tcp::acceptor> acc_;
    router router_;
    std::vector<middleware_fn> middlewares_;
    std::string host_;
    std::uint16_t port_ = 0;
    bool running_ = false;
};

} // namespace cnetmod::http
