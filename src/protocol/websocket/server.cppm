module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.websocket:server;

import std;
import :types;
import :frame;
import :handshake;
import :connection;
import cnetmod.protocol.http;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.tcp;

namespace cnetmod::ws {

// =============================================================================
// 路由段解析
// =============================================================================

namespace detail {

enum class seg_kind { exact, param, wildcard };

struct seg {
    seg_kind    kind;
    std::string value;
};

inline auto parse_ws_pattern(std::string_view pattern) -> std::vector<seg> {
    std::vector<seg> segs;
    if (!pattern.empty() && pattern[0] == '/')
        pattern.remove_prefix(1);
    while (!pattern.empty()) {
        auto slash = pattern.find('/');
        auto part = (slash != std::string_view::npos)
                     ? pattern.substr(0, slash) : pattern;
        if (!part.empty() && part[0] == ':') {
            segs.push_back({seg_kind::param, std::string(part.substr(1))});
        } else if (!part.empty() && part[0] == '*') {
            segs.push_back({seg_kind::wildcard,
                            std::string(part.size() > 1 ? part.substr(1) : "path")});
            break;
        } else {
            segs.push_back({seg_kind::exact, std::string(part)});
        }
        if (slash == std::string_view::npos) break;
        pattern.remove_prefix(slash + 1);
    }
    return segs;
}

inline auto split_ws_path(std::string_view path) -> std::vector<std::string_view> {
    std::vector<std::string_view> parts;
    if (!path.empty() && path[0] == '/')
        path.remove_prefix(1);
    while (!path.empty()) {
        auto slash = path.find('/');
        if (slash != std::string_view::npos) {
            if (slash > 0) parts.push_back(path.substr(0, slash));
            path.remove_prefix(slash + 1);
        } else {
            parts.push_back(path);
            break;
        }
    }
    return parts;
}

struct ws_route_params {
    std::unordered_map<std::string, std::string> named;
    std::string wildcard;

    [[nodiscard]] auto get(std::string_view key) const noexcept
        -> std::string_view
    {
        auto it = named.find(std::string(key));
        if (it != named.end()) return it->second;
        return {};
    }
};

inline auto try_ws_match(const std::vector<seg>& segs,
                         const std::vector<std::string_view>& parts,
                         ws_route_params& out) -> bool
{
    std::size_t si = 0, pi = 0;
    for (; si < segs.size(); ++si) {
        auto& s = segs[si];
        if (s.kind == seg_kind::wildcard) {
            std::string rest;
            for (std::size_t j = pi; j < parts.size(); ++j) {
                if (!rest.empty()) rest += '/';
                rest += parts[j];
            }
            out.wildcard = std::move(rest);
            out.named[s.value] = out.wildcard;
            return true;
        }
        if (pi >= parts.size()) return false;
        if (s.kind == seg_kind::exact) {
            if (parts[pi] != s.value) return false;
        } else {
            out.named[s.value] = std::string(parts[pi]);
        }
        ++pi;
    }
    return pi == parts.size();
}

} // namespace detail

// =============================================================================
// ws_context — WebSocket 连接上下文
// =============================================================================

export class ws_context {
public:
    ws_context(connection& conn, std::string path,
               http::header_map headers, std::string query,
               detail::ws_route_params params)
        : conn_(conn), path_(std::move(path))
        , headers_(std::move(headers)), query_(std::move(query))
        , params_(std::move(params))
    {}

    [[nodiscard]] auto path() const noexcept -> std::string_view { return path_; }
    [[nodiscard]] auto query_string() const noexcept -> std::string_view { return query_; }
    [[nodiscard]] auto headers() const noexcept -> const http::header_map& { return headers_; }

    [[nodiscard]] auto get_header(std::string_view key) const -> std::string_view {
        auto it = headers_.find(std::string(key));
        if (it != headers_.end()) return it->second;
        return {};
    }

    [[nodiscard]] auto param(std::string_view name) const noexcept
        -> std::string_view
    { return params_.get(name); }

    // --- 消息收发 ---

    auto send_text(std::string_view text)
        -> task<std::expected<void, std::error_code>>
    { co_return co_await conn_.async_send_text(text); }

    auto send_binary(std::span<const std::byte> data)
        -> task<std::expected<void, std::error_code>>
    { co_return co_await conn_.async_send_binary(data); }

    auto recv() -> task<std::expected<ws_message, std::error_code>>
    { co_return co_await conn_.async_recv(); }

    auto close(std::uint16_t code = close_code::normal,
               std::string_view reason = "")
        -> task<std::expected<void, std::error_code>>
    { co_return co_await conn_.async_close(code, reason); }

    [[nodiscard]] auto is_open() const noexcept -> bool { return conn_.is_open(); }
    [[nodiscard]] auto raw_connection() noexcept -> connection& { return conn_; }

private:
    connection& conn_;
    std::string path_;
    http::header_map headers_;
    std::string query_;
    detail::ws_route_params params_;
};

export using ws_handler_fn = std::function<task<void>(ws_context&)>;

// =============================================================================
// ws::server
// =============================================================================

export class server {
public:
    /// 单线程模式
    explicit server(io_context& ctx) : ctx_(ctx) {}

    /// 多核模式：accept 在 sctx.accept_io()，连接分发到 worker io_context
    explicit server(server_context& sctx)
        : ctx_(sctx.accept_io()), sctx_(&sctx) {}

    auto listen(std::string_view host, std::uint16_t port)
        -> std::expected<void, std::error_code>
    {
        auto addr_r = ip_address::from_string(host);
        if (!addr_r) return std::unexpected(addr_r.error());
        acc_ = std::make_unique<tcp::acceptor>(ctx_);
        auto r = acc_->open(endpoint{*addr_r, port});
        if (!r) return std::unexpected(r.error());
        return {};
    }

    void on(std::string_view pattern, ws_handler_fn handler) {
        routes_.push_back({detail::parse_ws_pattern(pattern), std::move(handler)});
    }

    auto run() -> task<void> {
        running_ = true;
        while (running_) {
            auto r = co_await async_accept(ctx_, acc_->native_socket());
            if (!r) { if (!running_) break; continue; }
            if (sctx_) {
                // 多核模式：round-robin 分发到 worker io_context
                auto& worker = sctx_->next_worker_io();
                spawn_on(worker, handle_connection(
                    std::move(*r), worker));
            } else {
                // 单线程模式
                spawn(ctx_, handle_connection(
                    std::move(*r), ctx_));
            }
        }
    }

    void stop() { running_ = false; if (acc_) acc_->close(); }

private:
    struct route_entry {
        std::vector<detail::seg> segments;
        ws_handler_fn            handler;
    };

    auto handle_connection(socket client, io_context& io) -> task<void> {
        // 1. 读 HTTP 升级请求
        http::request_parser req_parser;
        std::array<std::byte, 4096> buf{};
        while (!req_parser.ready()) {
            auto rd = co_await async_read(io, client,
                mutable_buffer{buf.data(), buf.size()});
            if (!rd || *rd == 0) { client.close(); co_return; }
            auto consumed = req_parser.consume(
                reinterpret_cast<const char*>(buf.data()), *rd);
            if (!consumed) { client.close(); co_return; }
        }

        // 2. 提取 path / query
        auto uri = req_parser.uri();
        std::string path, query;
        auto qpos = uri.find('?');
        if (qpos != std::string_view::npos) {
            path = std::string(uri.substr(0, qpos));
            query = std::string(uri.substr(qpos + 1));
        } else {
            path = std::string(uri);
        }

        // 3. 路由匹配
        detail::ws_route_params rp;
        const route_entry* matched = nullptr;
        auto parts = detail::split_ws_path(path);
        for (auto& entry : routes_) {
            detail::ws_route_params tmp;
            if (detail::try_ws_match(entry.segments, parts, tmp)) {
                matched = &entry;
                rp = std::move(tmp);
                break;
            }
        }

        if (!matched) {
            http::response resp(http::status::not_found);
            resp.set_body(std::string_view{"404 Not Found"});
            auto data = resp.serialize();
            (void)co_await async_write(io, client,
                const_buffer{data.data(), data.size()});
            client.close(); co_return;
        }

        // 4. 验证 WS 升级
        auto accept_key = validate_upgrade_request(req_parser);
        if (!accept_key) {
            http::response resp(http::status::bad_request);
            resp.set_body(std::string_view{"Bad WebSocket handshake"});
            auto data = resp.serialize();
            (void)co_await async_write(io, client,
                const_buffer{data.data(), data.size()});
            client.close(); co_return;
        }

        // 5. 发送升级响应
        auto upgrade_resp = build_upgrade_response(*accept_key);
        auto resp_data = upgrade_resp.serialize();
        auto wr = co_await async_write(io, client,
            const_buffer{resp_data.data(), resp_data.size()});
        if (!wr) { client.close(); co_return; }

        // 6. attach 已握手的 socket 到 connection
        connection conn(io);
        conn.attach(std::move(client), /*as_server=*/true);

        // 7. 运行 handler
        ws_context wctx(conn, std::move(path),
                        req_parser.headers(), std::move(query),
                        std::move(rp));
        co_await matched->handler(wctx);

        // 8. 确保关闭
        if (conn.is_open()) {
            (void)co_await conn.async_close();
        }
    }

    io_context& ctx_;
    server_context* sctx_ = nullptr;  // 非 null 时为多核模式
    std::unique_ptr<tcp::acceptor> acc_;
    std::vector<route_entry> routes_;
    bool running_ = false;
};

} // namespace cnetmod::ws
