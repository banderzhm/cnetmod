module;

#include <cnetmod/config.hpp>

#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

module cnetmod.protocol.http.v3.server;
import std;
import cnetmod.core.log;
import cnetmod.core.ssl;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.protocol.udp;
import cnetmod.executor.async_op;
import cnetmod.protocol.http.v3.session;
import cnetmod.protocol.quic;
import cnetmod.utils.concurrent_containers.atomic_hash_map;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import cnetmod.utils.concurrent_containers.queue;
import cnetmod.utils.concurrent_containers.copy_on_write_value;

namespace cnetmod::http::v3 {

namespace detail {

    class retry_token_manager
    {
    public:
        retry_token_manager()
            : secrets_(make_initial_secrets()) {}

        [[nodiscard]] auto ready() const noexcept -> bool
        {
            return secrets_.read()->ready;
        }

        [[nodiscard]] auto issue(const endpoint& sender, const quic::connection_id& odcid,
            const quic::connection_id& retry_scid) -> std::expected<std::vector<std::byte>, std::error_code>
        {
            rotate_if_due();
            const auto secrets = secrets_.read();
            if (!secrets->ready || odcid.empty() || retry_scid.empty())
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            auto payload = make_payload(sender, odcid, retry_scid,
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
            auto tag = authenticate(payload, secrets->current);
            if (!tag)
                return std::unexpected(tag.error());
            payload.insert(payload.end(), tag->begin(), tag->end());
            return payload;
        }

        [[nodiscard]] auto validate(std::span<const std::byte> token, const endpoint& sender,
            const quic::connection_id& retry_scid) -> std::expected<quic::connection_id, std::error_code>
        {
            rotate_if_due();
            const auto secrets = secrets_.read();
            constexpr std::size_t tag_size = 32;
            if (!secrets->ready || token.size() < 1U + 8U + 1U + 1U + tag_size)
                return std::unexpected(std::make_error_code(std::errc::permission_denied));
            const auto payload = token.first(token.size() - tag_size);
            auto current_tag = authenticate(payload, secrets->current);
            auto previous_tag = secrets->previous ? authenticate(payload, *secrets->previous)
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
            rotate_if_due();
            const auto secrets = secrets_.read();
            if (!secrets->ready || cid.empty())
                return std::unexpected(std::make_error_code(std::errc::io_error));
            static constexpr std::string_view label{"cnetmod quic stateless reset"};
            std::vector<std::byte> payload;
            payload.reserve(label.size() + cid.size());
            for (const char value : label)
                payload.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
            payload.insert(payload.end(), cid.data(), cid.data() + cid.size());
            auto tag = authenticate(payload, secrets->current);
            if (!tag)
                return std::unexpected(tag.error());
            std::array<std::byte, 16> token{};
            std::copy_n(tag->begin(), token.size(), token.begin());
            return token;
        }

    private:
        static constexpr std::chrono::seconds token_lifetime{10};
        static constexpr std::chrono::seconds secret_rotation_period{60};

        struct secret_state
        {
            std::array<unsigned char, 32> current{};
            std::optional<std::array<unsigned char, 32>> previous;
            std::chrono::steady_clock::time_point rotated_at;
            bool ready{};
        };

        [[nodiscard]] static auto make_initial_secrets() -> secret_state
        {
            secret_state result;
            result.rotated_at = std::chrono::steady_clock::now();
            result.ready = RAND_bytes(result.current.data(), result.current.size()) == 1;
            return result;
        }

        concurrent_containers::copy_on_write_value<secret_state> secrets_;

        auto rotate_if_due() -> void
        {
            const auto observed = secrets_.read();
            const auto now = std::chrono::steady_clock::now();
            if (!observed->ready || now - observed->rotated_at < secret_rotation_period)
                return;
            secrets_.update([](secret_state& state)
                {
                    const auto update_time = std::chrono::steady_clock::now();
                    if (!state.ready || update_time - state.rotated_at < secret_rotation_period)
                        return;
                    std::array<unsigned char, 32> next{};
                    if (RAND_bytes(next.data(), next.size()) != 1)
                    {
                        state.ready = false;
                        return;
                    }
                    state.previous = state.current;
                    state.current = next;
                    state.rotated_at = update_time;
                });
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
    struct inbox_budget_state
    {
        std::atomic<std::uint64_t> capacity_dropped{};
        std::atomic<std::uint64_t> budget_dropped{};
        std::atomic<std::uint64_t> creation_dropped{};
        std::atomic<std::uint64_t> queued{};
        std::atomic<std::uint64_t> peak_queued{};
        std::atomic<std::size_t> active_inboxes{};
    };

    struct dispatch_state
    {
        static constexpr std::size_t route_capacity{262144U};
        static constexpr std::size_t maximum_route_readers{1024U};

        struct route_entry
        {
            impl* owner{};
            quic::quic_connection* connection{};
            std::optional<std::chrono::steady_clock::time_point> expires_at;
        };

    private:
        // The dispatcher reads a CID route for every UDP datagram.  Atomic
        // shared_ptr snapshots are safe but turn that read into a pair of
        // reference-count RMW operations.  Route records are immutable and
        // reclaimed by this small, fixed epoch domain instead: readers only
        // perform acquire/release stores, while CID changes (the rare path)
        // pay the retirement/reclamation work.
        struct route_record
        {
            quic::connection_id cid;
            route_entry entry;
            bool tombstone{};
            std::uint64_t retired_epoch{};
            route_record* retired_next{};
        };

        struct alignas(64) route_slot
        {
            std::atomic<route_record*> value{};
        };

        struct alignas(64) reader_epoch
        {
            std::atomic<std::uint64_t> value{};
        };

        class read_guard
        {
        public:
            read_guard(dispatch_state& state, std::size_t reader) noexcept
                : state_(state), reader_(reader)
            {
                for (;;)
                {
                    const auto observed = state_.epoch_.load(std::memory_order_acquire);
                    state_.readers_[reader_].value.store(observed, std::memory_order_release);
                    if (observed == state_.epoch_.load(std::memory_order_acquire))
                        break;
                }
            }

            ~read_guard()
            {
                state_.readers_[reader_].value.store(0U, std::memory_order_release);
            }

            read_guard(const read_guard&) = delete;
            auto operator=(const read_guard&) -> read_guard& = delete;

        private:
            dispatch_state& state_;
            std::size_t reader_;
        };

        [[nodiscard]] auto slot_index(const quic::connection_id& cid) const noexcept
            -> std::size_t
        {
            return std::hash<quic::connection_id>{}(cid) & (route_capacity - 1U);
        }

        [[nodiscard]] auto locate(const quic::connection_id& cid) const noexcept
            -> std::pair<std::size_t, route_record*>
        {
            auto index = slot_index(cid);
            std::size_t reusable = route_capacity;
            route_record* reusable_record{};
            for (std::size_t probe{}; probe < route_capacity; ++probe)
            {
                auto* const current = routes_[index].value.load(std::memory_order_acquire);
                if (current == nullptr)
                {
                    if (reusable != route_capacity)
                        return {reusable, reusable_record};
                    return {index, current};
                }
                if (current->cid == cid)
                    return {index, current};
                if (current->tombstone && reusable == route_capacity)
                {
                    reusable = index;
                    reusable_record = current;
                }
                index = (index + 1U) & (route_capacity - 1U);
            }
            return reusable != route_capacity
                ? std::pair{reusable, reusable_record}
                : std::pair{route_capacity, static_cast<route_record*>(nullptr)};
        }

        void push_retired(route_record* record) noexcept
        {
            auto* head = retired_.load(std::memory_order_relaxed);
            do
            {
                record->retired_next = head;
            } while (!retired_.compare_exchange_weak(head, record,
                std::memory_order_release, std::memory_order_relaxed));
        }

        void retire(route_record* record) noexcept
        {
            if (record == nullptr)
                return;
            record->retired_epoch = epoch_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
            push_retired(record);
        }

        [[nodiscard]] auto safe_to_reclaim(std::uint64_t retired_epoch) const noexcept -> bool
        {
            for (const auto& reader : readers_)
            {
                const auto active = reader.value.load(std::memory_order_acquire);
                if (active != 0U && active <= retired_epoch)
                    return false;
            }
            return true;
        }

        void collect_retired() noexcept
        {
            auto* pending = retired_.exchange(nullptr, std::memory_order_acq_rel);
            while (pending != nullptr)
            {
                auto* const next = pending->retired_next;
                if (safe_to_reclaim(pending->retired_epoch))
                    delete pending;
                else
                    push_retired(pending);
                pending = next;
            }
        }

        [[nodiscard]] auto replace(const quic::connection_id& cid, route_entry entry,
            bool only_if_absent, std::size_t reader) -> bool
        {
            read_guard guard{*this, reader};
            for (;;)
            {
                const auto [index, current] = locate(cid);
                if (index == route_capacity)
                    return false;
                if (only_if_absent && current != nullptr && !current->tombstone)
                    return false;
                auto* replacement = new route_record{cid, std::move(entry)};
                auto* expected = current;
                if (routes_[index].value.compare_exchange_weak(expected, replacement,
                        std::memory_order_release, std::memory_order_acquire))
                {
                    retire(current);
                    collect_retired();
                    return true;
                }
                delete replacement;
            }
        }

        [[nodiscard]] auto erase_record(const quic::connection_id& cid,
            route_record* expected, std::size_t reader) -> bool
        {
            read_guard guard{*this, reader};
            auto [index, current] = locate(cid);
            if (index == route_capacity || current != expected ||
                current == nullptr || current->tombstone)
                return false;
            auto* replacement = new route_record{cid, {}, true};
            if (!routes_[index].value.compare_exchange_strong(current, replacement,
                    std::memory_order_release, std::memory_order_acquire))
            {
                delete replacement;
                return false;
            }
            retire(expected);
            collect_retired();
            return true;
        }

        [[nodiscard]] auto erase_owned(const quic::connection_id& cid,
            impl& owner, std::size_t reader) -> bool
        {
            read_guard guard{*this, reader};
            auto [index, current] = locate(cid);
            if (index == route_capacity || current == nullptr || current->tombstone ||
                current->entry.owner != std::addressof(owner))
                return false;
            auto* replacement = new route_record{cid, {}, true};
            auto* expected = current;
            if (!routes_[index].value.compare_exchange_strong(expected, replacement,
                    std::memory_order_release, std::memory_order_acquire))
            {
                delete replacement;
                return false;
            }
            retire(current);
            collect_retired();
            return true;
        }

    public:
        auto add_worker(impl& worker) -> std::size_t
        {
            // Workers are registered by the owning server before its
            // dispatcher starts. The vector is immutable on the hot path.
            if (workers.size() + 1U >= maximum_route_readers)
                std::terminate();
            workers.push_back(std::addressof(worker));
            return workers.size();
        }

        [[nodiscard]] auto select(const quic::connection_id& cid,
            bool retain_pending) -> std::optional<route_entry>
        {
            const auto now = std::chrono::steady_clock::now();
            {
                read_guard guard{*this, 0U};
                const auto [index, route] = locate(cid);
                if (index != route_capacity && route != nullptr && !route->tombstone)
                {
                    if (!route->entry.expires_at || *route->entry.expires_at > now)
                        return route->entry;
                    auto* replacement = new route_record{cid, {}, true};
                    auto* expected = route;
                    if (routes_[index].value.compare_exchange_strong(expected, replacement,
                            std::memory_order_release, std::memory_order_acquire))
                    {
                        retire(route);
                        collect_retired();
                    }
                    else
                        delete replacement;
                }
            }
            if (workers.empty())
                return std::nullopt;
            auto* selected = workers[next_worker.fetch_add(1U, std::memory_order_relaxed) % workers.size()];
            route_entry target{selected, nullptr, std::nullopt};
            if (retain_pending)
            {
                target.expires_at = now + std::chrono::seconds{10};
                if (!replace(cid, target, true, 0U))
                {
                    read_guard guard{*this, 0U};
                    const auto [index, winner] = locate(cid);
                    if (index != route_capacity && winner != nullptr && !winner->tombstone)
                        return winner->entry;
                    // Without a retained Initial route, later CRYPTO packets
                    // could reach a different worker. Drop it and let QUIC
                    // retransmission apply backpressure instead.
                    return std::nullopt;
                }
            }
            return target;
        }

        auto replace_connection_routes(impl& owner,
            quic::quic_connection& connection,
            std::span<const quic::quic_connection::local_cid_route> active) -> void
        {
            for (const auto& route : active)
                (void)replace(route.cid,
                    route_entry{std::addressof(owner), std::addressof(connection), std::nullopt},
                    false, owner.route_epoch_slot_);
        }

        auto retain_reset_route(impl& owner, const quic::connection_id& cid) -> void
        {
            (void)replace(cid, route_entry{std::addressof(owner), nullptr, std::nullopt},
                false, owner.route_epoch_slot_);
        }

        auto erase(impl& owner, const quic::connection_id& cid) -> void
        {
            (void)erase_owned(cid, owner, owner.route_epoch_slot_);
        }

        dispatch_state() : routes_(std::make_unique<route_slot[]>(route_capacity)) {}

        ~dispatch_state()
        {
            for (std::size_t index{}; index < route_capacity; ++index)
                delete routes_[index].value.load(std::memory_order_relaxed);
            auto* pending = retired_.exchange(nullptr, std::memory_order_relaxed);
            while (pending != nullptr)
            {
                auto* const next = pending->retired_next;
                delete pending;
                pending = next;
            }
        }

    private:
        std::unique_ptr<route_slot[]> routes_;
        std::array<reader_epoch, maximum_route_readers> readers_{};
        std::atomic<std::uint64_t> epoch_{1U};
        std::atomic<route_record*> retired_{};

    public:
        std::vector<impl*> workers;
        std::atomic<std::size_t> next_worker{};
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

    impl(io_context& context, ssl_context& tls, endpoint listen_endpoint,
        streaming_server_request_handler handler, bool shared_port = false)
        : impl(context, tls, std::move(listen_endpoint), server_request_handler{}, shared_port)
    {
        streaming_handler_ = std::move(handler);
    }

    impl(io_context& context, ssl_context& tls, endpoint listen_endpoint,
        async_webtransport_handler handler, bool shared_port = false)
        : impl(context, tls, std::move(listen_endpoint), server_request_handler{}, shared_port)
    {
        webtransport_handler_ = std::move(handler);
    }

    impl(io_context& context, ssl_context& tls, endpoint listen_endpoint,
        http3_server_handlers handlers, bool shared_port = false)
        : impl(context, tls, std::move(listen_endpoint), std::move(handlers.request), shared_port)
    {
        webtransport_handler_ = std::move(handlers.webtransport);
        push_cancellation_observer_ = std::move(handlers.push_cancelled);
    }

    impl(io_context& context, io_context& socket_context, ssl_context& tls,
        endpoint listen_endpoint, server_request_handler handler,
        udp::udp_socket& shared_socket,
        std::shared_ptr<detail::retry_token_manager> retry_tokens,
        std::shared_ptr<dispatch_state> dispatcher,
        std::shared_ptr<inbox_budget_state> inbox_budget)
        : context_(context), socket_context_(socket_context), tls_(tls), endpoint_(std::move(listen_endpoint)), handler_(std::move(handler)), socket_(context), datagram_socket_(std::addressof(shared_socket)), retry_tokens_(std::move(retry_tokens)), dispatcher_(std::move(dispatcher)), inbox_budget_(std::move(inbox_budget)), processor_only_(true)
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
        std::shared_ptr<dispatch_state> dispatcher,
        std::shared_ptr<inbox_budget_state> inbox_budget)
        : impl(context, socket_context, tls, std::move(listen_endpoint),
              server_request_handler{}, shared_socket, std::move(retry_tokens),
              std::move(dispatcher), std::move(inbox_budget))
    {
        async_handler_ = std::move(handler);
    }

    impl(io_context& context, io_context& socket_context, ssl_context& tls,
        endpoint listen_endpoint, streaming_server_request_handler handler,
        udp::udp_socket& shared_socket,
        std::shared_ptr<detail::retry_token_manager> retry_tokens,
        std::shared_ptr<dispatch_state> dispatcher,
        std::shared_ptr<inbox_budget_state> inbox_budget)
        : impl(context, socket_context, tls, std::move(listen_endpoint),
              server_request_handler{}, shared_socket, std::move(retry_tokens),
              std::move(dispatcher), std::move(inbox_budget))
    {
        streaming_handler_ = std::move(handler);
    }

    impl(io_context& context, io_context& socket_context, ssl_context& tls,
        endpoint listen_endpoint, async_webtransport_handler handler,
        udp::udp_socket& shared_socket,
        std::shared_ptr<detail::retry_token_manager> retry_tokens,
        std::shared_ptr<dispatch_state> dispatcher,
        std::shared_ptr<inbox_budget_state> inbox_budget)
        : impl(context, socket_context, tls, std::move(listen_endpoint),
              server_request_handler{}, shared_socket, std::move(retry_tokens),
              std::move(dispatcher), std::move(inbox_budget))
    {
        webtransport_handler_ = std::move(handler);
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
                handler_, socket_, retry_tokens_, dispatcher_, inbox_budget_);
            shard->route_epoch_slot_ = dispatcher_->add_worker(*shard);
            shards_.push_back(std::move(shard));
        }
        #else
        for (unsigned index{}; index < context.worker_count(); ++index)
        {
            auto& worker = context.next_worker_io();
            auto shard = std::make_unique<impl>(worker, tls_, endpoint_,
                handler_, true);
            shard->inbox_budget_ = inbox_budget_;
            shards_.push_back(std::move(shard));
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

    impl(server_context& context, ssl_context& tls, endpoint listen_endpoint,
        streaming_server_request_handler handler)
        : impl(context, tls, std::move(listen_endpoint), server_request_handler{})
    {
        handler_ = {};
        streaming_handler_ = std::move(handler);
        for (auto& shard : shards_)
        {
            shard->handler_ = {};
            shard->streaming_handler_ = streaming_handler_;
        }
    }

    impl(server_context& context, ssl_context& tls, endpoint listen_endpoint,
        async_webtransport_handler handler)
        : impl(context, tls, std::move(listen_endpoint), server_request_handler{})
    {
        handler_ = {};
        webtransport_handler_ = std::move(handler);
        for (auto& shard : shards_)
        {
            shard->handler_ = {};
            shard->webtransport_handler_ = webtransport_handler_;
        }
    }

    impl(server_context& context, ssl_context& tls, endpoint listen_endpoint,
        http3_server_handlers handlers)
        : impl(context, tls, std::move(listen_endpoint), std::move(handlers.request))
    {
        webtransport_handler_ = std::move(handlers.webtransport);
        push_cancellation_observer_ = std::move(handlers.push_cancelled);
        for (auto& shard : shards_)
        {
            shard->webtransport_handler_ = webtransport_handler_;
            shard->push_cancellation_observer_ = push_cancellation_observer_;
        }
    }

    [[nodiscard]] auto start() -> std::expected<void, std::error_code>
    {
        if (running_)
            return {};
        // A stopped listener can be started again on a freshly opened UDP
        // socket.  Its shutdown token is one-shot, so clear it before any
        // receive coroutine can arm it.
        listener_receive_wakeup_.reset();
        if (!shards_.empty())
        {
        #ifdef CNETMOD_PLATFORM_WINDOWS
            if (!retry_tokens_->ready())
                return std::unexpected(std::make_error_code(std::errc::io_error));
            tls_.configure_alpn_server({"h3"});
            socket_options options;
            options.recv_buffer_size = 4 * 1024 * 1024;
            options.send_buffer_size = 4 * 1024 * 1024;
            // A shared listener sends stateless Retry from its accept context
            // before a connection has an affinity. The Windows registered-I/O
            // provider cannot issue that control-plane send through ordinary
            // IOCP on a RIO socket, which drops the Retry and prevents the
            // handshake. Keep the shared multi-worker listener on IOCP's
            // batched UDP path; dedicated RIO sockets remain available where
            // their receive/send ownership is one execution context.
            options.registered_io = false;
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

    auto set_max_datagram_frame_size(std::uint64_t bytes) -> std::expected<void, std::error_code>
    {
        if (running_)
            return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
        transport_config_.max_datagram_frame_size = bytes;
        for (auto& shard : shards_)
            shard->transport_config_.max_datagram_frame_size = bytes;
        return {};
    }

    auto set_multipath_initial_max_path_id(std::uint32_t maximum_path_id)
        -> std::expected<void, std::error_code>
    {
        if (running_)
            return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
        if (maximum_path_id == 0U)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        transport_config_.multipath_initial_max_path_id = maximum_path_id;
        for (auto& shard : shards_)
            shard->transport_config_.multipath_initial_max_path_id = maximum_path_id;
        return {};
    }

    auto set_path_mtu_discovery(std::uint64_t maximum_payload,
        std::chrono::milliseconds probe_interval,
        std::chrono::milliseconds initial_probe_delay)
        -> std::expected<void, std::error_code>
    {
        if (running_)
            return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
        if (maximum_payload < quic::min_initial_pkt_size ||
            probe_interval <= std::chrono::milliseconds::zero() ||
            initial_probe_delay < std::chrono::milliseconds::zero())
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        transport_config_.enable_path_mtu_discovery = true;
        transport_config_.max_path_mtu = maximum_payload;
        transport_config_.path_mtu_probe_interval = probe_interval;
        transport_config_.path_mtu_initial_probe_delay = initial_probe_delay;
        for (auto& shard : shards_)
        {
            shard->transport_config_.enable_path_mtu_discovery = true;
            shard->transport_config_.max_path_mtu = maximum_payload;
            shard->transport_config_.path_mtu_probe_interval = probe_interval;
            shard->transport_config_.path_mtu_initial_probe_delay = initial_probe_delay;
        }
        return {};
    }

    auto set_qpack_settings(std::uint64_t max_table_capacity,
        std::uint64_t max_blocked_streams) -> std::expected<void, std::error_code>
    {
        if (running_)
            return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
        if ((max_table_capacity == 0U) != (max_blocked_streams == 0U))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        h3_settings_.qpack_max_table_capacity = max_table_capacity;
        h3_settings_.qpack_blocked_streams = max_blocked_streams;
        for (auto& shard : shards_)
        {
            shard->h3_settings_.qpack_max_table_capacity = max_table_capacity;
            shard->h3_settings_.qpack_blocked_streams = max_blocked_streams;
        }
        return {};
    }

    auto set_push_cancellation_observer(
        server_push_cancellation_observer observer)
        -> std::expected<void, std::error_code>
    {
        if (running_)
            return std::unexpected(
                std::make_error_code(std::errc::operation_in_progress));
        push_cancellation_observer_ = std::move(observer);
        for (auto& shard : shards_)
            shard->push_cancellation_observer_ = push_cancellation_observer_;
        return {};
    }

    auto set_early_data_tickets(
        std::shared_ptr<quic::server_early_data_ticket_callbacks> callbacks,
        std::vector<std::byte> context) -> std::expected<void, std::error_code>
    {
        if (running_)
            return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
        if (callbacks && (context.empty() || !callbacks->seal || !callbacks->open || !callbacks->replay_cache))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        transport_config_.early_data_tickets = std::move(callbacks);
        transport_config_.early_data_context = std::move(context);
        for (auto& shard : shards_)
        {
            shard->transport_config_.early_data_tickets = transport_config_.early_data_tickets;
            shard->transport_config_.early_data_context = transport_config_.early_data_context;
        }
        return {};
    }

    auto set_inbox_limits(http3_inbox_limits limits)
        -> std::expected<void, std::error_code>
    {
        if (running_)
            return std::unexpected(std::make_error_code(std::errc::operation_in_progress));
        if (limits.per_connection_datagrams < 2U ||
            limits.max_queued_datagrams < 2U ||
            limits.max_connection_inboxes == 0U)
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        inbox_limits_ = limits;
        for (auto& shard : shards_)
            shard->inbox_limits_ = limits;
        return {};
    }

    [[nodiscard]] auto stop() -> task<void>
    {
        running_ = false;
        // Closing a UDP descriptor alone does not reliably wake a readiness
        // awaiter on every backend (notably epoll).  Cancel the listener
        // receive first so its awaiter unregisters and resumes before this
        // coroutine eventually releases the socket.
        listener_receive_wakeup_.cancel();
        timer_wakeup_.cancel();
        if (!shards_.empty())
        {
        #ifdef CNETMOD_PLATFORM_WINDOWS
            socket_.close();
        #endif
            for (auto& shard : shards_)
                co_await shard->stop();
            co_return;
        }
        std::vector<std::shared_ptr<http3_server_session>> sessions;
        std::vector<std::shared_ptr<quic::quic_connection>> connections;
        {
            concurrent_containers::exclusive_latch_guard state_guard{state_latch_};
            sessions.reserve(sessions_.size());
            for (auto& [_, session] : sessions_)
                sessions.push_back(session);
            connections.reserve(connections_.size());
            for (auto& [_, connection] : connections_)
                connections.push_back(connection);
            sessions_.clear();
            connections_.clear();
            published_route_generations_.clear();
            timer_generations_.clear();
            timer_queue_ = {};
            timer_wait_deadline_.reset();
        }
        for (auto& session : sessions)
        {
            co_await session->send_goaway(std::numeric_limits<quic::stream_id>::max());
            co_await session->close();
        }
        for (auto& connection : connections)
            if (!connection->is_closed())
                co_await connection->async_close({}, "HTTP/3 listener stopping");
        if (!processor_only_)
            socket_.close();
        co_return;
    }

    [[nodiscard]] auto is_running() const noexcept -> bool
    {
        return running_.load(std::memory_order_acquire);
    }

    [[nodiscard]] auto statistics() const noexcept -> http3_server_statistics
    {
        auto result = http3_server_statistics{
            inbox_enqueued_datagrams_.load(std::memory_order_relaxed),
            dropped_inbox_datagrams_.load(std::memory_order_relaxed),
            inbox_budget_->active_inboxes.load(std::memory_order_relaxed),
            inbox_budget_->queued.load(std::memory_order_relaxed),
            inbox_budget_->peak_queued.load(std::memory_order_relaxed),
            inbox_budget_->budget_dropped.load(std::memory_order_relaxed),
            inbox_budget_->capacity_dropped.load(std::memory_order_relaxed),
            inbox_budget_->creation_dropped.load(std::memory_order_relaxed),
            pooled_receive_datagrams_.load(std::memory_order_relaxed),
            heap_receive_datagrams_.load(std::memory_order_relaxed)};
        #ifdef CNETMOD_PLATFORM_WINDOWS
        if (!processor_only_ && datagram_socket_ != nullptr)
        {
            const auto socket_statistics = datagram_socket_->native_socket().async_statistics();
            result.rio_receive_datagrams = socket_statistics.receive_completions;
            result.rio_send_datagrams = socket_statistics.send_completions;
            result.rio_receive_queue_dropped_datagrams = socket_statistics.receive_queue_drops;
            result.rio_fallbacks = socket_statistics.registered_io_fallback ? 1U : 0U;
        }
        #endif
        for (const auto& shard : shards_)
        {
            const auto shard_result = shard->statistics();
            // Server-context shards share the budget above; only their
            // per-worker ingress/allocator counters are additive.
            result.inbox_enqueued_datagrams += shard_result.inbox_enqueued_datagrams;
            result.inbox_dropped_datagrams += shard_result.inbox_dropped_datagrams;
            result.pooled_receive_datagrams += shard_result.pooled_receive_datagrams;
            result.heap_receive_datagrams += shard_result.heap_receive_datagrams;
        }
        return result;
    }

private:
    struct stateless_reset_route
    {
        std::array<std::byte, 16> token;
        std::chrono::steady_clock::time_point expires_at;
    };

    static constexpr auto stateless_reset_retention = std::chrono::seconds{30};

    struct timer_entry
    {
        std::chrono::steady_clock::time_point deadline;
        std::shared_ptr<quic::quic_connection> connection;
        std::uint64_t generation{};
    };

    struct timer_entry_later
    {
        [[nodiscard]] auto operator()(const timer_entry& left,
            const timer_entry& right) const noexcept -> bool
        {
            return left.deadline > right.deadline;
        }
    };

    struct timer_schedule
    {
        std::chrono::steady_clock::time_point deadline;
        std::uint64_t generation{};
    };

    auto schedule_connection_timer(const std::shared_ptr<quic::quic_connection>& connection) -> void
    {
        const auto deadline = connection->next_timer_deadline();
        concurrent_containers::exclusive_latch_guard state_guard{state_latch_};
        if (!deadline)
        {
            timer_generations_.erase(connection.get());
            return;
        }
        if (const auto existing = timer_generations_.find(connection.get());
            existing != timer_generations_.end())
        {
            // An earlier heap wakeup is still valid when packet activity only
            // moves the idle deadline later. Let that one poll once, then
            // publish the new deadline; this prevents one heap allocation per
            // received datagram on busy long-lived connections.
            if (*deadline >= existing->second.deadline)
                return;
        }
        const auto generation = timer_generations_.contains(connection.get())
            ? timer_generations_[connection.get()].generation + 1U
            : 1U;
        timer_generations_.insert_or_assign(connection.get(), timer_schedule{*deadline, generation});
        timer_queue_.push(timer_entry{*deadline, connection, generation});
        // The timer loop sleeps directly until the heap head. A newly
        // published earlier deadline must wake it; later work is already
        // covered by the armed deadline and does not need another wakeup.
        if (timer_wait_deadline_ && *deadline < *timer_wait_deadline_)
            timer_wakeup_.cancel();
    }

    auto retain_reset_route(const quic::quic_connection::local_cid_route& route) -> void
    {
        if (std::ranges::all_of(route.stateless_reset_token,
                [](std::byte value)
                {
                    return value == std::byte{};
                }))
            return;
        {
            concurrent_containers::exclusive_latch_guard state_guard{state_latch_};
            reset_routes_.insert_or_assign(route.cid, stateless_reset_route{route.stateless_reset_token, std::chrono::steady_clock::now() + stateless_reset_retention});
        }
        if (dispatcher_)
            dispatcher_->retain_reset_route(*this, route.cid);
    }

    auto refresh_connection_routes(const std::shared_ptr<quic::quic_connection>& connection) -> void
    {
        const bool closing = connection->is_closed() ||
            connection->state() == quic::connection_state::draining;
        const auto route_generation = connection->local_cid_route_generation();
        bool route_changed{};
        {
            concurrent_containers::shared_latch_guard state_guard{state_latch_};
            const auto known = published_route_generations_.find(connection.get());
            route_changed = known == published_route_generations_.end() ||
                known->second != route_generation;
        }
        // CID vectors are copied to publish dispatcher snapshots.  This is a
        // rare control-plane action: do not pay for it after every packet
        // when the connection's CID generation is unchanged.
        if (!route_changed && !closing)
            return;

        const auto retired = connection->take_retired_local_cid_routes();
        for (const auto& route : retired)
            retain_reset_route(route);
        const auto routes = connection->local_cid_routes();
        bool replace_dispatcher_routes{};
        {
            concurrent_containers::exclusive_latch_guard state_guard{state_latch_};
            const auto known = published_route_generations_.find(connection.get());
            route_changed = known == published_route_generations_.end() ||
                known->second != route_generation;
            if (route_changed)
            {
                std::erase_if(connections_, [&connection](const auto& entry)
                    {
                        return entry.second == connection;
                    });

                for (const auto& route : routes)
                    connections_.emplace(route.cid, connection);
                published_route_generations_.insert_or_assign(connection.get(), route_generation);
                replace_dispatcher_routes = dispatcher_ != nullptr;
            }
            if (closing)
            {
                std::erase_if(connections_, [&connection](const auto& entry)
                    {
                        return entry.second == connection;
                    });
                sessions_.erase(connection.get());
                published_route_generations_.erase(connection.get());
                timer_generations_.erase(connection.get());
            }
        }
        if (replace_dispatcher_routes)
            dispatcher_->replace_connection_routes(*this, *connection, routes);
        if (closing)
        {
            for (const auto& route : routes)
                retain_reset_route(route);
            if (dispatcher_)
                remove_connection_inboxes(*connection);
        }
    }

    auto discard_expired_reset_routes() -> void
    {
        const auto now = std::chrono::steady_clock::now();
        concurrent_containers::exclusive_latch_guard state_guard{state_latch_};
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
        if (dispatcher_)
        {
            const auto stale_before = now - std::chrono::seconds{10};
            const auto removed = inboxes_.erase_if([stale_before](
                                                       const inbox_key& key,
                                                       const std::shared_ptr<connection_inbox>& inbox)
                {
                    return key.connection == nullptr &&
                        inbox->promoted_connection.load(std::memory_order_acquire) == nullptr &&
                        inbox->created_at <= stale_before;
                });
            if (removed != 0U)
                inbox_budget_->active_inboxes.fetch_sub(removed, std::memory_order_release);
        }
    }

    auto ensure_http3_session(const std::shared_ptr<quic::quic_connection>& connection) -> void
    {
        if (connection->state() != quic::connection_state::connected)
            return;
        {
            concurrent_containers::shared_latch_guard state_guard{state_latch_};
            if (sessions_.contains(connection.get()))
                return;
        }
        auto session = std::shared_ptr<http3_server_session>{webtransport_handler_ && async_handler_
                ? make_http3_server_session(*connection,
                      http3_server_handlers{async_handler_, webtransport_handler_,
                          push_cancellation_observer_})
                : webtransport_handler_ ? make_http3_server_session(*connection, webtransport_handler_)
                : streaming_handler_    ? make_http3_server_session(*connection, streaming_handler_)
                : async_handler_        ? make_http3_server_session(*connection, async_handler_)
                                        : make_http3_server_session(*connection, handler_)};
        session->configure_local_settings(h3_settings_);
        session->configure_push_cancellation_observer(
            push_cancellation_observer_);
        {
            concurrent_containers::exclusive_latch_guard state_guard{state_latch_};
            if (sessions_.contains(connection.get()))
                return;
            sessions_.emplace(connection.get(), session);
        }
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
        std::optional<stateless_reset_route> route;
        {
            concurrent_containers::shared_latch_guard state_guard{state_latch_};
            if (const auto found = reset_routes_.find(dcid); found != reset_routes_.end())
                route = found->second;
        }
        if (!route || datagram.size() < 21U)
            co_return;

        std::vector<std::byte> reset(datagram.size());
        if (RAND_bytes(reinterpret_cast<unsigned char*>(reset.data()),
                static_cast<int>(reset.size())) != 1)
            co_return;
        reset.front() = static_cast<std::byte>(
            (std::to_integer<std::uint8_t>(reset.front()) & 0x3fU) | 0x40U);
        std::copy(route->token.begin(), route->token.end(),
            reset.end() - static_cast<std::ptrdiff_t>(route->token.size()));
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

        // Keep packet-level diagnosis opt-in.  This is deliberately before
        // long-header parsing so a browser Initial that is rejected by an
        // early invariant remains observable.

        // Never retain an unordered_map iterator after releasing
        // connections_mutex_. CID retirement/issuance can rehash this map on
        // another worker; a copied shared_ptr is the stable route hand-off.
        std::shared_ptr<quic::quic_connection> connection;
        const auto first = std::to_integer<std::uint8_t>(buffer.front());
        if ((first & 0x80U) != 0U)
        {
            auto header = quic::decode_long_header(buffer);
            if (!header)
            {

                co_return;
            }

            {
                concurrent_containers::shared_latch_guard state_guard{state_latch_};
                if (const auto route = connections_.find(header->dcid);
                    route != connections_.end())
                    connection = route->second;
            }
            if (!connection &&
                header->type == quic::packet_type::initial)
            {
                if ((header->version != static_cast<std::uint32_t>(quic::quic_version::v1) &&
                        header->version != static_cast<std::uint32_t>(quic::quic_version::v2)) ||
                    header->dcid.empty())
                {

                    co_return;
                }
                if (header->token.empty())
                {
                    if (buffer.size() < quic::min_initial_pkt_size)
                    {

                        co_return;
                    }
                    std::array<std::byte, 8> retry_cid_bytes{};
                    if (RAND_bytes(reinterpret_cast<unsigned char*>(retry_cid_bytes.data()),
                            retry_cid_bytes.size()) != 1)
                        co_return;
                    const auto retry_scid = quic::connection_id{retry_cid_bytes.data(),
                        static_cast<std::uint8_t>(retry_cid_bytes.size())};
                    auto token = retry_tokens_->issue(sender, header->dcid, retry_scid);
                    if (!token)
                    {
                        co_return;
                    }
                    auto retry = detail::make_retry_packet(header->version, header->scid,
                        retry_scid, *token, header->dcid);
                    if (!retry)
                        co_return;
                    if (std::addressof(context_) != std::addressof(socket_context_))
                        co_await post_awaitable{socket_context_};
                        // A shared Windows RIO receive ring is kept persistent,
                        // but stateless Retry is a control-plane packet and must
                        // remain available even if the provider rejects RIO send
                        // registration. Submit it explicitly through the socket
                        // context's IOCP and resume there; this also avoids
                        // mixing a Retry with a connection worker's send queue.
        #ifdef CNETMOD_HAS_IOCP
                    (void)co_await async_sendto_on(socket_context_, socket_context_,
                        datagram_socket_->native_socket(),
                        const_buffer{retry->data(), retry->size()}, sender);
        #else
                    (void)co_await async_sendto(socket_context_,
                        datagram_socket_->native_socket(),
                        const_buffer{retry->data(), retry->size()}, sender);
        #endif

                    if (std::addressof(context_) != std::addressof(socket_context_))
                        co_await post_awaitable{context_};
                    co_return;
                }
                auto original_dcid = retry_tokens_->validate(
                    header->token, sender, header->dcid);
                if (!original_dcid)
                    co_return;
                connection = std::make_shared<quic::quic_connection>(context_,
                    socket_context_, *datagram_socket_, sender, quic::quic_role::server,
                    tls_, transport_config_);
                if (!connection->register_cid(header->dcid) ||
                    !connection->set_original_destination_connection_id(*original_dcid))
                    co_return;
                {
                    concurrent_containers::exclusive_latch_guard state_guard{state_latch_};
                    const auto [route, inserted] = connections_.emplace(header->dcid, connection);
                    if (!inserted)
                        connection = route->second;
                }
                if (dispatcher_)
                    promote_pending_inbox(header->dcid, *connection);
            }
        }
        else
        {
            const auto cid_length = transport_config_.cid_length;

            if ((first & 0x40U) == 0U || buffer.size() < 1U + cid_length + 4U)
                co_return;
            const auto dcid = quic::connection_id{buffer.data() + 1U, cid_length};

            {
                concurrent_containers::shared_latch_guard state_guard{state_latch_};
                if (const auto route = connections_.find(dcid);
                    route != connections_.end())
                    connection = route->second;
            }

            if (!connection)
            {
                co_await send_stateless_reset(buffer, dcid, sender);
                co_return;
            }
        }
        if (!connection)
            co_return;
        auto processed = co_await connection->process_datagram(buffer, sender);
        if (!processed)
        {

            refresh_connection_routes(connection);
            schedule_connection_timer(connection);
            co_return;
        }
        ensure_http3_session(connection);
        refresh_connection_routes(connection);
        schedule_connection_timer(connection);
    }

    void record_receive_buffer(const udp_received_datagram& incoming) noexcept
    {
        if (incoming.bytes.is_pooled())
            pooled_receive_datagrams_.fetch_add(1U, std::memory_order_relaxed);
        else
            heap_receive_datagrams_.fetch_add(1U, std::memory_order_relaxed);
    }

    struct connection_inbox
    {
        // Overflow deliberately behaves like UDP loss, which QUIC already
        // recovers through ACK/loss detection. Capacity is configured before
        // start and constrained again by the listener-wide budget.
        explicit connection_inbox(std::size_t queue_capacity)
            : datagrams(queue_capacity) {}

        concurrent_containers::bounded_mpmc_queue<udp_received_datagram> datagrams;
        std::atomic_flag scheduled{};
        std::atomic<quic::quic_connection*> promoted_connection{};
        std::chrono::steady_clock::time_point created_at{
            std::chrono::steady_clock::now()};
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

    auto record_inbox_drop(std::atomic<std::uint64_t>& reason) -> void
    {
        reason.fetch_add(1U, std::memory_order_relaxed);
        const auto dropped = dropped_inbox_datagrams_.fetch_add(
                                 1U, std::memory_order_relaxed) +
            1U;
        // Power-of-two sampling keeps overload visible without turning the
        // drop path into a logger bottleneck.
        if ((dropped & (dropped - 1U)) == 0U)
            logger::warn{"HTTP/3 inbox overload; dropped {} UDP datagrams", dropped};
    }

    [[nodiscard]] auto reserve_inbox_datagram() -> bool
    {
        const auto previous = inbox_budget_->queued.fetch_add(
            1U, std::memory_order_acq_rel);
        if (previous >= inbox_limits_.max_queued_datagrams)
        {
            inbox_budget_->queued.fetch_sub(1U, std::memory_order_release);
            record_inbox_drop(inbox_budget_->budget_dropped);
            return false;
        }
        auto observed = inbox_budget_->peak_queued.load(std::memory_order_relaxed);
        const auto current = previous + 1U;
        while (current > observed &&
            !inbox_budget_->peak_queued.compare_exchange_weak(observed, current,
                std::memory_order_relaxed, std::memory_order_relaxed))
        {}
        return true;
    }

    auto release_inbox_datagram() noexcept -> void
    {
        inbox_budget_->queued.fetch_sub(1U, std::memory_order_release);
    }

    auto promote_pending_inbox(const quic::connection_id& pending_cid,
        quic::quic_connection& connection) -> void
    {
        const auto pending_key = inbox_key{nullptr, pending_cid};
        const auto pending = inboxes_.find(pending_key);
        if (!pending)
            return;
        // Keep the pending CID as an alias until the connection closes. A
        // dispatcher which had already selected the Initial route can still
        // enqueue safely, but both keys share one scheduled flag and therefore
        // one serial QUIC execution lane.
        (*pending)->promoted_connection.store(std::addressof(connection),
            std::memory_order_release);
        const inbox_key connection_key{std::addressof(connection), {}};
        if (inboxes_.try_emplace(connection_key, *pending))
            return;
        const auto existing = inboxes_.find(connection_key);
        if (!existing || *existing != *pending)
            logger::error{"HTTP/3 failed to preserve the pending inbox during CID promotion"};
    }

    auto remove_connection_inboxes(quic::quic_connection& connection) -> void
    {
        const auto removed = inboxes_.erase_if([target = std::addressof(connection)](
                                                   const inbox_key& key,
                                                   const std::shared_ptr<connection_inbox>& inbox)
            {
                return key.connection == target ||
                    inbox->promoted_connection.load(std::memory_order_acquire) == target;
            });
        if (removed != 0U)
            inbox_budget_->active_inboxes.fetch_sub(1U, std::memory_order_release);
    }

    auto enqueue(quic::quic_connection* connection,
        const quic::connection_id& dcid, udp_received_datagram incoming) -> void
    {
        const inbox_key key{connection, connection ? quic::connection_id{} : dcid};
        auto inbox = inboxes_.find(key);
        if (!inbox)
        {
            const auto previous = inbox_budget_->active_inboxes.fetch_add(1U,
                std::memory_order_acq_rel);
            if (previous >= inbox_limits_.max_connection_inboxes)
            {
                inbox_budget_->active_inboxes.fetch_sub(1U, std::memory_order_release);
                record_inbox_drop(inbox_budget_->creation_dropped);
                return;
            }
            auto candidate = std::make_shared<connection_inbox>(
                inbox_limits_.per_connection_datagrams);
            if (inboxes_.try_emplace(key, candidate))
                inbox = std::move(candidate);
            else
            {
                inbox_budget_->active_inboxes.fetch_sub(1U, std::memory_order_release);
                inbox = inboxes_.find(key);
            }
        }
        if (!inbox)
        {
            record_inbox_drop(inbox_budget_->creation_dropped);
            return;
        }
        if (!reserve_inbox_datagram())
            return;
        if (!(*inbox)->datagrams.try_enqueue(std::move(incoming)))
        {
            release_inbox_datagram();
            record_inbox_drop(inbox_budget_->capacity_dropped);
            return;
        }

        if (!(*inbox)->scheduled.test_and_set(std::memory_order_acq_rel))
            spawn(context_, drain_inbox(std::move(*inbox)));
        inbox_enqueued_datagrams_.fetch_add(1U, std::memory_order_relaxed);
    }

    [[nodiscard]] auto drain_inbox(std::shared_ptr<connection_inbox> inbox)
        -> task<void>
    {
        // A single busy connection must not retain an executor worker until
        // its queue happens to empty. Keep its QUIC state serialised through
        // `scheduled`, but periodically yield the worker so timers, control
        // streams and other connections can make progress.
        constexpr std::size_t drain_quantum = 64U;
        std::size_t processed_in_quantum{};
        while (running_)
        {
            auto incoming = inbox->datagrams.try_dequeue();
            if (!incoming)
            {
                inbox->scheduled.clear(std::memory_order_release);
                // A producer can enqueue between the failed dequeue and the
                // clear above. Reclaim scheduling ownership if that happened;
                // otherwise no work can be stranded without a drain task.
                if (inbox->datagrams.approximate_size() != 0U &&
                    !inbox->scheduled.test_and_set(std::memory_order_acq_rel))
                    continue;
                co_return;
            }
            release_inbox_datagram();
            discard_expired_reset_routes();
            co_await process_incoming(std::move(*incoming));
            if (++processed_in_quantum == drain_quantum &&
                inbox->datagrams.approximate_size() != 0U)
            {
                processed_in_quantum = 0U;
                co_await post_awaitable{context_};
            }
        }
        inbox->scheduled.clear(std::memory_order_release);
    }

    [[nodiscard]] auto run_loop() -> task<void>
    {
        while (running_)
        {
            discard_expired_reset_routes();
            // RIO owns a persistent receive ring and can safely return a
            // larger burst. The IOCP fallback drains a modest batch, then
            // yields after every packet so session/control work cannot be
            // starved by a permanently readable UDP socket.
            const bool registered_io = socket_.native_socket().registered_io_enabled();
            const auto receive_batch_size = registered_io ? std::size_t{32U}
                                                          : std::size_t{8U};
            auto received = co_await async_recvfrom_batch(context_,
                socket_.native_socket(), receive_batch_size,
                quic::max_udp_receive_payload, listener_receive_wakeup_);
            if (!received)
            {

                if (!running_)
                    break;
                continue;
            }

            for (std::size_t index{}; index < received->size(); ++index)
            {
                auto& incoming = (*received)[index];
                record_receive_buffer(incoming);
                co_await process_incoming(std::move(incoming));
                if (!registered_io && index + 1U < received->size())
                    co_await post_awaitable{context_};
            }

            // A readiness backend can keep completing full receive batches
            // synchronously while the UDP socket remains readable. Yield only
            // after a saturated batch: a short batch will suspend naturally
            // on the next receive, while unconditionally posting here adds a
            // full reactor turn to every low-latency request/response.
            if (received->size() == receive_batch_size && receive_batch_size > 1U)
                co_await post_awaitable{context_};
        }
    }

    [[nodiscard]] auto run_timer_loop() -> task<void>
    {
        while (running_)
        {
            discard_expired_reset_routes();
            const auto now = std::chrono::steady_clock::now();
            std::vector<std::shared_ptr<quic::quic_connection>> expired;
            {
                concurrent_containers::exclusive_latch_guard state_guard{state_latch_};
                while (!timer_queue_.empty() && timer_queue_.top().deadline <= now)
                {
                    auto entry = timer_queue_.top();
                    timer_queue_.pop();
                    const auto scheduled = timer_generations_.find(entry.connection.get());
                    if (scheduled == timer_generations_.end() ||
                        scheduled->second.generation != entry.generation)
                        continue;
                    timer_generations_.erase(scheduled);
                    expired.push_back(std::move(entry.connection));
                }
            }
            for (auto& connection : expired)
            {
                co_await connection->async_poll_timers();
                schedule_connection_timer(connection);
            }

            // No periodic scan: sleep until the heap head. Publishing an
            // earlier deadline cancels this wait, so ACK/PTO work remains
            // timely without a 1ms polling cadence.
            const auto now_after_poll = std::chrono::steady_clock::now();
            std::chrono::steady_clock::time_point deadline;
            {
                concurrent_containers::exclusive_latch_guard state_guard{state_latch_};
                deadline = timer_queue_.empty()
                    ? now_after_poll + std::chrono::hours{24}
                    : std::max(timer_queue_.top().deadline, now_after_poll);
                timer_wakeup_.reset();
                timer_wait_deadline_ = deadline;
            }
            (void)co_await async_timer_wait(context_, deadline - now_after_poll,
                timer_wakeup_);
            {
                concurrent_containers::exclusive_latch_guard state_guard{state_latch_};
                timer_wait_deadline_.reset();
            }
        }
    }

    [[nodiscard]] auto run_dispatch_loop() -> task<void>
    {
        while (running_)
        {
            auto received = co_await async_recvfrom_batch(context_,
                socket_.native_socket(), 64U, quic::max_udp_receive_payload,
                listener_receive_wakeup_);
            if (!received)
            {
                if (!running_)
                    break;
                continue;
            }
            for (auto& incoming : *received)
            {
                record_receive_buffer(incoming);
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
    streaming_server_request_handler streaming_handler_;
    async_webtransport_handler webtransport_handler_;
    server_push_cancellation_observer push_cancellation_observer_;
    udp::udp_socket socket_;
    udp::udp_socket* datagram_socket_{};
    // `run_loop` and `run_timer_loop` can be resumed by different IOCP
    // workers.  The maps and heap below form one structural control plane;
    // guard only their short, non-awaiting mutations with the project-owned
    // atomic latch. Packet processing and UDP I/O remain outside this latch.
    concurrent_containers::atomic_rw_latch state_latch_;
    std::unordered_map<quic::connection_id, std::shared_ptr<quic::quic_connection>> connections_;
    std::unordered_map<quic::quic_connection*, std::uint64_t> published_route_generations_;
    std::unordered_map<quic::quic_connection*, timer_schedule> timer_generations_;
    std::priority_queue<timer_entry, std::vector<timer_entry>, timer_entry_later> timer_queue_;
    std::optional<std::chrono::steady_clock::time_point> timer_wait_deadline_;
    // Owns the outstanding listener UDP receive. It is deliberately distinct
    // from per-connection cancellation so server shutdown never depends on
    // closing a descriptor to wake a platform readiness wait.
    cancel_token listener_receive_wakeup_;
    cancel_token timer_wakeup_;
    std::unordered_map<quic::quic_connection*, std::shared_ptr<http3_server_session>> sessions_;
    std::unordered_map<quic::connection_id, stateless_reset_route> reset_routes_;
    std::chrono::steady_clock::time_point next_reset_cleanup_{};
    quic::quic_config transport_config_{};
    http3_settings h3_settings_{};
    http3_inbox_limits inbox_limits_{};
    std::shared_ptr<detail::retry_token_manager> retry_tokens_;
    std::shared_ptr<dispatch_state> dispatcher_;
    std::shared_ptr<inbox_budget_state> inbox_budget_{
        std::make_shared<inbox_budget_state>()};
    // Slot zero is reserved for the shared-socket dispatcher.  Each Windows
    // shard receives its own epoch slot before the listener starts.
    std::size_t route_epoch_slot_{};
    std::vector<std::unique_ptr<impl>> shards_;
    concurrent_containers::atomic_hash_map<inbox_key, std::shared_ptr<connection_inbox>,
        inbox_key_hash>
        inboxes_{dispatch_state::route_capacity};
    bool processor_only_{};
    bool shared_port_{};
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> inbox_enqueued_datagrams_{};
    std::atomic<std::uint64_t> dropped_inbox_datagrams_{};
    std::atomic<std::uint64_t> pooled_receive_datagrams_{};
    std::atomic<std::uint64_t> heap_receive_datagrams_{};
};

http3_server::http3_server(io_context& context, ssl_context& tls, endpoint endpoint,
    server_request_handler handler)
    : impl_(std::make_unique<impl>(context, tls, std::move(endpoint), std::move(handler))) {}

http3_server::http3_server(io_context& context, ssl_context& tls, endpoint endpoint,
    async_server_request_handler handler)
    : impl_(std::make_unique<impl>(context, tls, std::move(endpoint), std::move(handler))) {}

http3_server::http3_server(io_context& context, ssl_context& tls, endpoint endpoint,
    streaming_server_request_handler handler)
    : impl_(std::make_unique<impl>(context, tls, std::move(endpoint), std::move(handler))) {}

http3_server::http3_server(io_context& context, ssl_context& tls, endpoint endpoint,
    async_webtransport_handler handler)
    : impl_(std::make_unique<impl>(context, tls, std::move(endpoint), std::move(handler))) {}

http3_server::http3_server(io_context& context, ssl_context& tls, endpoint endpoint,
    http3_server_handlers handlers)
    : impl_(std::make_unique<impl>(context, tls, std::move(endpoint), std::move(handlers))) {}

http3_server::http3_server(server_context& context, ssl_context& tls,
    endpoint endpoint, server_request_handler handler)
    : impl_(std::make_unique<impl>(context, tls, std::move(endpoint),
          std::move(handler))) {}

http3_server::http3_server(server_context& context, ssl_context& tls,
    endpoint endpoint, async_server_request_handler handler)
    : impl_(std::make_unique<impl>(context, tls, std::move(endpoint),
          std::move(handler))) {}

http3_server::http3_server(server_context& context, ssl_context& tls,
    endpoint endpoint, streaming_server_request_handler handler)
    : impl_(std::make_unique<impl>(context, tls, std::move(endpoint),
          std::move(handler))) {}

http3_server::http3_server(server_context& context, ssl_context& tls,
    endpoint endpoint, async_webtransport_handler handler)
    : impl_(std::make_unique<impl>(context, tls, std::move(endpoint),
          std::move(handler))) {}

http3_server::http3_server(server_context& context, ssl_context& tls,
    endpoint endpoint, http3_server_handlers handlers)
    : impl_(std::make_unique<impl>(context, tls, std::move(endpoint),
          std::move(handlers))) {}

http3_server::~http3_server() = default;

auto http3_server::start() -> std::expected<void, std::error_code>
{
    return impl_->start();
}

auto http3_server::set_push_cancellation_observer(
    server_push_cancellation_observer observer)
    -> std::expected<void, std::error_code>
{
    return impl_->set_push_cancellation_observer(std::move(observer));
}

auto http3_server::set_max_datagram_frame_size(std::uint64_t bytes)
    -> std::expected<void, std::error_code>
{
    return impl_->set_max_datagram_frame_size(bytes);
}

auto http3_server::set_multipath_initial_max_path_id(std::uint32_t maximum_path_id)
    -> std::expected<void, std::error_code>
{
    return impl_->set_multipath_initial_max_path_id(maximum_path_id);
}

auto http3_server::set_path_mtu_discovery(std::uint64_t maximum_payload,
    std::chrono::milliseconds probe_interval,
    std::chrono::milliseconds initial_probe_delay) -> std::expected<void, std::error_code>
{
    return impl_->set_path_mtu_discovery(maximum_payload, probe_interval,
        initial_probe_delay);
}

auto http3_server::set_qpack_settings(std::uint64_t max_table_capacity,
    std::uint64_t max_blocked_streams) -> std::expected<void, std::error_code>
{
    return impl_->set_qpack_settings(max_table_capacity, max_blocked_streams);
}

auto http3_server::set_inbox_limits(http3_inbox_limits limits)
    -> std::expected<void, std::error_code>
{
    return impl_->set_inbox_limits(limits);
}

auto http3_server::set_early_data_tickets(
    std::shared_ptr<quic::server_early_data_ticket_callbacks> callbacks,
    std::vector<std::byte> context) -> std::expected<void, std::error_code>
{
    return impl_->set_early_data_tickets(std::move(callbacks), std::move(context));
}

auto http3_server::stop() -> task<void>
{
    co_await impl_->stop();
}

auto http3_server::is_running() const noexcept -> bool
{
    return impl_->is_running();
}

auto http3_server::statistics() const noexcept -> http3_server_statistics
{
    return impl_->statistics();
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

auto make_http3_server(io_context& ctx, ssl_context& tls, endpoint ep,
    streaming_server_request_handler handler) -> std::unique_ptr<http3_server>
{
    return std::make_unique<http3_server>(ctx, tls, std::move(ep), std::move(handler));
}

auto make_http3_server(io_context& ctx, ssl_context& tls, endpoint ep,
    async_webtransport_handler handler) -> std::unique_ptr<http3_server>
{
    return std::make_unique<http3_server>(ctx, tls, std::move(ep), std::move(handler));
}

auto make_http3_server(io_context& ctx, ssl_context& tls, endpoint ep,
    http3_server_handlers handlers) -> std::unique_ptr<http3_server>
{
    return std::make_unique<http3_server>(ctx, tls, std::move(ep), std::move(handlers));
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

auto make_http3_server(server_context& ctx, ssl_context& tls, endpoint ep,
    streaming_server_request_handler handler) -> std::unique_ptr<http3_server>
{
    return std::make_unique<http3_server>(ctx, tls, std::move(ep), std::move(handler));
}

auto make_http3_server(server_context& ctx, ssl_context& tls, endpoint ep,
    async_webtransport_handler handler) -> std::unique_ptr<http3_server>
{
    return std::make_unique<http3_server>(ctx, tls, std::move(ep), std::move(handler));
}

auto make_http3_server(server_context& ctx, ssl_context& tls, endpoint ep,
    http3_server_handlers handlers) -> std::unique_ptr<http3_server>
{
    return std::make_unique<http3_server>(ctx, tls, std::move(ep), std::move(handlers));
}

} // namespace cnetmod::http::v3
