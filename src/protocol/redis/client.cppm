module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.redis:client;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.dns;
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

export struct endpoint_info {
    std::string host;
    std::uint16_t port = 0;
};

export enum class redirect_kind {
    moved,
    ask,
};

export struct cluster_redirect {
    redirect_kind kind = redirect_kind::moved;
    std::uint16_t slot = 0;
    endpoint_info endpoint;
};

export struct cluster_slot_range {
    std::uint16_t start = 0;
    std::uint16_t end = 0;
    endpoint_info master;
    std::vector<endpoint_info> replicas;
};

export struct cluster_pipeline_item {
    std::vector<std::string> args;
    std::string key;
};

export class cluster_slot_cache {
public:
    void clear() {
        slots_.fill(std::nullopt);
    }

    void update(const std::vector<cluster_slot_range>& ranges) {
        clear();
        for (const auto& range : ranges) {
            auto end = std::min<std::uint16_t>(range.end, 16383);
            for (std::uint16_t slot = range.start; slot <= end; ++slot) {
                slots_[slot] = range.master;
                if (slot == 16383) break;
            }
        }
    }

    void update_slot(std::uint16_t slot, endpoint_info endpoint) {
        if (slot < slots_.size()) {
            slots_[slot] = std::move(endpoint);
        }
    }

    [[nodiscard]] auto endpoint_for_slot(std::uint16_t slot) const
        -> std::optional<endpoint_info>
    {
        if (slot >= slots_.size()) return std::nullopt;
        return slots_[slot];
    }

    [[nodiscard]] auto endpoint_for_key(std::string_view key) const
        -> std::optional<endpoint_info>;

    [[nodiscard]] auto covered_slots() const noexcept -> std::size_t {
        return static_cast<std::size_t>(std::ranges::count_if(slots_,
            [](const auto& ep) { return ep.has_value(); }));
    }

private:
    std::array<std::optional<endpoint_info>, 16384> slots_{};
};

export auto first_value(const std::vector<resp3_node>& nodes) noexcept
    -> std::string_view;
export auto all_values(const std::vector<resp3_node>& nodes)
    -> std::vector<std::string_view>;
export auto is_ok(const std::vector<resp3_node>& nodes) noexcept -> bool;
export auto has_error(const std::vector<resp3_node>& nodes) noexcept -> bool;
export auto error_message(const std::vector<resp3_node>& nodes) noexcept
    -> std::string_view;

// =============================================================================
// redis::client — Single Connection Async Client (RESP3)
// =============================================================================

export class client {
public:
    explicit client(io_context& ctx) noexcept : ctx_(ctx) {}

    // ----- Connect / Close -----

    /// Connect to Redis, automatically handles TLS, HELLO 3, AUTH and SELECT
    auto connect(connect_options opts = {}) -> task<std::expected<void, std::string>> {
        opts_ = opts;
        auto connect_r = co_await async_connect_happy_eyeballs(ctx_, opts.host, opts.port);
        if (!connect_r) {
            co_return std::unexpected(connect_r.error().message());
        }
        sock_ = std::move(connect_r->sock);

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

    auto cmd(std::span<const std::string> args)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        if (args.empty()) co_return std::unexpected(std::string("empty command"));

        std::string payload;
        detail::add_header(payload, resp3_type::array, args.size());
        for (const auto& a : args)
            detail::add_bulk(payload, a);

        auto w = co_await do_write(const_buffer{payload.data(), payload.size()});
        if (!w) co_return std::unexpected(w.error().message());
        co_return co_await parse_one_response();
    }

    auto cmd_follow_redirect(std::vector<std::string> args, std::size_t max_redirects = 3)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        for (std::size_t attempt = 0; attempt <= max_redirects; ++attempt) {
            auto r = co_await cmd(std::span<const std::string>{args.data(), args.size()});
            if (!r) co_return r;

            auto redirect = parse_redirect(*r);
            if (!redirect) co_return r;
            if (attempt == max_redirects) {
                co_return std::unexpected(std::string("redis cluster redirect limit exceeded"));
            }

            auto next = opts_;
            next.host = redirect->endpoint.host;
            next.port = redirect->endpoint.port;
            close();

            auto connected = co_await connect(next);
            if (!connected) co_return std::unexpected(connected.error());

            if (redirect->kind == redirect_kind::ask) {
                auto asking = co_await cmd({"ASKING"});
                if (!asking) co_return asking;
                if (has_error(*asking)) {
                    co_return std::unexpected(std::string(error_message(*asking)));
                }
            }
        }

        co_return std::unexpected(std::string("redis cluster redirect failed"));
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

    auto pipe(std::span<const std::vector<std::string>> cmds)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        std::string batch;
        std::size_t n = 0;
        for (const auto& c : cmds) {
            if (c.empty()) {
                co_return std::unexpected(std::string("empty command in pipeline"));
            }
            detail::add_header(batch, resp3_type::array, c.size());
            for (const auto& a : c)
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

    auto psubscribe(std::initializer_list<std::string_view> patterns)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        co_return co_await subscription_command("PSUBSCRIBE", patterns);
    }

    auto punsubscribe(std::initializer_list<std::string_view> patterns)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        co_return co_await subscription_command("PUNSUBSCRIBE", patterns);
    }

    auto sentinel_get_master_addr_by_name(std::string_view master)
        -> task<std::expected<endpoint_info, std::string>>
    {
        auto r = co_await cmd({"SENTINEL", "GET-MASTER-ADDR-BY-NAME", master});
        if (!r) co_return std::unexpected(r.error());
        if (has_error(*r)) co_return std::unexpected(std::string(error_message(*r)));

        auto values = all_values(*r);
        if (values.size() < 2) {
            co_return std::unexpected(std::string("sentinel returned no master address"));
        }

        std::uint16_t port = 0;
        auto port_text = values[1];
        auto [ptr, ec] = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
        if (ec != std::errc{}) {
            co_return std::unexpected(std::string("sentinel returned invalid master port"));
        }

        co_return endpoint_info{.host = std::string(values[0]), .port = port};
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

    [[nodiscard]] static auto key_slot(std::string_view key) noexcept -> std::uint16_t;
    [[nodiscard]] static auto parse_redirect(const std::vector<resp3_node>& nodes)
        -> std::optional<cluster_redirect>;
    [[nodiscard]] static auto parse_cluster_slots(const std::vector<resp3_node>& nodes)
        -> std::expected<std::vector<cluster_slot_range>, std::string>;

private:
    // ── Transport layer ──

    auto do_write(const_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>
    {
#ifdef CNETMOD_HAS_SSL
        if (ssl_) {
            auto r = co_await ssl_->async_write_all(buf);
            if (!r) co_return std::unexpected(r.error());
            co_return buf.size;
        }
#endif
        auto r = co_await async_write_all(ctx_, sock_, buf);
        if (!r) co_return std::unexpected(r.error());
        co_return buf.size;
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

    auto subscription_command(std::string_view command,
                              std::initializer_list<std::string_view> names)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        std::string payload;
        detail::add_header(payload, resp3_type::array, 1 + names.size());
        detail::add_bulk(payload, command);
        for (auto name : names)
            detail::add_bulk(payload, name);

        auto w = co_await do_write(const_buffer{payload.data(), payload.size()});
        if (!w) co_return std::unexpected(w.error().message());

        std::vector<resp3_node> all;
        for (std::size_t i = 0; i < names.size(); ++i) {
            auto nodes = co_await parse_one_response();
            if (!nodes) co_return std::unexpected(nodes.error());
            for (auto& nd : *nodes)
                all.push_back(std::move(nd));
        }
        co_return all;
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
    connect_options opts_;
    std::string rbuf_;
    std::size_t rpos_ = 0;
    bool        resp3_mode_ = false;
    push_callback push_cb_;

#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> ssl_ctx_;
    std::unique_ptr<ssl_stream>  ssl_;
#endif
};

export class cluster_client {
public:
    explicit cluster_client(io_context& ctx) noexcept
        : ctx_(ctx), seed_(ctx) {}

    auto connect(connect_options seed) -> task<std::expected<void, std::string>> {
        seed_options_ = std::move(seed);
        auto r = co_await seed_.connect(seed_options_);
        if (!r) co_return r;
        co_return co_await refresh_slots();
    }

    auto refresh_slots() -> task<std::expected<void, std::string>> {
        auto r = co_await seed_.cmd({"CLUSTER", "SLOTS"});
        if (!r) co_return std::unexpected(r.error());
        auto ranges = client::parse_cluster_slots(*r);
        if (!ranges) co_return std::unexpected(ranges.error());
        slot_cache_.update(*ranges);
        co_return std::expected<void, std::string>{};
    }

    auto cmd_for_key(std::vector<std::string> args,
                     std::string_view key,
                     std::size_t max_redirects = 3)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        if (args.empty()) co_return std::unexpected(std::string("empty command"));

        for (std::size_t attempt = 0; attempt <= max_redirects; ++attempt) {
            const auto slot = client::key_slot(key);
            auto endpoint = slot_cache_.endpoint_for_slot(slot);
            if (!endpoint) {
                auto refreshed = co_await refresh_slots();
                if (!refreshed) co_return std::unexpected(refreshed.error());
                endpoint = slot_cache_.endpoint_for_slot(slot);
            }
            if (!endpoint) {
                co_return std::unexpected(std::string("redis cluster slot not covered"));
            }

            auto* conn = co_await connection_for(*endpoint);
            if (!conn) co_return std::unexpected(std::string("redis cluster node connect failed"));

            auto r = co_await conn->cmd(std::span<const std::string>{args.data(), args.size()});
            if (!r) co_return r;

            auto redirect = client::parse_redirect(*r);
            if (!redirect) co_return r;
            if (attempt == max_redirects) {
                co_return std::unexpected(std::string("redis cluster redirect limit exceeded"));
            }

            if (redirect->kind == redirect_kind::moved) {
                slot_cache_.update_slot(redirect->slot, redirect->endpoint);
            }

            auto* next = co_await connection_for(redirect->endpoint);
            if (!next) {
                co_return std::unexpected(std::string("redis cluster redirect connect failed"));
            }
            if (redirect->kind == redirect_kind::ask) {
                auto asking = co_await next->cmd({"ASKING"});
                if (!asking) co_return asking;
                if (has_error(*asking)) {
                    co_return std::unexpected(std::string(error_message(*asking)));
                }
            }
        }

        co_return std::unexpected(std::string("redis cluster command failed"));
    }

    auto pipeline(std::span<const cluster_pipeline_item> items)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        std::map<std::string, std::vector<std::vector<std::string>>> grouped;
        std::map<std::string, endpoint_info> endpoints;

        for (const auto& item : items) {
            if (item.args.empty()) {
                co_return std::unexpected(std::string("empty command in cluster pipeline"));
            }
            auto slot = client::key_slot(item.key);
            auto ep = slot_cache_.endpoint_for_slot(slot);
            if (!ep) {
                auto refreshed = co_await refresh_slots();
                if (!refreshed) co_return std::unexpected(refreshed.error());
                ep = slot_cache_.endpoint_for_slot(slot);
            }
            if (!ep) {
                co_return std::unexpected(std::string("redis cluster slot not covered"));
            }
            auto key = endpoint_key(*ep);
            endpoints[key] = *ep;
            grouped[key].push_back(item.args);
        }

        std::vector<resp3_node> all;
        for (auto& [key, commands] : grouped) {
            auto* conn = co_await connection_for(endpoints[key]);
            if (!conn) co_return std::unexpected(std::string("redis cluster node connect failed"));
            auto r = co_await conn->pipe(std::span<const std::vector<std::string>>{
                commands.data(), commands.size()});
            if (!r) co_return std::unexpected(r.error());
            for (auto& node : *r) {
                all.push_back(std::move(node));
            }
        }
        co_return all;
    }

    [[nodiscard]] auto slots() const noexcept -> const cluster_slot_cache& {
        return slot_cache_;
    }

private:
    static auto endpoint_key(const endpoint_info& ep) -> std::string {
        const bool ipv6 = ep.host.find(':') != std::string::npos &&
            !(ep.host.starts_with("[") && ep.host.ends_with("]"));
        return (ipv6 ? "[" + ep.host + "]" : ep.host) + ":" + std::to_string(ep.port);
    }

    auto connection_for(const endpoint_info& ep) -> task<client*> {
        auto key = endpoint_key(ep);
        auto it = nodes_.find(key);
        if (it != nodes_.end() && it->second->is_open()) {
            co_return it->second.get();
        }

        auto next_opts = seed_options_;
        next_opts.host = ep.host;
        next_opts.port = ep.port;

        auto conn = std::make_unique<client>(ctx_);
        auto r = co_await conn->connect(next_opts);
        if (!r) {
            co_return nullptr;
        }
        auto* ptr = conn.get();
        nodes_[key] = std::move(conn);
        co_return ptr;
    }

    io_context& ctx_;
    connect_options seed_options_;
    client seed_;
    cluster_slot_cache slot_cache_;
    std::map<std::string, std::unique_ptr<client>> nodes_;
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

namespace detail {

inline constexpr std::array<std::uint16_t, 256> crc16_table = [] {
    std::array<std::uint16_t, 256> table{};
    for (std::uint16_t i = 0; i < 256; ++i) {
        std::uint16_t crc = static_cast<std::uint16_t>(i << 8);
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<std::uint16_t>(
                (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1));
        }
        table[i] = crc;
    }
    return table;
}();

inline auto crc16(std::string_view text) noexcept -> std::uint16_t {
    std::uint16_t crc = 0;
    for (unsigned char ch : text) {
        crc = static_cast<std::uint16_t>((crc << 8) ^
            crc16_table[((crc >> 8) ^ ch) & 0x00FF]);
    }
    return crc;
}

inline auto cluster_hash_key(std::string_view key) noexcept -> std::string_view {
    auto open = key.find('{');
    if (open == std::string_view::npos) return key;
    auto close = key.find('}', open + 1);
    if (close == std::string_view::npos || close == open + 1) return key;
    return key.substr(open + 1, close - open - 1);
}

} // namespace detail

auto client::key_slot(std::string_view key) noexcept -> std::uint16_t {
    return static_cast<std::uint16_t>(detail::crc16(detail::cluster_hash_key(key)) % 16384);
}

auto cluster_slot_cache::endpoint_for_key(std::string_view key) const
    -> std::optional<endpoint_info>
{
    return endpoint_for_slot(client::key_slot(key));
}

auto client::parse_redirect(const std::vector<resp3_node>& nodes)
    -> std::optional<cluster_redirect>
{
    auto message = error_message(nodes);
    if (message.empty()) return std::nullopt;

    redirect_kind kind{};
    std::string_view rest;
    if (message.starts_with("MOVED ")) {
        kind = redirect_kind::moved;
        rest = message.substr(6);
    } else if (message.starts_with("ASK ")) {
        kind = redirect_kind::ask;
        rest = message.substr(4);
    } else {
        return std::nullopt;
    }

    auto space = rest.find(' ');
    if (space == std::string_view::npos) return std::nullopt;
    auto slot_text = rest.substr(0, space);
    auto endpoint_text = rest.substr(space + 1);

    std::uint16_t slot = 0;
    auto [slot_ptr, slot_ec] =
        std::from_chars(slot_text.data(), slot_text.data() + slot_text.size(), slot);
    if (slot_ec != std::errc{}) return std::nullopt;

    std::string_view host;
    std::string_view port_text;
    if (!endpoint_text.empty() && endpoint_text.front() == '[') {
        auto bracket = endpoint_text.find(']');
        if (bracket == std::string_view::npos) return std::nullopt;
        if (bracket + 1 >= endpoint_text.size() || endpoint_text[bracket + 1] != ':') {
            return std::nullopt;
        }
        host = endpoint_text.substr(1, bracket - 1);
        port_text = endpoint_text.substr(bracket + 2);
    } else {
        auto colon = endpoint_text.rfind(':');
        if (colon == std::string_view::npos) return std::nullopt;
        host = endpoint_text.substr(0, colon);
        port_text = endpoint_text.substr(colon + 1);
    }
    if (host.empty() || port_text.empty()) return std::nullopt;

    std::uint16_t port = 0;
    auto [port_ptr, port_ec] =
        std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (port_ec != std::errc{}) return std::nullopt;

    return cluster_redirect{
        .kind = kind,
        .slot = slot,
        .endpoint = endpoint_info{.host = std::string(host), .port = port},
    };
}

namespace detail {

inline auto parse_u16(std::string_view text) -> std::optional<std::uint16_t> {
    std::uint16_t value = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{}) return std::nullopt;
    return value;
}

inline auto parse_cluster_endpoint(const std::vector<resp3_node>& nodes,
                                   std::size_t& index)
    -> std::expected<endpoint_info, std::string>
{
    if (index >= nodes.size() || !nodes[index].is_aggregate()) {
        return std::unexpected(std::string("cluster slots endpoint is not an array"));
    }

    auto field_count = nodes[index].aggregate_size;
    ++index;
    if (field_count < 2 || index + 1 >= nodes.size()) {
        return std::unexpected(std::string("cluster slots endpoint is incomplete"));
    }

    auto host = nodes[index++].value;
    auto port = parse_u16(nodes[index++].value);
    if (host.empty() || !port) {
        return std::unexpected(std::string("cluster slots endpoint host/port invalid"));
    }

    for (std::size_t skipped = 2; skipped < field_count && index < nodes.size(); ++skipped) {
        ++index;
    }

    return endpoint_info{.host = std::string(host), .port = *port};
}

} // namespace detail

auto client::parse_cluster_slots(const std::vector<resp3_node>& nodes)
    -> std::expected<std::vector<cluster_slot_range>, std::string>
{
    if (nodes.empty() || !nodes.front().is_aggregate()) {
        return std::unexpected(std::string("CLUSTER SLOTS response is not an array"));
    }

    std::vector<cluster_slot_range> ranges;
    ranges.reserve(nodes.front().aggregate_size);

    std::size_t index = 1;
    for (std::size_t range_i = 0; range_i < nodes.front().aggregate_size; ++range_i) {
        if (index >= nodes.size() || !nodes[index].is_aggregate()) {
            return std::unexpected(std::string("CLUSTER SLOTS range is not an array"));
        }

        auto range_fields = nodes[index].aggregate_size;
        ++index;
        if (range_fields < 3 || index + 2 >= nodes.size()) {
            return std::unexpected(std::string("CLUSTER SLOTS range is incomplete"));
        }

        auto start = detail::parse_u16(nodes[index++].value);
        auto end = detail::parse_u16(nodes[index++].value);
        if (!start || !end || *start > *end || *end > 16383) {
            return std::unexpected(std::string("CLUSTER SLOTS range bounds invalid"));
        }

        auto master = detail::parse_cluster_endpoint(nodes, index);
        if (!master) return std::unexpected(master.error());

        cluster_slot_range range{
            .start = *start,
            .end = *end,
            .master = *master,
            .replicas = {},
        };

        for (std::size_t field = 3; field < range_fields && index < nodes.size(); ++field) {
            auto replica = detail::parse_cluster_endpoint(nodes, index);
            if (!replica) return std::unexpected(replica.error());
            range.replicas.push_back(*replica);
        }

        ranges.push_back(std::move(range));
    }

    return ranges;
}

} // namespace cnetmod::redis
