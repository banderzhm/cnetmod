module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.redis:client;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :types;
import :request;
import :parser;

namespace cnetmod::redis {

// =============================================================================
// Connection Options
// =============================================================================

export struct connect_options {
    std::string   host     = "127.0.0.1";
    std::uint16_t port     = 6379;
    std::string   password;            // AUTH password (empty = no auth)
    std::string   username;            // Redis 6+ ACL username (empty = default)
    std::uint32_t db       = 0;       // SELECT database number (0 = default)
    bool          resp3    = true;    // Whether to send HELLO 3 to switch to RESP3

    // TLS configuration
    bool        tls           = false;
    bool        tls_verify    = true;
    std::string tls_ca_file;
    std::string tls_cert_file;
    std::string tls_key_file;
    std::string tls_sni;
};

// =============================================================================
// PubSub Push Callback Type
// =============================================================================

/// Push message callback: (channel, message, complete node list)
export using push_callback = std::function<void(std::string_view channel,
                                                std::string_view message)>;

// =============================================================================
// redis::client — Single Connection Async Client (RESP3)
// =============================================================================

export class client {
public:
    explicit client(io_context& ctx) noexcept : ctx_(ctx) {}

    // ----- Connect / Close -----

    /// Connect to Redis, automatically handles TLS, HELLO 3, AUTH and SELECT
    auto connect(connect_options opts = {}) -> task<std::expected<void, std::string>> {
        auto addr_r = ip_address::from_string(opts.host);
        if (!addr_r)
            co_return std::unexpected(std::string("invalid host"));

        auto family = addr_r->is_v4() ? address_family::ipv4 : address_family::ipv6;
        auto sock_r = socket::create(family, socket_type::stream);
        if (!sock_r)
            co_return std::unexpected(std::string("socket create failed"));
        sock_ = std::move(*sock_r);

        auto cr = co_await async_connect(ctx_, sock_, endpoint{*addr_r, opts.port});
        if (!cr) {
            sock_.close();
            co_return std::unexpected(cr.error().message());
        }

        // TLS
#ifdef CNETMOD_HAS_SSL
        if (opts.tls) {
            auto ssl_ctx_r = ssl_context::client();
            if (!ssl_ctx_r) {
                sock_.close();
                co_return std::unexpected("ssl context: " + ssl_ctx_r.error().message());
            }
            ssl_ctx_ = std::make_unique<ssl_context>(std::move(*ssl_ctx_r));
            ssl_ctx_->set_verify_peer(opts.tls_verify);
            if (!opts.tls_ca_file.empty()) {
                auto r = ssl_ctx_->load_ca_file(opts.tls_ca_file);
                if (!r) { sock_.close(); co_return std::unexpected("ssl ca: " + r.error().message()); }
            } else if (opts.tls_verify) {
                (void)ssl_ctx_->set_default_ca();
            }
            if (!opts.tls_cert_file.empty()) {
                auto r = ssl_ctx_->load_cert_file(opts.tls_cert_file);
                if (!r) { sock_.close(); co_return std::unexpected("ssl cert: " + r.error().message()); }
            }
            if (!opts.tls_key_file.empty()) {
                auto r = ssl_ctx_->load_key_file(opts.tls_key_file);
                if (!r) { sock_.close(); co_return std::unexpected("ssl key: " + r.error().message()); }
            }
            ssl_ = std::make_unique<ssl_stream>(*ssl_ctx_, ctx_, sock_);
            ssl_->set_connect_state();
            ssl_->set_hostname(opts.tls_sni.empty() ? opts.host : opts.tls_sni);
            auto hs = co_await ssl_->async_handshake();
            if (!hs) { sock_.close(); co_return std::unexpected("ssl handshake: " + hs.error().message()); }
        }
#else
        if (opts.tls) {
            sock_.close();
            co_return std::unexpected(std::string("SSL not available"));
        }
#endif

        // HELLO 3 (switch to RESP3 protocol)
        if (opts.resp3) {
            request hello_req;
            if (!opts.password.empty()) {
                if (opts.username.empty())
                    hello_req.push("HELLO", "3", "AUTH", "default", opts.password);
                else
                    hello_req.push("HELLO", "3", "AUTH", opts.username, opts.password);
            } else {
                hello_req.push("HELLO", "3");
            }
            auto hello_r = co_await exec(hello_req);
            if (!hello_r)
                co_return std::unexpected("HELLO 3 failed: " + hello_r.error());
            // HELLO reply is a map, check for errors
            if (!hello_r->empty() && hello_r->front().is_error()) {
                // Server doesn't support RESP3, fallback to RESP2
                resp3_mode_ = false;
                // Still need AUTH
                if (!opts.password.empty()) {
                    auto auth_r = co_await do_auth(opts);
                    if (!auth_r) co_return auth_r;
                }
            } else {
                resp3_mode_ = true;
            }
        } else {
            resp3_mode_ = false;
            // AUTH
            if (!opts.password.empty()) {
                auto auth_r = co_await do_auth(opts);
                if (!auth_r) co_return auth_r;
            }
        }

        // SELECT db
        if (opts.db > 0) {
            request sel;
            sel.push("SELECT", std::to_string(opts.db));
            auto sel_r = co_await exec(sel);
            if (!sel_r)
                co_return std::unexpected("SELECT failed: " + sel_r.error());
            if (!sel_r->empty() && sel_r->front().is_error())
                co_return std::unexpected("SELECT error: " + sel_r->front().value);
        }

        co_return std::expected<void, std::string>{};
    }

    auto is_open() const noexcept -> bool { return sock_.is_open(); }

    void close() noexcept {
#ifdef CNETMOD_HAS_SSL
        ssl_.reset();
        ssl_ctx_.reset();
#endif
        sock_.close();
    }

    // ----- Execute Commands -----

    /// Execute a request (can contain multiple commands), returns all response nodes (pre-order traversal)
    auto exec(const request& req)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        auto payload = req.payload();
        auto w = co_await do_write(const_buffer{payload.data(), payload.size()});
        if (!w) co_return std::unexpected(w.error().message());

        std::vector<resp3_node> all_nodes;
        for (std::size_t i = 0; i < req.size(); ++i) {
            auto nodes = co_await parse_one_response();
            if (!nodes) co_return std::unexpected(nodes.error());
            for (auto& n : *nodes)
                all_nodes.push_back(std::move(n));
        }
        co_return all_nodes;
    }

    /// Convenience: single command (initializer_list)
    auto cmd(std::initializer_list<std::string_view> args)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        request req;
        // Build: first is command, rest are arguments
        if (args.size() == 0) co_return std::unexpected(std::string("empty command"));

        // Manually construct to support initializer_list
        std::string payload;
        detail::add_header(payload, resp3_type::array, args.size());
        for (auto a : args)
            detail::add_bulk(payload, a);

        auto w = co_await do_write(const_buffer{payload.data(), payload.size()});
        if (!w) co_return std::unexpected(w.error().message());
        co_return co_await parse_one_response();
    }

    /// Pipeline: Multiple commands in single round-trip
    auto pipe(std::initializer_list<std::initializer_list<std::string_view>> cmds)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        std::string batch;
        std::size_t n = 0;
        for (auto& c : cmds) {
            detail::add_header(batch, resp3_type::array, c.size());
            for (auto a : c)
                detail::add_bulk(batch, a);
            ++n;
        }

        auto w = co_await do_write(const_buffer{batch.data(), batch.size()});
        if (!w) co_return std::unexpected(w.error().message());

        std::vector<resp3_node> all;
        for (std::size_t i = 0; i < n; ++i) {
            auto nodes = co_await parse_one_response();
            if (!nodes) co_return std::unexpected(nodes.error());
            for (auto& nd : *nodes)
                all.push_back(std::move(nd));
        }
        co_return all;
    }

    // ----- PubSub -----

    /// Subscribe to channels
    auto subscribe(std::initializer_list<std::string_view> channels)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        std::string payload;
        detail::add_header(payload, resp3_type::array, 1 + channels.size());
        detail::add_bulk(payload, "SUBSCRIBE");
        for (auto ch : channels)
            detail::add_bulk(payload, ch);

        auto w = co_await do_write(const_buffer{payload.data(), payload.size()});
        if (!w) co_return std::unexpected(w.error().message());

        // One confirmation response per channel
        std::vector<resp3_node> all;
        for (std::size_t i = 0; i < channels.size(); ++i) {
            auto nodes = co_await parse_one_response();
            if (!nodes) co_return std::unexpected(nodes.error());
            for (auto& nd : *nodes)
                all.push_back(std::move(nd));
        }
        co_return all;
    }

    /// Unsubscribe from channels
    auto unsubscribe(std::initializer_list<std::string_view> channels)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        std::string payload;
        detail::add_header(payload, resp3_type::array, 1 + channels.size());
        detail::add_bulk(payload, "UNSUBSCRIBE");
        for (auto ch : channels)
            detail::add_bulk(payload, ch);

        auto w = co_await do_write(const_buffer{payload.data(), payload.size()});
        if (!w) co_return std::unexpected(w.error().message());

        std::vector<resp3_node> all;
        for (std::size_t i = 0; i < channels.size(); ++i) {
            auto nodes = co_await parse_one_response();
            if (!nodes) co_return std::unexpected(nodes.error());
            for (auto& nd : *nodes)
                all.push_back(std::move(nd));
        }
        co_return all;
    }

    /// Register push message callback
    void on_push(push_callback cb) { push_cb_ = std::move(cb); }

    /// Receive one push message (blocking wait)
    auto receive_push()
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        co_return co_await parse_one_response();
    }

    /// Check if running in RESP3 mode
    [[nodiscard]] auto is_resp3() const noexcept -> bool { return resp3_mode_; }

private:
    // ── Transport layer ──

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

    // ── AUTH helper ──

    auto do_auth(const connect_options& opts)
        -> task<std::expected<void, std::string>>
    {
        request auth_req;
        if (opts.username.empty())
            auth_req.push("AUTH", opts.password);
        else
            auth_req.push("AUTH", opts.username, opts.password);
        auto r = co_await exec(auth_req);
        if (!r) co_return std::unexpected("AUTH failed: " + r.error());
        if (!r->empty() && r->front().is_error())
            co_return std::unexpected("AUTH error: " + r->front().value);
        co_return std::expected<void, std::string>{};
    }

    // ── Parse one complete response ──

    auto parse_one_response()
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        std::vector<resp3_node> nodes;
        resp3_parser parser;
        std::error_code ec;

        while (!parser.done()) {
            // Ensure buffer has data
            if (parser.consumed() >= rbuf_.size() || rbuf_.empty()) {
                // Need to read more data from network
                auto ok = co_await fill();
                if (!ok) co_return std::unexpected(std::string("connection closed"));
            }

            auto view = std::string_view(rbuf_).substr(rpos_);
            auto node = parser.consume(view, ec);
            if (ec)
                co_return std::unexpected(ec.message());

            if (node) {
                nodes.push_back(std::move(*node));
                rpos_ += parser.consumed();
                parser.reset();

                // Check if more nodes needed (aggregate types)
                if (!nodes.empty()) {
                    auto& first = nodes.front();
                    if (first.is_aggregate() && first.aggregate_size > 0) {
                        // Continue parsing child nodes
                        auto remaining = co_await parse_children(
                            first.aggregate_size * element_multiplicity(first.data_type));
                        if (!remaining)
                            co_return std::unexpected(remaining.error());
                        for (auto& n : *remaining)
                            nodes.push_back(std::move(n));
                    }
                }
                compact_buffer();
                co_return nodes;
            }

            // Need more data
            rpos_ += parser.consumed();
            parser.reset();
            auto ok = co_await fill();
            if (!ok)
                co_return std::unexpected(std::string("connection closed"));
        }

        compact_buffer();
        co_return nodes;
    }

    /// Parse N child nodes
    auto parse_children(std::size_t count)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        std::vector<resp3_node> children;
        children.reserve(count);

        for (std::size_t i = 0; i < count; ++i) {
            resp3_parser parser;
            std::error_code ec;
            bool got_node = false;

            while (!got_node) {
                auto view = std::string_view(rbuf_).substr(rpos_);
                if (view.empty()) {
                    auto ok = co_await fill();
                    if (!ok) co_return std::unexpected(std::string("connection closed"));
                    continue;
                }

                auto node = parser.consume(view, ec);
                if (ec)
                    co_return std::unexpected(ec.message());

                if (node) {
                    rpos_ += parser.consumed();
                    children.push_back(std::move(*node));
                    got_node = true;

                    // If child node is aggregate type, parse recursively
                    auto& child = children.back();
                    if (child.is_aggregate() && child.aggregate_size > 0) {
                        auto sub = co_await parse_children(
                            child.aggregate_size * element_multiplicity(child.data_type));
                        if (!sub) co_return std::unexpected(sub.error());
                        for (auto& n : *sub)
                            children.push_back(std::move(n));
                        i += sub->size(); // Skip already counted child elements
                    }
                } else {
                    rpos_ += parser.consumed();
                    parser.reset();
                    auto ok = co_await fill();
                    if (!ok) co_return std::unexpected(std::string("connection closed"));
                }
            }
        }
        co_return children;
    }

    // ── Read buffer ──

    auto fill() -> task<bool> {
        std::array<std::byte, 8192> tmp{};
        auto r = co_await do_read(mutable_buffer{tmp.data(), tmp.size()});
        if (!r || *r == 0) co_return false;
        rbuf_.append(reinterpret_cast<const char*>(tmp.data()), *r);
        co_return true;
    }

    void compact_buffer() {
        if (rpos_ > 4096) {
            rbuf_.erase(0, rpos_);
            rpos_ = 0;
        }
    }

    // ── Members ──

    io_context& ctx_;
    socket      sock_;
    std::string rbuf_;
    std::size_t rpos_ = 0;
    bool        resp3_mode_ = false;
    push_callback push_cb_;

#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> ssl_ctx_;
    std::unique_ptr<ssl_stream>  ssl_;
#endif
};

// =============================================================================
// Convenience functions: Extract simple values from node list
// =============================================================================

/// Extract first value from node list (skip aggregate header node)
export auto first_value(const std::vector<resp3_node>& nodes) noexcept
    -> std::string_view
{
    for (auto& n : nodes) {
        if (!n.is_aggregate() && !n.is_null())
            return n.value;
    }
    return {};
}

/// Extract all values from node list (skip aggregate header node)
export auto all_values(const std::vector<resp3_node>& nodes)
    -> std::vector<std::string_view>
{
    std::vector<std::string_view> vals;
    for (auto& n : nodes) {
        if (!n.is_aggregate() && !n.is_null())
            vals.push_back(n.value);
    }
    return vals;
}

/// Check if node list represents OK response
export auto is_ok(const std::vector<resp3_node>& nodes) noexcept -> bool {
    if (nodes.empty()) return false;
    auto& n = nodes.front();
    return (n.data_type == resp3_type::simple_string && n.value == "OK");
}

/// Check if node list contains error
export auto has_error(const std::vector<resp3_node>& nodes) noexcept -> bool {
    for (auto& n : nodes) {
        if (n.is_error()) return true;
    }
    return false;
}

/// Get first error message
export auto error_message(const std::vector<resp3_node>& nodes) noexcept
    -> std::string_view
{
    for (auto& n : nodes) {
        if (n.is_error()) return n.value;
    }
    return {};
}

} // namespace cnetmod::redis
