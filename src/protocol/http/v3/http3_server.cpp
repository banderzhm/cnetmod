module;

#include <cnetmod/config.hpp>

#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <cstdio>

#ifdef CNETMOD_ENABLE_QUIC
    #ifdef CNETMOD_HAS_SSL

module cnetmod.protocol.http.v3.server;
import std;
import cnetmod.core.ssl;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.protocol.udp;
import cnetmod.executor.async_op;
import cnetmod.protocol.http.v3.session;
import cnetmod.protocol.quic;

namespace cnetmod::http::v3 {

namespace detail {

    class retry_token_manager
    {
    public:
        retry_token_manager() noexcept
            : rotated_at_(std::chrono::steady_clock::now()),
              ready_(RAND_bytes(current_secret_.data(), current_secret_.size()) == 1) {}

        [[nodiscard]] auto ready() const noexcept -> bool
        {
            const std::scoped_lock lock(mutex_);
            return ready_;
        }

        [[nodiscard]] auto issue(const endpoint& sender, const quic::connection_id& odcid,
            const quic::connection_id& retry_scid) -> std::expected<std::vector<std::byte>, std::error_code>
        {
            const std::scoped_lock lock(mutex_);
            rotate_if_due();
            if (!ready_ || odcid.empty() || retry_scid.empty())
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            auto payload = make_payload(sender, odcid, retry_scid,
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
            auto tag = authenticate(payload, current_secret_);
            if (!tag)
                return std::unexpected(tag.error());
            payload.insert(payload.end(), tag->begin(), tag->end());
            return payload;
        }

        [[nodiscard]] auto validate(std::span<const std::byte> token, const endpoint& sender,
            const quic::connection_id& retry_scid) -> std::expected<quic::connection_id, std::error_code>
        {
            const std::scoped_lock lock(mutex_);
            rotate_if_due();
            constexpr std::size_t tag_size = 32;
            if (!ready_ || token.size() < 1U + 8U + 1U + 1U + tag_size)
                return std::unexpected(std::make_error_code(std::errc::permission_denied));
            const auto payload = token.first(token.size() - tag_size);
            auto current_tag = authenticate(payload, current_secret_);
            auto previous_tag = previous_secret_ ? authenticate(payload, *previous_secret_)
                                                 : std::expected<std::array<std::byte, 32>, std::error_code>{
                                                       std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory))};
            const auto token_tag = token.data() + payload.size();
            const bool current_matches = current_tag &&
                CRYPTO_memcmp(current_tag->data(), token_tag, tag_size) == 0;
            const bool previous_matches = previous_tag &&
                CRYPTO_memcmp(previous_tag->data(), token_tag, tag_size) == 0;
            if (!current_matches && !previous_matches)
                return std::unexpected(std::make_error_code(std::errc::permission_denied));
            std::size_t offset{};
            if (std::to_integer<std::uint8_t>(payload[offset++]) != 1U)
                return std::unexpected(std::make_error_code(std::errc::permission_denied));
            std::uint64_t issued{};
            for (std::size_t i{}; i < 8U; ++i)
                issued = (issued << 8U) | std::to_integer<std::uint8_t>(payload[offset++]);
            const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                                 .count();
            if (issued > static_cast<std::uint64_t>(now) ||
                static_cast<std::uint64_t>(now) - issued > token_lifetime.count())
                return std::unexpected(std::make_error_code(std::errc::timed_out));
            auto source = read_string(payload, offset);
            auto odcid = read_cid(payload, offset);
            auto embedded_retry_scid = read_cid(payload, offset);
            if (!source || !odcid || !embedded_retry_scid || offset != payload.size() ||
                *source != sender.to_string() || *embedded_retry_scid != retry_scid)
                return std::unexpected(std::make_error_code(std::errc::permission_denied));
            return *odcid;
        }

        [[nodiscard]] auto issue_stateless_reset_token(const quic::connection_id& cid)
            -> std::expected<std::array<std::byte, 16>, std::error_code>
        {
            const std::scoped_lock lock(mutex_);
            rotate_if_due();
            if (!ready_ || cid.empty())
                return std::unexpected(std::make_error_code(std::errc::io_error));
            static constexpr std::string_view label{"cnetmod quic stateless reset"};
            std::vector<std::byte> payload;
            payload.reserve(label.size() + cid.size());
            for (const char value : label)
                payload.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
            payload.insert(payload.end(), cid.data(), cid.data() + cid.size());
            auto tag = authenticate(payload, current_secret_);
            if (!tag)
                return std::unexpected(tag.error());
            std::array<std::byte, 16> token{};
            std::copy_n(tag->begin(), token.size(), token.begin());
            return token;
        }

    private:
        static constexpr std::chrono::seconds token_lifetime{10};
        static constexpr std::chrono::seconds secret_rotation_period{60};
        std::array<unsigned char, 32> current_secret_{};
        std::optional<std::array<unsigned char, 32>> previous_secret_;
        std::chrono::steady_clock::time_point rotated_at_;
        bool ready_{};
        mutable std::mutex mutex_;

        auto rotate_if_due() -> void
        {
            const auto now = std::chrono::steady_clock::now();
            if (!ready_ || now - rotated_at_ < secret_rotation_period)
                return;
            std::array<unsigned char, 32> next{};
            if (RAND_bytes(next.data(), next.size()) != 1)
            {
                ready_ = false;
                return;
            }
            previous_secret_ = current_secret_;
            current_secret_ = next;
            rotated_at_ = now;
        }

        [[nodiscard]] static auto make_payload(const endpoint& sender, const quic::connection_id& odcid,
            const quic::connection_id& retry_scid, std::int64_t timestamp) -> std::vector<std::byte>
        {
            std::vector<std::byte> result;
            result.reserve(96);
            result.push_back(std::byte{1});
            for (int shift = 56; shift >= 0; shift -= 8)
                result.push_back(static_cast<std::byte>((static_cast<std::uint64_t>(timestamp) >> shift) & 0xffU));
            append_string(result, sender.to_string());
            append_cid(result, odcid);
            append_cid(result, retry_scid);
            return result;
        }

        static auto append_string(std::vector<std::byte>& out, std::string_view value) -> void
        {
            out.push_back(static_cast<std::byte>(value.size()));
            for (const auto c : value)
                out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        }

        static auto append_cid(std::vector<std::byte>& out, const quic::connection_id& cid) -> void
        {
            out.push_back(static_cast<std::byte>(cid.size()));
            out.insert(out.end(), cid.data(), cid.data() + cid.size());
        }

        [[nodiscard]] static auto read_string(std::span<const std::byte> in, std::size_t& offset)
            -> std::optional<std::string>
        {
            if (offset >= in.size())
                return std::nullopt;
            const auto length = std::to_integer<std::uint8_t>(in[offset++]);
            if (offset + length > in.size())
                return std::nullopt;
            std::string value;
            value.reserve(length);
            for (std::size_t i{}; i < length; ++i)
                value.push_back(static_cast<char>(std::to_integer<unsigned char>(in[offset + i])));
            offset += length;
            return value;
        }

        [[nodiscard]] static auto read_cid(std::span<const std::byte> in, std::size_t& offset)
            -> std::optional<quic::connection_id>
        {
            if (offset >= in.size())
                return std::nullopt;
            const auto length = std::to_integer<std::uint8_t>(in[offset++]);
            if (length > quic::max_cid_length || offset + length > in.size())
                return std::nullopt;
            auto cid = quic::connection_id{in.data() + offset, length};
            offset += length;
            return cid;
        }

        [[nodiscard]] static auto authenticate(std::span<const std::byte> payload,
            const std::array<unsigned char, 32>& secret)
            -> std::expected<std::array<std::byte, 32>, std::error_code>
        {
            std::array<std::byte, 32> tag{};
            unsigned int length{};
            if (HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
                    reinterpret_cast<const unsigned char*>(payload.data()), payload.size(),
                    reinterpret_cast<unsigned char*>(tag.data()), &length) == nullptr ||
                length != tag.size())
                return std::unexpected(std::make_error_code(std::errc::io_error));
            return tag;
        }
    };

    [[nodiscard]] inline auto make_retry_packet(std::uint32_t wire_version,
        const quic::connection_id& client_scid, const quic::connection_id& retry_scid,
        std::span<const std::byte> token, const quic::connection_id& odcid)
        -> std::expected<std::vector<std::byte>, std::error_code>
    {
        const auto version = static_cast<quic::quic_version>(wire_version);
        if (version != quic::quic_version::v1 && version != quic::quic_version::v2)
            return std::unexpected(std::make_error_code(std::errc::protocol_not_supported));
        std::vector<std::byte> packet;
        packet.reserve(7U + client_scid.size() + retry_scid.size() + token.size() + 16U);
        packet.push_back(static_cast<std::byte>(version == quic::quic_version::v1 ? 0xf0U : 0xc0U));
        for (int shift = 24; shift >= 0; shift -= 8)
            packet.push_back(static_cast<std::byte>((wire_version >> shift) & 0xffU));
        packet.push_back(static_cast<std::byte>(client_scid.size()));
        packet.insert(packet.end(), client_scid.data(), client_scid.data() + client_scid.size());
        packet.push_back(static_cast<std::byte>(retry_scid.size()));
        packet.insert(packet.end(), retry_scid.data(), retry_scid.data() + retry_scid.size());
        packet.insert(packet.end(), token.begin(), token.end());
        auto integrity = quic::make_retry_integrity_tag(version, odcid, packet);
        if (!integrity)
            return std::unexpected(integrity.error());
        packet.insert(packet.end(), integrity->begin(), integrity->end());
        return packet;
    }

} // namespace detail

// A coroutine lambda stores captures in its closure, not necessarily in the
// coroutine frame.  The listener launches this task detached, so use ordinary
// value parameters to retain both objects until the session has stopped.
// This also makes the ownership boundary explicit: the session borrows the
// QUIC connection while this task owns shared references to both.
auto run_server_session(std::shared_ptr<http3_server_session> session,
    std::shared_ptr<quic::quic_connection> connection) -> task<void>
{
    (void)connection;
    co_await session->run();
}

/// HTTP/3 listener ownership.  QUIC packet demultiplexing is performed by the
/// transport layer; the server exposes lifecycle and request-session policy.
struct http3_server::impl
{
public:
    struct dispatch_state
    {
        struct route_entry
        {
            impl* owner{};
            quic::quic_connection* connection{};
            std::optional<std::chrono::steady_clock::time_point> expires_at;
        };

        auto add_worker(impl& worker) -> void
        {
            const std::scoped_lock lock(mutex);
            workers.push_back(std::addressof(worker));
        }

        [[nodiscard]] auto select(const quic::connection_id& cid,
            bool retain_pending) -> std::optional<route_entry>
        {
            const std::scoped_lock lock(mutex);
            const auto now = std::chrono::steady_clock::now();
            if (const auto route = routes.find(cid); route != routes.end())
            {
                if (!route->second.expires_at || *route->second.expires_at > now)
                    return route->second;
                routes.erase(route);
            }
            if (workers.empty())
                return std::nullopt;
            auto* selected = workers[next_worker++ % workers.size()];
            route_entry target{selected, nullptr, std::nullopt};
            if (retain_pending)
            {
                target.expires_at = now + std::chrono::seconds{10};
                routes.emplace(cid, target);
            }
            return target;
        }

        auto replace_connection_routes(impl& owner,
            quic::quic_connection& connection,
            std::span<const quic::quic_connection::local_cid_route> active) -> void
        {
            const std::scoped_lock lock(mutex);
            for (const auto& route : active)
                routes.insert_or_assign(route.cid,
                    route_entry{std::addressof(owner), std::addressof(connection), std::nullopt});
        }

        auto retain_reset_route(impl& owner, const quic::connection_id& cid) -> void
        {
            const std::scoped_lock lock(mutex);
            routes.insert_or_assign(cid,
                route_entry{std::addressof(owner), nullptr, std::nullopt});
        }

        auto erase(impl& owner, const quic::connection_id& cid) -> void
        {
            const std::scoped_lock lock(mutex);
            const auto route = routes.find(cid);
            if (route != routes.end() && route->second.owner == std::addressof(owner))
                routes.erase(route);
        }

        std::mutex mutex;
        std::unordered_map<quic::connection_id, route_entry> routes;
        std::vector<impl*> workers;
        std::size_t next_worker{};
    };

    impl(io_context& context, ssl_context& tls, endpoint listen_endpoint,
        server_request_handler handler, bool shared_port = false)
        : context_(context), socket_context_(context), tls_(tls), endpoint_(std::move(listen_endpoint)), handler_(std::move(handler)), socket_(context), datagram_socket_(std::addressof(socket_)), retry_tokens_(std::make_shared<detail::retry_token_manager>()), shared_port_(shared_port)
    {
        transport_config_.stateless_reset_token_generator = [tokens = retry_tokens_](const quic::connection_id& cid)
        {
            return tokens->issue_stateless_reset_token(cid);
        };
    }

    impl(io_context& context, ssl_context& tls, endpoint listen_endpoint,
        async_server_request_handler handler, bool shared_port = false)
        : impl(context, tls, std::move(listen_endpoint), server_request_handler{}, shared_port)
    {
        async_handler_ = std::move(handler);
    }

    impl(io_context& context, io_context& socket_context, ssl_context& tls,
        endpoint listen_endpoint, server_request_handler handler,
        udp::udp_socket& shared_socket,
        std::shared_ptr<detail::retry_token_manager> retry_tokens,
        std::shared_ptr<dispatch_state> dispatcher)
        : context_(context), socket_context_(socket_context), tls_(tls), endpoint_(std::move(listen_endpoint)), handler_(std::move(handler)), socket_(context), datagram_socket_(std::addressof(shared_socket)), retry_tokens_(std::move(retry_tokens)), dispatcher_(std::move(dispatcher)), processor_only_(true)
    {
        transport_config_.stateless_reset_token_generator = [tokens = retry_tokens_](const quic::connection_id& cid)
        {
            return tokens->issue_stateless_reset_token(cid);
        };
    }

    impl(io_context& context, io_context& socket_context, ssl_context& tls,
        endpoint listen_endpoint, async_server_request_handler handler,
        udp::udp_socket& shared_socket,
        std::shared_ptr<detail::retry_token_manager> retry_tokens,
        std::shared_ptr<dispatch_state> dispatcher)
        : impl(context, socket_context, tls, std::move(listen_endpoint),
              server_request_handler{}, shared_socket, std::move(retry_tokens),
              std::move(dispatcher))
    {
        async_handler_ = std::move(handler);
    }

    impl(server_context& context, ssl_context& tls, endpoint listen_endpoint,
        server_request_handler handler)
        : context_(context.accept_io()), socket_context_(context_), tls_(tls), endpoint_(std::move(listen_endpoint)), handler_(std::move(handler)), socket_(context_), datagram_socket_(std::addressof(socket_)), retry_tokens_(std::make_shared<detail::retry_token_manager>())
    {
        transport_config_.stateless_reset_token_generator = [tokens = retry_tokens_](const quic::connection_id& cid)
        {
            return tokens->issue_stateless_reset_token(cid);
        };
        shards_.reserve(context.worker_count());
        #ifdef CNETMOD_PLATFORM_WINDOWS
        dispatcher_ = std::make_shared<dispatch_state>();
        for (unsigned index{}; index < context.worker_count(); ++index)
        {
            auto& worker = context.next_worker_io();
            auto shard = std::make_unique<impl>(worker, context_, tls_, endpoint_,
                handler_, socket_, retry_tokens_, dispatcher_);
            dispatcher_->add_worker(*shard);
            shards_.push_back(std::move(shard));
        }
        #else
        for (unsigned index{}; index < context.worker_count(); ++index)
        {
            auto& worker = context.next_worker_io();
            shards_.push_back(std::make_unique<impl>(worker, tls_, endpoint_,
                handler_, true));
        }
        #endif
    }

    impl(server_context& context, ssl_context& tls, endpoint listen_endpoint,
        async_server_request_handler handler)
        : impl(context, tls, std::move(listen_endpoint), server_request_handler{})
    {
        handler_ = {};
        async_handler_ = std::move(handler);
        for (auto& shard : shards_)
        {
            shard->handler_ = {};
            shard->async_handler_ = async_handler_;
        }
    }

    [[nodiscard]] auto start() -> std::expected<void, std::error_code>
    {
        if (running_)
            return {};
        if (!shards_.empty())
        {
        #ifdef CNETMOD_PLATFORM_WINDOWS
            if (!retry_tokens_->ready())
                return std::unexpected(std::make_error_code(std::errc::io_error));
            tls_.configure_alpn_server({"h3"});
            socket_options options;
            options.recv_buffer_size = 4 * 1024 * 1024;
            options.send_buffer_size = 4 * 1024 * 1024;
            auto opened = socket_.open(endpoint_, options);
            if (!opened)
                return std::unexpected(opened.error());
            for (auto& shard : shards_)
            {
                shard->running_ = true;
                spawn(shard->context_, shard->run_timer_loop());
            }
            running_ = true;
            spawn(context_, run_dispatch_loop());
            return {};
        #else
            for (auto& shard : shards_)
            {
                auto started = shard->start();
                if (!started)
                    return std::unexpected(started.error());
            }
            running_ = true;
            return {};
        #endif
        }
        if (!retry_tokens_->ready())
            return std::unexpected(std::make_error_code(std::errc::io_error));
        // RFC 9114 requires HTTP/3 peers to negotiate an `h3` ALPN value.
        // The TLS context is application-owned so that certificates and
        // verification policy remain configurable, but the HTTP/3 listener
        // owns this protocol-specific selection policy.
        tls_.configure_alpn_server({"h3"});
        socket_options options;
        options.reuse_address = shared_port_;
        options.reuse_port = shared_port_;
        options.recv_buffer_size = 4 * 1024 * 1024;
        options.send_buffer_size = 4 * 1024 * 1024;
        auto opened = socket_.open(endpoint_, options);
        if (!opened)
            return std::unexpected(opened.error());
        running_ = true;
        spawn(context_, run_loop());
        spawn(context_, run_timer_loop());
        return {};
    }

    [[nodiscard]] auto stop() -> task<void>
    {
        running_ = false;
        if (!shards_.empty())
        {
        #ifdef CNETMOD_PLATFORM_WINDOWS
            socket_.close();
        #endif
            for (auto& shard : shards_)
                co_await shard->stop();
            co_return;
        }
        for (auto& [_, session] : sessions_)
        {
            co_await session->send_goaway(std::numeric_limits<quic::stream_id>::max());
            co_await session->close();
        }
        sessions_.clear();
        for (auto& [_, connection] : connections_)
            if (!connection->is_closed())
                co_await connection->async_close({}, "HTTP/3 listener stopping");
        connections_.clear();
        if (!processor_only_)
            socket_.close();
        co_return;
    }

    [[nodiscard]] auto is_running() const noexcept -> bool
    {
        return running_.load(std::memory_order_acquire);
    }

private:
    struct stateless_reset_route
    {
        std::array<std::byte, 16> token;
        std::chrono::steady_clock::time_point expires_at;
    };

    static constexpr auto stateless_reset_retention = std::chrono::seconds{30};

    auto retain_reset_route(const quic::quic_connection::local_cid_route& route) -> void
    {
        if (std::ranges::all_of(route.stateless_reset_token,
                [](std::byte value)
                {
                    return value == std::byte{};
                }))
            return;
        reset_routes_.insert_or_assign(route.cid, stateless_reset_route{route.stateless_reset_token, std::chrono::steady_clock::now() + stateless_reset_retention});
        if (dispatcher_)
            dispatcher_->retain_reset_route(*this, route.cid);
    }

    auto refresh_connection_routes(const std::shared_ptr<quic::quic_connection>& connection) -> void
    {
        for (const auto& route : connection->take_retired_local_cid_routes())
            retain_reset_route(route);

        std::erase_if(connections_, [&connection](const auto& entry)
            {
                return entry.second == connection;
            });

        const auto routes = connection->local_cid_routes();
        if (dispatcher_)
            dispatcher_->replace_connection_routes(*this, *connection, routes);
        if (connection->is_closed() ||
            connection->state() == quic::connection_state::draining)
        {
            for (const auto& route : routes)
                retain_reset_route(route);
            sessions_.erase(connection.get());
            if (dispatcher_)
            {
                const std::scoped_lock lock(inboxes_mutex_);
                inboxes_.erase(inbox_key{connection.get(), {}});
            }
            return;
        }
        for (const auto& route : routes)
            connections_.emplace(route.cid, connection);
    }

    auto discard_expired_reset_routes() -> void
    {
        const auto now = std::chrono::steady_clock::now();
        if (now < next_reset_cleanup_)
            return;
        next_reset_cleanup_ = now + std::chrono::seconds{1};
        std::erase_if(reset_routes_, [this, now](const auto& entry)
            {
                if (entry.second.expires_at > now)
                    return false;
                if (dispatcher_)
                    dispatcher_->erase(*this, entry.first);
                return true;
            });
    }

    auto ensure_http3_session(const std::shared_ptr<quic::quic_connection>& connection) -> void
    {
        if (connection->state() != quic::connection_state::connected || sessions_.contains(connection.get()))
            return;
        auto session = std::shared_ptr<http3_server_session>{async_handler_
                ? make_http3_server_session(*connection, async_handler_)
                : make_http3_server_session(*connection, handler_)};
        sessions_.emplace(connection.get(), session);
        // The listener map is bookkeeping, not coroutine ownership.  Keep the
        // session and its referenced QUIC connection alive until run() has
        // observed closure; otherwise stop()/route retirement can destroy a
        // raw session pointer while the detached coroutine is suspended in
        // async_accept_stream().
        spawn(context_, run_server_session(std::move(session), connection));
    }

    [[nodiscard]] auto send_stateless_reset(std::span<const std::byte> datagram,
        const quic::connection_id& dcid, const endpoint& sender) -> task<void>
    {
        // RFC 9000 §10.3: only a CID for which this endpoint previously
        // advertised a token is eligible.  Never reset arbitrary traffic.
        const auto route = reset_routes_.find(dcid);
        if (route == reset_routes_.end() || datagram.size() < 21U)
            co_return;

        std::vector<std::byte> reset(datagram.size());
        if (RAND_bytes(reinterpret_cast<unsigned char*>(reset.data()),
                static_cast<int>(reset.size())) != 1)
            co_return;
        reset.front() = static_cast<std::byte>(
            (std::to_integer<std::uint8_t>(reset.front()) & 0x3fU) | 0x40U);
        std::copy(route->second.token.begin(), route->second.token.end(),
            reset.end() - static_cast<std::ptrdiff_t>(route->second.token.size()));
        if (std::addressof(context_) != std::addressof(socket_context_))
            co_await post_awaitable{socket_context_};
        (void)co_await async_sendto(socket_context_, datagram_socket_->native_socket(),
            const_buffer{reset.data(), reset.size()}, sender);
        if (std::addressof(context_) != std::addressof(socket_context_))
            co_await post_awaitable{context_};
    }

    [[nodiscard]] auto process_incoming(udp_received_datagram incoming) -> task<void>
    {
        auto& buffer = incoming.bytes;
        const auto& sender = incoming.peer;
        if (buffer.empty())
            co_return;

        std::unordered_map<quic::connection_id,
            std::shared_ptr<quic::quic_connection>>::iterator route;
        const auto first = std::to_integer<std::uint8_t>(buffer.front());
        if ((first & 0x80U) != 0U)
        {
            auto header = quic::decode_long_header(buffer);
            if (!header)
                co_return;
            route = connections_.find(header->dcid);
            if (route == connections_.end() &&
                header->type == quic::packet_type::initial)
            {
                if ((header->version != static_cast<std::uint32_t>(quic::quic_version::v1) &&
                        header->version != static_cast<std::uint32_t>(quic::quic_version::v2)) ||
                    header->dcid.empty() || header->scid.empty())
                    co_return;
                if (header->token.empty())
                {
                    if (buffer.size() < quic::min_initial_pkt_size)
                        co_return;
                    std::array<std::byte, 8> retry_cid_bytes{};
                    if (RAND_bytes(reinterpret_cast<unsigned char*>(retry_cid_bytes.data()),
                            retry_cid_bytes.size()) != 1)
                        co_return;
                    const auto retry_scid = quic::connection_id{retry_cid_bytes.data(),
                        static_cast<std::uint8_t>(retry_cid_bytes.size())};
                    auto token = retry_tokens_->issue(sender, header->dcid, retry_scid);
                    if (!token)
                        co_return;
                    auto retry = detail::make_retry_packet(header->version, header->scid,
                        retry_scid, *token, header->dcid);
                    if (!retry)
                        co_return;
                    if (std::addressof(context_) != std::addressof(socket_context_))
                        co_await post_awaitable{socket_context_};
                    (void)co_await async_sendto(socket_context_,
                        datagram_socket_->native_socket(),
                        const_buffer{retry->data(), retry->size()}, sender);
                    if (std::addressof(context_) != std::addressof(socket_context_))
                        co_await post_awaitable{context_};
                    co_return;
                }
                auto original_dcid = retry_tokens_->validate(
                    header->token, sender, header->dcid);
                if (!original_dcid)
                    co_return;
                auto connection = std::make_shared<quic::quic_connection>(context_,
                    socket_context_, *datagram_socket_, sender, quic::quic_role::server,
                    tls_, transport_config_);
                if (!connection->register_cid(header->dcid) ||
                    !connection->set_original_destination_connection_id(*original_dcid))
                    co_return;
                route = connections_.emplace(header->dcid, std::move(connection)).first;
            }
        }
        else
        {
            const auto cid_length = transport_config_.cid_length;
            if ((first & 0x40U) == 0U || buffer.size() < 1U + cid_length + 4U)
                co_return;
            const auto dcid = quic::connection_id{buffer.data() + 1U, cid_length};
            route = connections_.find(dcid);
            if (route == connections_.end())
            {
                co_await send_stateless_reset(buffer, dcid, sender);
                co_return;
            }
        }
        if (route == connections_.end())
            co_return;
        const auto connection = route->second;
        auto processed = co_await connection->process_datagram(buffer, sender);
        if (!processed)
        {
            if (std::getenv("CNETMOD_QUIC_DIAG") != nullptr)
                std::fprintf(stderr,
                    "H3 listener packet rejected bytes=%zu error=%d\n",
                    buffer.size(), processed.error().value());
            refresh_connection_routes(connection);
            co_return;
        }
        ensure_http3_session(connection);
        refresh_connection_routes(connection);
    }

    struct connection_inbox
    {
        std::mutex mutex;
        std::deque<udp_received_datagram> datagrams;
        bool scheduled{};
    };

    struct inbox_key
    {
        quic::quic_connection* connection{};
        quic::connection_id pending_cid;

        auto operator==(const inbox_key&) const -> bool = default;
    };

    struct inbox_key_hash
    {
        auto operator()(const inbox_key& key) const noexcept -> std::size_t
        {
            if (key.connection)
                return std::hash<quic::quic_connection*>{}(key.connection);
            return std::hash<quic::connection_id>{}(key.pending_cid);
        }
    };

    auto enqueue(quic::quic_connection* connection,
        const quic::connection_id& dcid, udp_received_datagram incoming) -> void
    {
        const inbox_key key{connection, connection ? quic::connection_id{} : dcid};
        std::shared_ptr<connection_inbox> inbox;
        {
            const std::scoped_lock lock(inboxes_mutex_);
            auto [entry, inserted] = inboxes_.try_emplace(
                key, std::make_shared<connection_inbox>());
            (void)inserted;
            inbox = entry->second;
        }
        bool schedule{};
        {
            const std::scoped_lock lock(inbox->mutex);
            inbox->datagrams.push_back(std::move(incoming));
            if (!inbox->scheduled)
            {
                inbox->scheduled = true;
                schedule = true;
            }
        }
        if (schedule)
            spawn(context_, drain_inbox(std::move(inbox)));
    }

    [[nodiscard]] auto drain_inbox(std::shared_ptr<connection_inbox> inbox)
        -> task<void>
    {
        while (running_)
        {
            std::optional<udp_received_datagram> incoming;
            {
                const std::scoped_lock lock(inbox->mutex);
                if (inbox->datagrams.empty())
                {
                    inbox->scheduled = false;
                    co_return;
                }
                incoming.emplace(std::move(inbox->datagrams.front()));
                inbox->datagrams.pop_front();
            }
            discard_expired_reset_routes();
            co_await process_incoming(std::move(*incoming));
        }
        const std::scoped_lock lock(inbox->mutex);
        inbox->scheduled = false;
    }

    [[nodiscard]] auto run_loop() -> task<void>
    {
        while (running_)
        {
            discard_expired_reset_routes();
            auto received = co_await async_recvfrom_batch(context_,
                socket_.native_socket(), 32U, quic::max_udp_receive_payload);
            if (!received)
            {
                if (std::getenv("CNETMOD_QUIC_DIAG") != nullptr)
                    std::fprintf(stderr,
                        "H3 listener receive failed error=%d\n",
                        received.error().value());
                if (!running_)
                    break;
                continue;
            }
            if (std::getenv("CNETMOD_QUIC_DIAG") != nullptr)
            {
                diagnostic_receive_batches_ += 1U;
                diagnostic_receive_datagrams_ += received->size();
                if ((diagnostic_receive_batches_ & 0x3fU) == 0U)
                    std::fprintf(stderr,
                        "H3 listener receive batches=%llu datagrams=%llu last_batch=%zu\n",
                        static_cast<unsigned long long>(diagnostic_receive_batches_),
                        static_cast<unsigned long long>(diagnostic_receive_datagrams_),
                        received->size());
            }
            for (auto& incoming : *received)
                co_await process_incoming(std::move(incoming));

            // A readiness backend can keep completing full receive batches
            // synchronously while the UDP socket remains readable. Yield only
            // after a saturated batch: a short batch will suspend naturally
            // on the next receive, while unconditionally posting here adds a
            // full reactor turn to every low-latency request/response.
            if (received->size() == 32U)
                co_await post_awaitable{context_};
        }
    }

    [[nodiscard]] auto run_timer_loop() -> task<void>
    {
        while (running_)
        {
            co_await async_sleep(context_, std::chrono::milliseconds{1});
            std::unordered_map<quic::quic_connection*,
                std::shared_ptr<quic::quic_connection>>
                snapshot;
            for (const auto& [_, connection] : connections_)
                snapshot.try_emplace(connection.get(), connection);
            for (auto& [_, connection] : snapshot)
            {
                co_await connection->async_poll_timers();
                refresh_connection_routes(connection);
            }
        }
    }

    [[nodiscard]] auto run_dispatch_loop() -> task<void>
    {
        while (running_)
        {
            auto received = co_await async_recvfrom_batch(context_,
                socket_.native_socket(), 64U, quic::max_udp_receive_payload);
            if (!received)
            {
                if (!running_)
                    break;
                continue;
            }
            for (auto& incoming : *received)
            {
                if (incoming.bytes.empty())
                    continue;
                quic::connection_id dcid;
                bool retain_pending{};
                const auto first = std::to_integer<std::uint8_t>(incoming.bytes.front());
                if ((first & 0x80U) != 0U)
                {
                    auto header = quic::decode_long_header(incoming.bytes);
                    if (!header)
                        continue;
                    dcid = header->dcid;
                    retain_pending = header->type == quic::packet_type::initial;
                }
                else
                {
                    const auto cid_length = transport_config_.cid_length;
                    if ((first & 0x40U) == 0U ||
                        incoming.bytes.size() < 1U + cid_length + 4U)
                        continue;
                    dcid = quic::connection_id{
                        incoming.bytes.data() + 1U, cid_length};
                }
                if (auto target = dispatcher_->select(dcid, retain_pending))
                    target->owner->enqueue(
                        target->connection, dcid, std::move(incoming));
            }
        }
    }

    io_context& context_;
    io_context& socket_context_;
    ssl_context& tls_;
    endpoint endpoint_;
    server_request_handler handler_;
    async_server_request_handler async_handler_;
    udp::udp_socket socket_;
    udp::udp_socket* datagram_socket_{};
    std::unordered_map<quic::connection_id, std::shared_ptr<quic::quic_connection>> connections_;
    std::unordered_map<quic::quic_connection*, std::shared_ptr<http3_server_session>> sessions_;
    std::unordered_map<quic::connection_id, stateless_reset_route> reset_routes_;
    std::chrono::steady_clock::time_point next_reset_cleanup_{};
    quic::quic_config transport_config_{};
    std::shared_ptr<detail::retry_token_manager> retry_tokens_;
    std::shared_ptr<dispatch_state> dispatcher_;
    std::vector<std::unique_ptr<impl>> shards_;
    std::mutex inboxes_mutex_;
    std::unordered_map<inbox_key, std::shared_ptr<connection_inbox>, inbox_key_hash>
        inboxes_;
    bool processor_only_{};
    bool shared_port_{};
    std::atomic<bool> running_{false};
    std::uint64_t diagnostic_receive_batches_{};
    std::uint64_t diagnostic_receive_datagrams_{};
};

http3_server::http3_server(io_context& context, ssl_context& tls, endpoint endpoint,
    server_request_handler handler)
    : impl_(std::make_unique<impl>(context, tls, std::move(endpoint), std::move(handler))) {}

http3_server::http3_server(io_context& context, ssl_context& tls, endpoint endpoint,
    async_server_request_handler handler)
    : impl_(std::make_unique<impl>(context, tls, std::move(endpoint), std::move(handler))) {}

http3_server::http3_server(server_context& context, ssl_context& tls,
    endpoint endpoint, server_request_handler handler)
    : impl_(std::make_unique<impl>(context, tls, std::move(endpoint),
          std::move(handler))) {}

http3_server::http3_server(server_context& context, ssl_context& tls,
    endpoint endpoint, async_server_request_handler handler)
    : impl_(std::make_unique<impl>(context, tls, std::move(endpoint),
          std::move(handler))) {}

http3_server::~http3_server() = default;

auto http3_server::start() -> std::expected<void, std::error_code>
{
    return impl_->start();
}

auto http3_server::stop() -> task<void>
{
    co_await impl_->stop();
}

auto http3_server::is_running() const noexcept -> bool
{
    return impl_->is_running();
}

auto make_http3_server(io_context& ctx, ssl_context& tls, endpoint ep,
    server_request_handler handler) -> std::unique_ptr<http3_server>
{
    return std::make_unique<http3_server>(ctx, tls, std::move(ep), std::move(handler));
}

auto make_http3_server(io_context& ctx, ssl_context& tls, endpoint ep,
    async_server_request_handler handler) -> std::unique_ptr<http3_server>
{
    return std::make_unique<http3_server>(ctx, tls, std::move(ep), std::move(handler));
}

auto make_http3_server(server_context& ctx, ssl_context& tls, endpoint ep,
    server_request_handler handler) -> std::unique_ptr<http3_server>
{
    return std::make_unique<http3_server>(ctx, tls, std::move(ep),
        std::move(handler));
}

auto make_http3_server(server_context& ctx, ssl_context& tls, endpoint ep,
    async_server_request_handler handler) -> std::unique_ptr<http3_server>
{
    return std::make_unique<http3_server>(ctx, tls, std::move(ep),
        std::move(handler));
}

} // namespace cnetmod::http::v3

    #endif
#endif
