module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#endif

export module cnetmod.protocol.raft:tcp_transport;

import std;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.file;
import cnetmod.core.ssl;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.cancel;
import cnetmod.coro.mutex;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import cnetmod.protocol.tcp;
import :types;
import :node;
import :transport;
import :wire;

namespace cnetmod::raft
{
    namespace tcp_transport_detail
    {
        inline auto crc32(std::span<const std::byte> data, std::uint32_t crc = 0xffffffffu)
            -> std::uint32_t
        {
            for (auto byte : data)
            {
                crc ^= static_cast<std::uint8_t>(byte);
                for (auto bit = 0; bit < 8; ++bit)
                {
                    const auto mask = static_cast<std::uint32_t>(-(crc & 1u));
                    crc = (crc >> 1) ^ (0xedb88320u & mask);
                }
            }
            return crc;
        }

        inline auto crc32_finish(std::uint32_t crc) noexcept -> std::uint32_t
        {
            return crc ^ 0xffffffffu;
        }

        inline auto crc32_bytes(std::span<const std::byte> data) noexcept -> std::uint32_t
        {
            return crc32_finish(crc32(data));
        }

        inline auto normalize_fingerprint(std::string_view fingerprint) -> std::string
        {
            std::string normalized;
            normalized.reserve(fingerprint.size());
            for (unsigned char ch : fingerprint)
            {
                if (std::isxdigit(ch))
                    normalized.push_back(static_cast<char>(std::tolower(ch)));
            }
            return normalized;
        }

        inline auto constant_time_equal(std::string_view left, std::string_view right) noexcept -> bool
        {
            if (left.size() != right.size())
                return false;
            unsigned char diff = 0;
            for (std::size_t i = 0; i < left.size(); ++i)
                diff |= static_cast<unsigned char>(left[i] ^ right[i]);
            return diff == 0;
        }

        template <typename F>
        class thread_invoke_awaitable
        {
        public:
            using result_type = std::invoke_result_t<F>;

            thread_invoke_awaitable(io_context& ctx, F fn)
                : state_(std::make_shared<state>(&ctx, std::move(fn)))
            {
            }

            [[nodiscard]] auto await_ready() const noexcept -> bool
            {
                return false;
            }

            void await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                state_->continuation = continuation;
                auto st = state_;
                try
                {
                    std::thread([st = std::move(st)]() mutable
                    {
                        try
                        {
                            st->result.emplace(st->fn());
                        }
                        catch (...)
                        {
                            st->exception = std::current_exception();
                        }

                        auto* posted_state = new std::shared_ptr<state>(std::move(st));
                        (*posted_state)->ctx->post(&resume_on_context, posted_state, [](void* p) {
                            delete static_cast<std::shared_ptr<state>*>(p);
                        });
                    }).detach();
                }
                catch (...)
                {
                    state_->exception = std::current_exception();
                    auto* posted_state = new std::shared_ptr<state>(state_);
                    state_->ctx->post(&resume_on_context, posted_state, [](void* p) {
                        delete static_cast<std::shared_ptr<state>*>(p);
                    });
                }
            }

            auto await_resume() -> result_type
            {
                if (state_->exception)
                    std::rethrow_exception(state_->exception);
                return std::move(*state_->result);
            }

        private:
            struct state
            {
                state(io_context* target, F callable)
                    : ctx(target), fn(std::move(callable))
                {
                }

                io_context* ctx = nullptr;
                F fn;
                std::optional<result_type> result;
                std::exception_ptr exception;
                std::coroutine_handle<> continuation{};
            };

            static void resume_on_context(void* arg) noexcept
            {
                std::unique_ptr<std::shared_ptr<state>> posted_state{
                    static_cast<std::shared_ptr<state>*>(arg)
                };
                (*posted_state)->continuation.resume();
            }

            std::shared_ptr<state> state_;
        };

        template <typename F>
        auto thread_invoke(io_context& ctx, F&& fn)
        {
            return thread_invoke_awaitable<std::decay_t<F>>{
                ctx,
                std::decay_t<F>(std::forward<F>(fn)),
            };
        }

#ifdef CNETMOD_HAS_SSL
        inline auto peer_certificate_sha256(ssl_stream& stream)
            -> std::expected<std::string, std::error_code>
        {
            auto* cert = SSL_get1_peer_certificate(stream.native());
            if (!cert)
                return std::unexpected(std::make_error_code(std::errc::permission_denied));

            unsigned char digest[EVP_MAX_MD_SIZE]{};
            unsigned int digest_len = 0;
            if (X509_digest(cert, EVP_sha256(), digest, &digest_len) != 1)
            {
                X509_free(cert);
                return std::unexpected(make_ssl_error());
            }
            X509_free(cert);

            std::string out;
            out.reserve(digest_len * 2);
            for (unsigned int i = 0; i < digest_len; ++i)
                out += std::format("{:02x}", digest[i]);
            return out;
        }
#endif

        inline auto write_file_all(io_context& ctx,
                                   file& out,
                                   std::span<const std::byte> data,
                                   std::uint64_t offset)
            -> task<std::expected<void, std::error_code>>
        {
            std::size_t written = 0;
            while (written < data.size())
            {
                auto r = co_await async_file_write(ctx, out,
                                                   const_buffer{data.data() + written, data.size() - written},
                                                   offset + written);
                if (!r) co_return std::unexpected(r.error());
                if (*r == 0) co_return std::unexpected(std::make_error_code(std::errc::io_error));
                written += *r;
            }
            co_return {};
        }

        inline auto crc32_file(io_context& ctx, const std::filesystem::path& path)
            -> task<std::expected<std::uint32_t, std::error_code>>
        {
            auto stat = co_await async_file_stat(ctx, path);
            if (!stat) co_return std::unexpected(stat.error());

            auto opened = co_await async_file_open(ctx, path, open_mode::read);
            if (!opened) co_return std::unexpected(opened.error());
            auto input = std::move(*opened);

            std::array<std::byte, 64 * 1024> buffer{};
            auto crc = 0xffffffffu;
            std::uint64_t offset = 0;
            while (offset < stat->size)
            {
                const auto want = std::min<std::uint64_t>(buffer.size(), stat->size - offset);
                auto r = co_await async_file_read(ctx, input,
                                                  mutable_buffer{
                                                      buffer.data(), static_cast<std::size_t>(want)
                                                  }, offset);
                if (!r)
                {
                    (void)co_await async_file_close(ctx, input);
                    co_return std::unexpected(r.error());
                }
                if (*r == 0)
                {
                    (void)co_await async_file_close(ctx, input);
                    co_return std::unexpected(std::make_error_code(std::errc::io_error));
                }
                crc = crc32(std::span<const std::byte>{buffer.data(), *r}, crc);
                offset += *r;
            }

            auto close = co_await async_file_close(ctx, input);
            if (!close) co_return std::unexpected(close.error());
            co_return crc32_finish(crc);
        }

        inline auto read_exact(io_context& ctx, socket& sock, std::span<std::byte> data)
            -> task<std::expected<void, std::error_code>>
        {
            std::size_t done = 0;
            while (done < data.size())
            {
                auto r = co_await async_read(ctx, sock, mutable_buffer{data.data() + done, data.size() - done});
                if (!r) co_return std::unexpected(r.error());
                if (*r == 0) co_return std::unexpected(std::make_error_code(std::errc::connection_reset));
                done += *r;
            }
            co_return {};
        }

        inline auto read_exact(io_context& ctx, socket& sock, std::span<std::byte> data, cancel_token& token)
            -> task<std::expected<void, std::error_code>>
        {
            std::size_t done = 0;
            while (done < data.size())
            {
                auto r = co_await async_read(ctx, sock, mutable_buffer{data.data() + done, data.size() - done}, token);
                if (!r) co_return std::unexpected(r.error());
                if (*r == 0) co_return std::unexpected(std::make_error_code(std::errc::connection_reset));
                done += *r;
            }
            co_return {};
        }

        inline auto read_message(io_context& ctx, socket& sock)
            -> task<std::expected<raft_rpc_message, std::error_code>>
        {
            std::array<std::byte, 9> header{};
            if (auto r = co_await read_exact(ctx, sock, header); !r)
                co_return std::unexpected(r.error());

            std::uint32_t payload_size = 0;
            try
            {
                payload_size = raft_frame_payload_size(std::span<const std::byte, 9>{header});
            }
            catch (...)
            {
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }

            std::vector<std::byte> frame;
            frame.reserve(header.size() + payload_size);
            frame.insert(frame.end(), header.begin(), header.end());
            frame.resize(header.size() + payload_size);
            if (payload_size != 0)
            {
                if (auto r = co_await read_exact(ctx, sock, std::span<std::byte>{frame}.subspan(header.size())); !r)
                    co_return std::unexpected(r.error());
            }

            try
            {
                co_return decode_raft_message(frame);
            }
            catch (...)
            {
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }
        }

        inline auto read_message(io_context& ctx, socket& sock, cancel_token& token)
            -> task<std::expected<raft_rpc_message, std::error_code>>
        {
            std::array<std::byte, 9> header{};
            if (auto r = co_await read_exact(ctx, sock, header, token); !r)
                co_return std::unexpected(r.error());

            std::uint32_t payload_size = 0;
            try
            {
                payload_size = raft_frame_payload_size(std::span<const std::byte, 9>{header});
            }
            catch (...)
            {
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }

            std::vector<std::byte> frame;
            frame.reserve(header.size() + payload_size);
            frame.insert(frame.end(), header.begin(), header.end());
            frame.resize(header.size() + payload_size);
            if (payload_size != 0)
            {
                if (auto r = co_await read_exact(ctx, sock, std::span<std::byte>{frame}.subspan(header.size()), token); !r)
                    co_return std::unexpected(r.error());
            }

            try
            {
                co_return decode_raft_message(frame);
            }
            catch (...)
            {
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }
        }

#ifdef CNETMOD_HAS_SSL
        inline auto read_exact(ssl_stream& stream, std::span<std::byte> data)
            -> task<std::expected<void, std::error_code>>
        {
            std::size_t done = 0;
            while (done < data.size())
            {
                auto r = co_await stream.async_read(mutable_buffer{data.data() + done, data.size() - done});
                if (!r) co_return std::unexpected(r.error());
                if (*r == 0) co_return std::unexpected(std::make_error_code(std::errc::connection_reset));
                done += *r;
            }
            co_return {};
        }

        inline auto read_message(ssl_stream& stream)
            -> task<std::expected<raft_rpc_message, std::error_code>>
        {
            std::array<std::byte, 9> header{};
            if (auto r = co_await read_exact(stream, header); !r)
                co_return std::unexpected(r.error());

            std::uint32_t payload_size = 0;
            try
            {
                payload_size = raft_frame_payload_size(std::span<const std::byte, 9>{header});
            }
            catch (...)
            {
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }

            std::vector<std::byte> frame;
            frame.reserve(header.size() + payload_size);
            frame.insert(frame.end(), header.begin(), header.end());
            frame.resize(header.size() + payload_size);
            if (payload_size != 0)
            {
                if (auto r = co_await read_exact(stream, std::span<std::byte>{frame}.subspan(header.size())); !r)
                    co_return std::unexpected(r.error());
            }

            try
            {
                co_return decode_raft_message(frame);
            }
            catch (...)
            {
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }
        }
#endif

        inline auto send_raw(io_context& ctx, const endpoint& ep, const raft_rpc_message& message)
            -> task<std::expected<void, std::error_code>>
        {
            auto family = ep.address().is_v6() ? address_family::ipv6 : address_family::ipv4;
            auto sock_r = socket::create(family, socket_type::stream);
            if (!sock_r) co_return std::unexpected(sock_r.error());
            auto sock = std::move(*sock_r);
            if (auto r = sock.set_non_blocking(true); !r) co_return std::unexpected(r.error());

            if (auto r = co_await async_connect(ctx, sock, ep); !r)
                co_return std::unexpected(r.error());

            std::vector<std::byte> frame;
            try
            {
                frame = encode_raft_message(message);
            }
            catch (...)
            {
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }

            auto r = co_await async_write_all(ctx, sock,
                                              const_buffer{frame.data(), frame.size()});
            sock.close();
            co_return r;
        }
    } // namespace tcp_transport_detail

    export struct raft_tcp_peer
    {
        node_id id;
        endpoint address;
    };

    export struct raft_tcp_security_options
    {
        std::string shared_secret;
        bool require_auth_token = false;
        bool enable_tls = false;
        bool require_peer_certificate = false;
        std::map<node_id, std::string> peer_certificate_sha256;
#ifdef CNETMOD_HAS_SSL
        ssl_context* client_tls = nullptr;
        ssl_context* server_tls = nullptr;
#endif
    };

    export struct raft_snapshot_retention_options
    {
        std::size_t keep_last = 2;
        std::chrono::steady_clock::duration min_age{std::chrono::minutes{5}};
    };

    export struct raft_tcp_transport_options
    {
        std::uint32_t max_send_attempts = 3;
        std::chrono::milliseconds retry_backoff{20};
        std::filesystem::path snapshot_directory = "raft-snapshots";
        std::size_t snapshot_chunk_size = 1024 * 1024;
        std::chrono::milliseconds snapshot_session_timeout{std::chrono::minutes{5}};
        std::size_t max_outbound_queue = 1024;
        raft_tcp_security_options security;
        raft_snapshot_retention_options snapshot_retention;
    };

    export struct raft_peer_transport_metrics
    {
        node_id peer;
        bool connected = false;
        std::uint64_t queued_sends = 0;
        std::uint64_t send_successes = 0;
        std::uint64_t send_failures = 0;
        std::uint64_t reconnects = 0;
        std::uint64_t max_queue_depth = 0;
        std::uint64_t coalesced_sends = 0;
        std::chrono::steady_clock::duration last_queue_wait_latency{};
        std::chrono::steady_clock::duration last_send_latency{};
        std::chrono::steady_clock::time_point last_send_at{};
        std::chrono::steady_clock::time_point last_receive_at{};
        std::error_code last_error;
    };

    export class raft_tcp_transport final : public raft_transport
    {
    public:
        raft_tcp_transport(io_context& ctx,
                           node_id local_id,
                           raft_tcp_transport_options options = {})
            : ctx_(ctx),
              local_id_(std::move(local_id)),
              options_(options)
        {
        }

        void set_node(raft_node& node) noexcept
        {
            node_ = &node;
        }

        void set_error_handler(std::function<void(node_id, std::error_code)> handler)
        {
            on_error_ = std::move(handler);
        }

        void add_peer(raft_tcp_peer peer)
        {
            auto id = std::move(peer.id);
            peers_[id] = peer.address;
            peer_metrics_[id].peer = id;
        }

        void remove_peer(const node_id& peer)
        {
            peers_.erase(peer);
            if (auto it = connections_.find(peer); it != connections_.end())
            {
                it->second.outbound.clear();
                it->second.write_token.cancel();
                it->second.sock.close();
            }
        }

        [[nodiscard]] auto peers() const -> std::vector<node_id>
        {
            std::vector<node_id> out;
            out.reserve(peers_.size());
            for (const auto& [id, _] : peers_) out.push_back(id);
            return out;
        }

        [[nodiscard]] auto peer_metrics() const -> std::vector<raft_peer_transport_metrics>
        {
            std::vector<raft_peer_transport_metrics> out;
            out.reserve(peer_metrics_.size());
            for (const auto& [peer, metrics] : peer_metrics_)
            {
                auto item = metrics;
                if (auto it = connections_.find(peer); it != connections_.end())
                    item.connected = it->second.sock.is_open();
                out.push_back(std::move(item));
            }
            return out;
        }

        [[nodiscard]] auto peer_metrics(const node_id& peer) const
            -> std::optional<raft_peer_transport_metrics>
        {
            auto it = peer_metrics_.find(peer);
            if (it == peer_metrics_.end())
                return std::nullopt;
            auto out = it->second;
            if (auto conn = connections_.find(peer); conn != connections_.end())
                out.connected = conn->second.sock.is_open();
            return out;
        }

        void send_pre_vote(const node_id& peer,
                           const request_vote_request& request) override
        {
            send(peer, raft_rpc_message{
                     .type = raft_rpc_type::pre_vote,
                     .from = local_id_,
                     .to = peer,
                     .vote = request,
                 });
        }

        void send_request_vote(const node_id& peer,
                               const request_vote_request& request) override
        {
            send(peer, raft_rpc_message{
                     .type = raft_rpc_type::request_vote,
                     .from = local_id_,
                     .to = peer,
                     .vote = request,
                 });
        }

        void send_request_vote_response(const node_id& peer,
                                        const request_vote_response& response) override
        {
            send(peer, raft_rpc_message{
                     .type = raft_rpc_type::request_vote_response,
                     .from = local_id_,
                     .to = peer,
                     .vote_response = response,
                 });
        }

        void send_timeout_now(const node_id& peer,
                              const timeout_now_request& request) override
        {
            send(peer, raft_rpc_message{
                     .type = raft_rpc_type::timeout_now,
                     .from = local_id_,
                     .to = peer,
                     .timeout_now = request,
                 });
        }

        void send_append_entries(const node_id& peer,
                                 const append_entries_request& request) override
        {
            send(peer, raft_rpc_message{
                     .type = raft_rpc_type::append_entries,
                     .from = local_id_,
                     .to = peer,
                     .append = request,
                 });
        }

        void send_append_entries_response(const node_id& peer,
                                          const append_entries_response& response) override
        {
            send(peer, raft_rpc_message{
                     .type = raft_rpc_type::append_entries_response,
                     .from = local_id_,
                     .to = peer,
                     .append_response = response,
                 });
        }

        void send_install_snapshot(const node_id& peer,
                                   const install_snapshot_request& request) override
        {
            if (!request.uri.empty() && request.data.empty())
            {
                auto it = peers_.find(peer);
                if (it == peers_.end())
                {
                    notify_error(peer, std::make_error_code(std::errc::host_unreachable));
                    return;
                }
                spawn(ctx_, send_snapshot_file(peer, it->second, request));
                return;
            }
            if (!request.data.empty() && request.data.size() > request_chunk_size())
            {
                auto it = peers_.find(peer);
                if (it == peers_.end())
                {
                    notify_error(peer, std::make_error_code(std::errc::host_unreachable));
                    return;
                }
                spawn(ctx_, send_snapshot_memory(peer, it->second, request));
                return;
            }

            auto snapshot = request;
            if (!snapshot.data.empty() && snapshot.snapshot_id.empty())
            {
                snapshot.snapshot_id = default_snapshot_id(snapshot);
                snapshot.total_size = snapshot.data.size();
                snapshot.offset = 0;
                snapshot.chunk_crc32 = tcp_transport_detail::crc32_bytes(snapshot.data);
                snapshot.file_crc32 = snapshot.chunk_crc32;
                snapshot.done = true;
            }

            send(peer, raft_rpc_message{
                     .type = raft_rpc_type::install_snapshot,
                     .from = local_id_,
                     .to = peer,
                     .snapshot = std::move(snapshot),
                 });
        }

        void send_install_snapshot_response(const node_id& peer,
                                            const install_snapshot_response& response) override
        {
            send(peer, raft_rpc_message{
                     .type = raft_rpc_type::install_snapshot_response,
                     .from = local_id_,
                     .to = peer,
                     .snapshot_response = response,
                 });
        }

        void broadcast_pre_vote(raft_node& node)
        {
            auto req = node.begin_pre_vote();
            for (const auto& peer : peers()) send_pre_vote(peer, req);
        }

        void broadcast_request_vote(raft_node& node)
        {
            auto req = node.begin_election();
            for (const auto& peer : peers()) send_request_vote(peer, req);
        }

        void replicate_to_all(raft_node& node)
        {
            for (const auto& peer : peers())
            {
                if (node.should_send_snapshot(peer))
                {
                    auto req = node.make_install_snapshot(peer);
                    node.mark_snapshot_sent(peer);
                    send_install_snapshot(peer, req);
                }
                else
                {
                    auto req = node.make_append_entries(peer);
                    const auto last_sent = req.entries.empty()
                                               ? req.prev_log_index
                                               : req.entries.back().index;
                    node.mark_append_sent(peer, last_sent);
                    send_append_entries(peer, req);
                }
            }
        }

        auto transfer_leader(raft_node& node, const node_id& target)
            -> std::expected<void, raft_error>
        {
            auto request = node.transfer_leader(target);
            if (!request)
                return std::unexpected(request.error());
            if (*request)
            {
                send_timeout_now(target, **request);
            }
            else
            {
                replicate_to_all(node);
            }
            return {};
        }

        auto serve(endpoint listen_endpoint) -> task<std::expected<void, std::error_code>>
        {
            cancel_token token;
            co_return co_await serve(listen_endpoint, token);
        }

        auto serve(endpoint listen_endpoint, cancel_token& token)
            -> task<std::expected<void, std::error_code>>
        {
            tcp::acceptor acceptor{ctx_};
            socket_options opts{.reuse_address = true, .no_delay = true};
            if (auto r = acceptor.open(listen_endpoint, opts); !r)
                co_return std::unexpected(r.error());

            while (running_ && !token.is_cancelled())
            {
                token.reset();
                auto accepted = co_await async_accept(ctx_, acceptor.native_socket(), token);
                if (!accepted)
                {
                    if (token.is_cancelled())
                        co_return {};
                    co_return std::unexpected(accepted.error());
                }
                spawn(ctx_, handle_connection(std::move(*accepted)));
            }
            co_return {};
        }

        void stop() noexcept
        {
            running_ = false;
            for (auto& [_, conn] : connections_)
                conn.write_token.cancel();
            for (auto& [_, conn] : connections_)
                conn.sock.close();
            for (auto* token : inbound_tokens_)
            {
                if (token)
                    token->cancel();
            }
            for (auto* sock : inbound_sockets_)
            {
                if (sock)
                    sock->close();
            }
            for (auto* sock : outbound_tls_sockets_)
            {
                if (sock)
                    sock->close();
            }
        }

        auto cleanup_snapshot_sessions() -> std::size_t
        {
            return cleanup_snapshot_sessions(options_.snapshot_session_timeout);
        }

        auto cleanup_snapshot_sessions(std::chrono::steady_clock::duration timeout)
            -> std::size_t
        {
            const auto now = std::chrono::steady_clock::now();
            std::size_t removed = 0;
            for (auto it = snapshot_receives_.begin(); it != snapshot_receives_.end();)
            {
                if (now - it->second.last_activity >= timeout)
                {
                    std::error_code ec;
                    if (!it->second.temp_path.empty())
                        std::filesystem::remove(it->second.temp_path, ec);
                    it = snapshot_receives_.erase(it);
                    ++removed;
                }
                else
                {
                    ++it;
                }
            }
            for (auto it = snapshot_sends_.begin(); it != snapshot_sends_.end();)
            {
                if (now - it->second.last_activity >= timeout)
                {
                    it = snapshot_sends_.erase(it);
                    ++removed;
                }
                else
                {
                    ++it;
                }
            }
            return removed;
        }

        auto cleanup_snapshot_files()
            -> task<std::expected<std::size_t, std::error_code>>
        {
            auto directory = options_.snapshot_directory;
            auto retention = options_.snapshot_retention;
            co_return co_await tcp_transport_detail::thread_invoke(ctx_,
                [directory = std::move(directory), retention]
                {
                    return cleanup_snapshot_files_blocking(directory, retention);
                });
        }

    private:
        enum class snapshot_materialize_status
        {
            complete,
            pending,
            failed,
        };

        struct peer_connection
        {
            struct queued_message
            {
                endpoint ep;
                raft_rpc_message message;
                std::chrono::steady_clock::time_point enqueued_at;
            };

            socket sock;
            async_mutex write_lock;
            cancel_token write_token;
            std::deque<queued_message> outbound;
            bool writer_running = false;
        };

        struct inbound_socket_guard
        {
            raft_tcp_transport* owner = nullptr;
            socket* sock = nullptr;
            cancel_token* token = nullptr;

            inbound_socket_guard(raft_tcp_transport& transport, socket& inbound, cancel_token& inbound_token) noexcept
                : owner(&transport), sock(&inbound), token(&inbound_token)
            {
                owner->inbound_sockets_.insert(sock);
                owner->inbound_tokens_.insert(token);
            }

            ~inbound_socket_guard()
            {
                if (owner && token)
                    owner->inbound_tokens_.erase(token);
                if (owner && sock)
                    owner->inbound_sockets_.erase(sock);
            }

            inbound_socket_guard(const inbound_socket_guard&) = delete;
            auto operator=(const inbound_socket_guard&) -> inbound_socket_guard& = delete;
        };

        struct outbound_tls_socket_guard
        {
            raft_tcp_transport* owner = nullptr;
            socket* sock = nullptr;

            outbound_tls_socket_guard(raft_tcp_transport& transport, socket& outbound) noexcept
                : owner(&transport), sock(&outbound)
            {
                owner->outbound_tls_sockets_.insert(sock);
            }

            ~outbound_tls_socket_guard()
            {
                if (owner && sock)
                    owner->outbound_tls_sockets_.erase(sock);
            }

            outbound_tls_socket_guard(const outbound_tls_socket_guard&) = delete;
            auto operator=(const outbound_tls_socket_guard&) -> outbound_tls_socket_guard& = delete;
        };

        struct snapshot_receive_state
        {
            std::uint64_t expected_offset = 0;
            std::filesystem::path temp_path;
            std::filesystem::path final_path;
            std::chrono::steady_clock::time_point last_activity = std::chrono::steady_clock::now();
        };

        struct snapshot_send_state
        {
            endpoint ep;
            install_snapshot_request request;
            std::chrono::steady_clock::time_point last_activity = std::chrono::steady_clock::now();
        };

        struct snapshot_file_candidate
        {
            std::filesystem::path path;
            std::filesystem::file_time_type modified;
        };

        void send(const node_id& peer, raft_rpc_message message)
        {
            if (!running_)
                return;
            auto& metrics = metrics_for(peer);
            auto it = peers_.find(peer);
            if (it == peers_.end())
            {
                record_send_failure(peer, std::make_error_code(std::errc::host_unreachable), {});
                return;
            }
            if (metrics.queued_sends >= options_.max_outbound_queue)
            {
                record_send_failure(peer, std::make_error_code(std::errc::no_buffer_space), {});
                return;
            }

            ++metrics.queued_sends;
            message.auth_token = make_auth_token(message);
            auto& conn = connections_[peer];
            conn.outbound.push_back(peer_connection::queued_message{
                .ep = it->second,
                .message = std::move(message),
                .enqueued_at = std::chrono::steady_clock::now(),
            });
            metrics.max_queue_depth = std::max<std::uint64_t>(
                metrics.max_queue_depth,
                static_cast<std::uint64_t>(conn.outbound.size()));

            if (!conn.writer_running)
            {
                conn.writer_running = true;
                spawn(ctx_, send_writer(peer));
            }
        }

        void notify_error(const node_id& peer, std::error_code ec)
        {
            if (on_error_) on_error_(peer, ec);
        }

        auto send_writer(node_id peer) -> task<void>
        {
            for (;;)
            {
                if (!running_)
                {
                    if (auto conn_it = connections_.find(peer); conn_it != connections_.end())
                    {
                        auto& conn = conn_it->second;
                        conn.outbound.clear();
                        conn.writer_running = false;
                    }
                    co_return;
                }
                auto conn_it = connections_.find(peer);
                if (conn_it == connections_.end())
                    co_return;

                auto& conn = conn_it->second;
                if (conn.outbound.empty())
                {
                    conn.writer_running = false;
                    co_return;
                }

                auto item = std::move(conn.outbound.front());
                conn.outbound.pop_front();
                coalesce_append_entries(peer, conn, item);

                auto& metrics = metrics_for(peer);
                metrics.last_queue_wait_latency =
                    std::chrono::steady_clock::now() - item.enqueued_at;

                const auto started = std::chrono::steady_clock::now();
                conn.write_token.reset();
                auto r = co_await send_with_retry(peer, item.ep, item.message, conn.write_token);
                auto latency = std::chrono::steady_clock::now() - started;

                if (auto it = peer_metrics_.find(peer);
                    it != peer_metrics_.end() && it->second.queued_sends != 0)
                {
                    --it->second.queued_sends;
                }

                if (r)
                    record_send_success(peer, latency);
                else
                    record_send_failure(peer, r.error(), latency);
            }
        }

        void coalesce_append_entries(const node_id& peer,
                                     peer_connection& conn,
                                     peer_connection::queued_message& item)
        {
            if (item.message.type != raft_rpc_type::append_entries)
                return;

            auto& metrics = metrics_for(peer);
            while (!conn.outbound.empty() &&
                   conn.outbound.front().message.type == raft_rpc_type::append_entries &&
                   droppable_append_entries(item.message.append))
            {
                item = std::move(conn.outbound.front());
                conn.outbound.pop_front();
                ++metrics.coalesced_sends;
                if (metrics.queued_sends != 0)
                    --metrics.queued_sends;
            }
        }

        [[nodiscard]] static auto droppable_append_entries(const append_entries_request& request)
            -> bool
        {
            return request.entries.empty() && request.read_contexts.empty();
        }

        auto send_with_retry(const node_id& peer, endpoint ep, const raft_rpc_message& message, cancel_token& token)
            -> task<std::expected<void, std::error_code>>
        {
            std::error_code last_error;
            const auto attempts = std::max<std::uint32_t>(1, options_.max_send_attempts);
            for (std::uint32_t attempt = 0; attempt < attempts; ++attempt)
            {
                if (token.is_cancelled() || !running_)
                    co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
                auto r = options_.security.enable_tls
                    ? co_await send_tls_once(peer, ep, message, token)
                    : co_await send_pooled(peer, ep, message);
                if (r) co_return {};
                last_error = r.error();
                if (attempt + 1 < attempts && options_.retry_backoff.count() > 0)
                {
                    if (token.is_cancelled() || !running_)
                        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
                    (void)co_await async_timer_wait(ctx_, options_.retry_backoff);
                }
            }
            co_return std::unexpected(last_error);
        }

        auto handle_connection(socket sock) -> task<void>
        {
            cancel_token read_token;
            inbound_socket_guard inbound_guard{*this, sock, read_token};
            if (options_.security.enable_tls)
            {
#ifdef CNETMOD_HAS_SSL
                if (!options_.security.server_tls)
                {
                    sock.close();
                    co_return;
                }
                ssl_stream stream{*options_.security.server_tls, ctx_, sock};
                stream.set_accept_state();
                if (auto hs = co_await stream.async_handshake(); !hs)
                {
                    sock.close();
                    co_return;
                }
                auto message = co_await tcp_transport_detail::read_message(stream);
                while (message && node_)
                {
                    if (auto tls_peer = verify_tls_peer(stream, message->from); !tls_peer)
                    {
                        record_send_failure(message->from, tls_peer.error(), {});
                        break;
                    }
                    co_await dispatch_message(*message);
                    message = co_await tcp_transport_detail::read_message(stream);
                }
                (void)co_await stream.async_shutdown();
                sock.close();
                co_return;
#else
                sock.close();
                co_return;
#endif
            }
            auto message = co_await tcp_transport_detail::read_message(ctx_, sock, read_token);
            while (message && node_)
            {
                co_await dispatch_message(*message);
                read_token.reset();
                message = co_await tcp_transport_detail::read_message(ctx_, sock, read_token);
            }
            sock.close();
        }

        auto send_tls_once(const node_id& peer, const endpoint& ep, const raft_rpc_message& message, cancel_token& token)
            -> task<std::expected<void, std::error_code>>
        {
#ifdef CNETMOD_HAS_SSL
            if (!options_.security.client_tls)
                co_return std::unexpected(std::make_error_code(std::errc::protocol_not_supported));
            if (token.is_cancelled() || !running_)
                co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));

            auto family = ep.address().is_v6() ? address_family::ipv6 : address_family::ipv4;
            auto sock_r = socket::create(family, socket_type::stream);
            if (!sock_r) co_return std::unexpected(sock_r.error());
            auto sock = std::move(*sock_r);
            outbound_tls_socket_guard tls_guard{*this, sock};
            if (auto r = sock.set_non_blocking(true); !r)
                co_return std::unexpected(r.error());
            token.reset();
            if (token.is_cancelled() || !running_)
            {
                sock.close();
                co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
            }
            if (auto r = co_await async_connect(ctx_, sock, ep, token); !r)
                co_return std::unexpected(r.error());
            if (token.is_cancelled() || !running_)
            {
                sock.close();
                co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
            }

            ssl_stream stream{*options_.security.client_tls, ctx_, sock};
            stream.set_connect_state();
            stream.set_hostname(ep.address().to_string());
            if (auto hs = co_await stream.async_handshake(); !hs)
            {
                sock.close();
                co_return std::unexpected(hs.error());
            }
            if (auto tls_peer = verify_tls_peer(stream, peer); !tls_peer)
            {
                sock.close();
                co_return std::unexpected(tls_peer.error());
            }

            std::vector<std::byte> frame;
            try
            {
                frame = encode_raft_message(message);
            }
            catch (...)
            {
                sock.close();
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }
            auto sent = co_await stream.async_write_all(const_buffer{frame.data(), frame.size()});
            (void)co_await stream.async_shutdown();
            sock.close();
            co_return sent;
#else
            (void)peer;
            (void)ep;
            (void)message;
            (void)token;
            co_return std::unexpected(std::make_error_code(std::errc::protocol_not_supported));
#endif
        }

#ifdef CNETMOD_HAS_SSL
        [[nodiscard]] auto verify_tls_peer(ssl_stream& stream, const node_id& peer) const
            -> std::expected<void, std::error_code>
        {
            auto it = options_.security.peer_certificate_sha256.find(peer);
            if (it == options_.security.peer_certificate_sha256.end() || it->second.empty())
            {
                if (options_.security.require_peer_certificate)
                    return std::unexpected(std::make_error_code(std::errc::permission_denied));
                return {};
            }

            auto expected = tcp_transport_detail::normalize_fingerprint(it->second);
            if (expected.empty())
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));

            auto actual = tcp_transport_detail::peer_certificate_sha256(stream);
            if (!actual)
                return std::unexpected(actual.error());

            if (!tcp_transport_detail::constant_time_equal(*actual, expected))
                return std::unexpected(std::make_error_code(std::errc::permission_denied));
            return {};
        }
#endif

        auto send_pooled(const node_id& peer, const endpoint& ep, const raft_rpc_message& message)
            -> task<std::expected<void, std::error_code>>
        {
            auto& conn = connections_[peer];
            co_await conn.write_lock.lock();
            async_lock_guard guard{conn.write_lock, std::adopt_lock};

            if (!conn.sock.is_open())
            {
                auto family = ep.address().is_v6() ? address_family::ipv6 : address_family::ipv4;
                auto sock_r = socket::create(family, socket_type::stream);
                if (!sock_r) co_return std::unexpected(sock_r.error());
                conn.sock = std::move(*sock_r);
                if (auto r = conn.sock.set_non_blocking(true); !r)
                {
                    conn.sock.close();
                    co_return std::unexpected(r.error());
                }
                conn.write_token.reset();
                if (auto r = co_await async_connect(ctx_, conn.sock, ep, conn.write_token); !r)
                {
                    conn.sock.close();
                    co_return std::unexpected(r.error());
                }
                ++metrics_for(peer).reconnects;
            }

            std::vector<std::byte> frame;
            try
            {
                frame = encode_raft_message(message);
            }
            catch (...)
            {
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }

            conn.write_token.reset();
            auto r = co_await async_write_all(ctx_, conn.sock, const_buffer{frame.data(), frame.size()}, conn.write_token);
            if (!r)
                conn.sock.close();
            co_return r;
        }

        auto dispatch_message(raft_rpc_message& message) -> task<void>
        {
            if (!message.to.empty() && message.to != local_id_)
                co_return;
            if (message.from.empty() || !peers_.contains(message.from))
                co_return;
            if (!authenticate(message))
            {
                record_send_failure(message.from,
                                    std::make_error_code(std::errc::permission_denied),
                                    {});
                co_return;
            }
            metrics_for(message.from).last_receive_at = std::chrono::steady_clock::now();
            switch (message.type)
            {
            case raft_rpc_type::pre_vote:
                {
                    auto resp = node_->handle_request_vote(message.vote);
                    send_request_vote_response(message.from, resp);
                    break;
                }
            case raft_rpc_type::request_vote:
                {
                    auto resp = node_->handle_request_vote(message.vote);
                    send_request_vote_response(message.from, resp);
                    break;
                }
            case raft_rpc_type::request_vote_response:
                {
                    const bool quorum = node_->handle_vote_response(message.from, message.vote_response);
                    if (quorum && message.vote_response.pre_vote)
                        broadcast_request_vote(*node_);
                    else if (quorum)
                        replicate_to_all(*node_);
                    break;
                }
            case raft_rpc_type::timeout_now:
                {
                    auto vote = node_->handle_timeout_now(message.timeout_now);
                    if (vote)
                    {
                        for (const auto& peer : peers())
                            send_request_vote(peer, *vote);
                    }
                    break;
                }
            case raft_rpc_type::append_entries:
                {
                    auto resp = node_->handle_append_entries(message.append);
                    send_append_entries_response(message.from, resp);
                    break;
                }
            case raft_rpc_type::append_entries_response:
                {
                    const bool advanced =
                        node_->handle_append_entries_response(message.from, message.append_response);
                    if (auto transfer = node_->take_pending_leader_transfer(message.from))
                    {
                        send_timeout_now(message.from, *transfer);
                    }
                    else if (advanced)
                    {
                        replicate_to_all(*node_);
                    }
                    break;
                }
            case raft_rpc_type::install_snapshot:
                {
                    auto status = co_await materialize_snapshot(message);
                    install_snapshot_response resp;
                    if (status == snapshot_materialize_status::complete)
                    {
                        resp = node_->handle_install_snapshot(message.snapshot);
                        resp.snapshot_id = message.snapshot.snapshot_id;
                        resp.accepted_offset = message.snapshot.total_size;
                    }
                    else
                    {
                        resp = install_snapshot_response{
                            .term = node_->current_term(),
                            .success = status == snapshot_materialize_status::pending,
                            .snapshot_id = message.snapshot.snapshot_id,
                            .accepted_offset = accepted_snapshot_offset(message.from,
                                                                        message.snapshot),
                            .error = status == snapshot_materialize_status::failed
                                         ? "snapshot chunk rejected"
                                         : "",
                        };
                    }
                    send_install_snapshot_response(message.from, resp);
                    break;
                }
            case raft_rpc_type::install_snapshot_response:
                if (!message.snapshot_response.success &&
                    !message.snapshot_response.snapshot_id.empty() &&
                    message.snapshot_response.accepted_offset != 0)
                {
                    co_await resume_snapshot_send(message.from, message.snapshot_response);
                    break;
                }
                if (node_->handle_install_snapshot_response(message.from,
                                                            message.snapshot_response))
                {
                    snapshot_sends_.erase(snapshot_send_key(message.from,
                                                            message.snapshot_response.snapshot_id));
                    if (auto transfer = node_->take_pending_leader_transfer(message.from))
                        send_timeout_now(message.from, *transfer);
                    else
                        replicate_to_all(*node_);
                }
                break;
            }
            co_return;
        }

        auto materialize_snapshot(raft_rpc_message& message) -> task<snapshot_materialize_status>
        {
            if (!is_chunked_snapshot(message.snapshot))
                co_return snapshot_materialize_status::complete;

            if (message.snapshot.snapshot_id.empty())
                message.snapshot.snapshot_id = default_snapshot_id(message.snapshot);

            if (tcp_transport_detail::crc32_bytes(message.snapshot.data) !=
                message.snapshot.chunk_crc32)
            {
                co_return snapshot_materialize_status::failed;
            }

            std::error_code ec;
            std::filesystem::create_directories(options_.snapshot_directory, ec);
            if (ec)
                co_return snapshot_materialize_status::failed;

            const auto final_path = snapshot_path(message.from, message.snapshot);
            const auto temp_path = final_path.string() + ".part";

            const auto key = snapshot_session_key(message.from, message.snapshot);
            auto& state = snapshot_receives_[key];
            state.last_activity = std::chrono::steady_clock::now();
            if (message.snapshot.offset == 0 && state.expected_offset == 0)
            {
                state.temp_path = temp_path;
                state.final_path = final_path;
                auto reset = co_await async_file_open(ctx_, state.temp_path,
                                                      open_mode::write | open_mode::create | open_mode::truncate);
                if (!reset)
                {
                    snapshot_receives_.erase(key);
                    co_return snapshot_materialize_status::failed;
                }
                auto close = co_await async_file_close(ctx_, *reset);
                if (!close)
                {
                    snapshot_receives_.erase(key);
                    co_return snapshot_materialize_status::failed;
                }
            }
            else if (state.expected_offset == 0)
            {
                snapshot_receives_.erase(key);
                co_return snapshot_materialize_status::failed;
            }

            if (message.snapshot.offset < state.expected_offset)
            {
                if (message.snapshot.offset + message.snapshot.data.size() <= state.expected_offset)
                    co_return message.snapshot.done && state.expected_offset == message.snapshot.total_size
                                  ? snapshot_materialize_status::complete
                                  : snapshot_materialize_status::pending;
                co_return snapshot_materialize_status::failed;
            }

            if (message.snapshot.offset != state.expected_offset)
            {
                co_return snapshot_materialize_status::failed;
            }

            auto out = co_await async_file_open(ctx_, state.temp_path,
                                                open_mode::write | open_mode::create);
            if (!out)
                co_return snapshot_materialize_status::failed;
            auto written = co_await tcp_transport_detail::write_file_all(ctx_, *out,
                                                                         std::span<const std::byte>{
                                                                             message.snapshot.data
                                                                         }, state.expected_offset);
            if (!written)
            {
                (void)co_await async_file_close(ctx_, *out);
                co_return snapshot_materialize_status::failed;
            }

            state.expected_offset += message.snapshot.data.size();
            if (!message.snapshot.done)
            {
                auto close = co_await async_file_close(ctx_, *out);
                if (!close)
                    co_return snapshot_materialize_status::failed;
                co_return snapshot_materialize_status::pending;
            }

            auto flush = co_await async_file_flush(ctx_, *out);
            auto close = co_await async_file_close(ctx_, *out);
            if (!flush || !close)
                co_return snapshot_materialize_status::failed;

            if (state.expected_offset != message.snapshot.total_size)
                co_return snapshot_materialize_status::failed;
            auto crc = co_await tcp_transport_detail::crc32_file(ctx_, state.temp_path);
            if (!crc || *crc != message.snapshot.file_crc32)
                co_return snapshot_materialize_status::failed;

            std::filesystem::remove(state.final_path, ec);
            ec.clear();
            std::filesystem::rename(state.temp_path, state.final_path, ec);
            if (ec)
            {
                snapshot_receives_.erase(key);
                co_return snapshot_materialize_status::failed;
            }

            message.snapshot.uri = state.final_path.string();
            message.snapshot.data.clear();
            snapshot_receives_.erase(key);
            co_return snapshot_materialize_status::complete;
        }

        auto send_snapshot_file(node_id peer,
                                endpoint ep,
                                install_snapshot_request request,
                                std::uint64_t start_offset = 0) -> task<void>
        {
            auto stat = co_await async_file_stat(ctx_, request.uri);
            if (!stat)
            {
                notify_error(peer, stat.error());
                co_return;
            }

            auto file_crc = co_await tcp_transport_detail::crc32_file(ctx_, request.uri);
            if (!file_crc)
            {
                notify_error(peer, file_crc.error());
                co_return;
            }

            auto opened = co_await async_file_open(ctx_, request.uri, open_mode::read);
            if (!opened)
            {
                notify_error(peer, opened.error());
                co_return;
            }
            auto input = std::move(*opened);
            const auto total_size = stat->size;
            request.snapshot_id = request.snapshot_id.empty()
                                      ? default_snapshot_id(request)
                                      : request.snapshot_id;
            if (start_offset == 0)
            {
                snapshot_sends_[snapshot_send_key(peer, request.snapshot_id)] = snapshot_send_state{
                    .ep = ep,
                    .request = request,
                    .last_activity = std::chrono::steady_clock::now(),
                };
            }
            request.total_size = total_size;
            request.file_crc32 = *file_crc;
            request.uri.clear();

            auto chunk_size = request_chunk_size();
            std::vector<std::byte> buffer(chunk_size);
            std::uint64_t offset = std::min(start_offset, total_size);
            do
            {
                const auto want = std::min<std::uint64_t>(chunk_size, total_size - offset);
                std::size_t got = 0;
                while (got < want)
                {
                    auto r = co_await async_file_read(ctx_, input,
                                                      mutable_buffer{
                                                          buffer.data() + got,
                                                          static_cast<std::size_t>(want - got)
                                                      },
                                                      offset + got);
                    if (!r)
                    {
                        (void)co_await async_file_close(ctx_, input);
                        notify_error(peer, r.error());
                        co_return;
                    }
                    if (*r == 0)
                    {
                        (void)co_await async_file_close(ctx_, input);
                        notify_error(peer, std::make_error_code(std::errc::io_error));
                        co_return;
                    }
                    got += *r;
                }
                if (got != static_cast<std::size_t>(want))
                {
                    (void)co_await async_file_close(ctx_, input);
                    notify_error(peer, std::make_error_code(std::errc::io_error));
                    co_return;
                }

                auto chunk = request;
                chunk.offset = offset;
                chunk.done = offset + got >= total_size;
                chunk.data.assign(buffer.begin(),
                                  buffer.begin() + static_cast<std::ptrdiff_t>(got));
                chunk.chunk_crc32 = tcp_transport_detail::crc32_bytes(chunk.data);

                raft_rpc_message message{
                    .type = raft_rpc_type::install_snapshot,
                    .from = local_id_,
                    .to = peer,
                    .snapshot = std::move(chunk),
                };
                auto& conn = connections_[peer];
                conn.write_token.reset();
                auto sent = co_await send_with_retry(peer, ep, message, conn.write_token);
                if (auto it = snapshot_sends_.find(snapshot_send_key(peer, request.snapshot_id));
                    it != snapshot_sends_.end())
                {
                    it->second.last_activity = std::chrono::steady_clock::now();
                }
                if (!sent)
                {
                    (void)co_await async_file_close(ctx_, input);
                    notify_error(peer, sent.error());
                    co_return;
                }
                offset += got;
            }
            while (offset < total_size);

            auto close = co_await async_file_close(ctx_, input);
            if (!close)
                notify_error(peer, close.error());
        }

        auto send_snapshot_memory(node_id peer,
                                  endpoint ep,
                                  install_snapshot_request request) -> task<void>
        {
            request.snapshot_id = request.snapshot_id.empty()
                                      ? default_snapshot_id(request)
                                      : request.snapshot_id;
            if (request.offset == 0)
            {
                snapshot_sends_[snapshot_send_key(peer, request.snapshot_id)] = snapshot_send_state{
                    .ep = ep,
                    .request = request,
                    .last_activity = std::chrono::steady_clock::now(),
                };
            }
            request.total_size = request.data.size();
            request.file_crc32 = tcp_transport_detail::crc32_bytes(request.data);
            const auto payload = std::move(request.data);
            request.data.clear();

            const auto chunk_size = request_chunk_size();
            for (std::uint64_t offset = std::min<std::uint64_t>(request.offset, payload.size());
                 offset < payload.size();
                 offset += chunk_size)
            {
                const auto remaining = payload.size() - static_cast<std::size_t>(offset);
                const auto got = std::min<std::size_t>(chunk_size, remaining);
                auto chunk = request;
                chunk.offset = offset;
                chunk.done = offset + got >= payload.size();
                chunk.data.assign(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                                  payload.begin() + static_cast<std::ptrdiff_t>(offset + got));
                chunk.chunk_crc32 = tcp_transport_detail::crc32_bytes(chunk.data);

                raft_rpc_message message{
                    .type = raft_rpc_type::install_snapshot,
                    .from = local_id_,
                    .to = peer,
                    .snapshot = std::move(chunk),
                };
                auto& conn = connections_[peer];
                conn.write_token.reset();
                auto sent = co_await send_with_retry(peer, ep, message, conn.write_token);
                if (auto it = snapshot_sends_.find(snapshot_send_key(peer, request.snapshot_id));
                    it != snapshot_sends_.end())
                {
                    it->second.last_activity = std::chrono::steady_clock::now();
                }
                if (!sent)
                {
                    notify_error(peer, sent.error());
                    co_return;
                }
            }
        }

        auto resume_snapshot_send(const node_id& peer,
                                  const install_snapshot_response& response) -> task<void>
        {
            auto it = snapshot_sends_.find(snapshot_send_key(peer, response.snapshot_id));
            if (it == snapshot_sends_.end())
                co_return;
            auto request = it->second.request;
            auto ep = it->second.ep;
            if (!request.uri.empty())
            {
                co_await send_snapshot_file(peer, ep, request, response.accepted_offset);
            }
            else if (!request.data.empty())
            {
                request.offset = response.accepted_offset;
                co_await send_snapshot_memory(peer, ep, std::move(request));
            }
        }

        [[nodiscard]] auto request_chunk_size() const noexcept -> std::size_t
        {
            return std::max<std::size_t>(1, options_.snapshot_chunk_size);
        }

        [[nodiscard]] auto default_snapshot_id(const install_snapshot_request& snapshot) const
            -> std::string
        {
            return std::format("{}-{}-{}",
                               snapshot.leader_id.empty() ? local_id_ : snapshot.leader_id,
                               snapshot.metadata.last_included_index,
                               snapshot.metadata.last_included_term);
        }

        [[nodiscard]] auto snapshot_path(const node_id& from,
                                         const install_snapshot_request& snapshot) const
            -> std::filesystem::path
        {
            const auto name = std::format("{}-{}-{}-{}.snapshot",
                                          from,
                                          snapshot.metadata.last_included_index,
                                          snapshot.metadata.last_included_term,
                                          snapshot.snapshot_id);
            return options_.snapshot_directory / name;
        }

        [[nodiscard]] static auto snapshot_session_key(const node_id& from,
                                                       const install_snapshot_request& snapshot)
            -> std::string
        {
            return std::format("{}:{}", from, snapshot.snapshot_id);
        }

        [[nodiscard]] static auto snapshot_send_key(const node_id& peer,
                                                    std::string_view snapshot_id)
            -> std::string
        {
            return std::format("{}:{}", peer, snapshot_id);
        }

        [[nodiscard]] auto accepted_snapshot_offset(const node_id& from,
                                                    const install_snapshot_request& snapshot) const
            -> std::uint64_t
        {
            auto it = snapshot_receives_.find(snapshot_session_key(from, snapshot));
            if (it == snapshot_receives_.end())
                return 0;
            return it->second.expected_offset;
        }

        [[nodiscard]] static auto is_chunked_snapshot(const install_snapshot_request& snapshot)
            -> bool
        {
            return !snapshot.data.empty() ||
                !snapshot.snapshot_id.empty() ||
                snapshot.total_size != 0 ||
                snapshot.offset != 0 ||
                snapshot.chunk_crc32 != 0 ||
                snapshot.file_crc32 != 0 ||
                !snapshot.done;
        }

        static auto cleanup_snapshot_files_blocking(const std::filesystem::path& directory,
                                                    const raft_snapshot_retention_options& retention)
            -> std::expected<std::size_t, std::error_code>
        {
            std::error_code ec;
            if (!std::filesystem::exists(directory, ec))
                return 0u;
            if (ec)
                return std::unexpected(ec);

            std::vector<snapshot_file_candidate> files;
            for (std::filesystem::directory_iterator it{directory, ec}, end;
                 !ec && it != end;
                 it.increment(ec))
            {
                if (!it->is_regular_file(ec) || ec)
                    continue;
                if (it->path().extension() != ".snapshot")
                    continue;
                auto modified = it->last_write_time(ec);
                if (ec)
                    continue;
                files.push_back(snapshot_file_candidate{
                    .path = it->path(),
                    .modified = modified,
                });
            }
            if (ec)
                return std::unexpected(ec);

            std::ranges::sort(files, std::greater<>{}, &snapshot_file_candidate::modified);
            if (files.size() <= retention.keep_last)
                return 0u;

            const auto now = std::filesystem::file_time_type::clock::now();
            std::size_t removed = 0;
            for (std::size_t i = retention.keep_last; i < files.size(); ++i)
            {
                if (now - files[i].modified < retention.min_age)
                    continue;
                ec.clear();
                std::filesystem::remove(files[i].path, ec);
                if (ec)
                    return std::unexpected(ec);
                ++removed;
            }
            return removed;
        }

        [[nodiscard]] static auto fnv1a64(std::string_view data,
                                          std::uint64_t seed = 1469598103934665603ull)
            -> std::uint64_t
        {
            auto hash = seed;
            for (unsigned char ch : data)
            {
                hash ^= ch;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        [[nodiscard]] auto make_auth_token(const raft_rpc_message& message) const -> std::string
        {
            if (options_.security.shared_secret.empty())
                return {};
            auto hash = fnv1a64(options_.security.shared_secret);
            hash = fnv1a64(message.from, hash);
            hash = fnv1a64(message.to, hash);
            hash = fnv1a64(std::to_string(static_cast<int>(message.type)), hash);
            return std::format("{:016x}", hash);
        }

        [[nodiscard]] auto authenticate(const raft_rpc_message& message) const -> bool
        {
            if (options_.security.shared_secret.empty())
                return !options_.security.require_auth_token || message.auth_token.empty();
            return message.auth_token == make_auth_token(message);
        }

        auto metrics_for(const node_id& peer) -> raft_peer_transport_metrics&
        {
            auto& metrics = peer_metrics_[peer];
            if (metrics.peer.empty())
                metrics.peer = peer;
            return metrics;
        }

        void record_send_success(const node_id& peer,
                                 std::chrono::steady_clock::duration latency)
        {
            auto& metrics = metrics_for(peer);
            ++metrics.send_successes;
            metrics.last_error.clear();
            metrics.last_send_latency = latency;
            metrics.last_send_at = std::chrono::steady_clock::now();
        }

        void record_send_failure(const node_id& peer,
                                 std::error_code ec,
                                 std::chrono::steady_clock::duration latency)
        {
            auto& metrics = metrics_for(peer);
            ++metrics.send_failures;
            metrics.last_error = ec;
            metrics.last_send_latency = latency;
            metrics.last_send_at = std::chrono::steady_clock::now();
            notify_error(peer, ec);
        }

        io_context& ctx_;
        node_id local_id_;
        std::map<node_id, endpoint> peers_;
        std::map<node_id, peer_connection> connections_;
        std::set<socket*> inbound_sockets_;
        std::set<socket*> outbound_tls_sockets_;
        std::set<cancel_token*> inbound_tokens_;
        std::map<node_id, raft_peer_transport_metrics> peer_metrics_;
        std::map<std::string, snapshot_receive_state> snapshot_receives_;
        std::map<std::string, snapshot_send_state> snapshot_sends_;
        raft_tcp_transport_options options_;
        raft_node* node_ = nullptr;
        std::function<void(node_id, std::error_code)> on_error_;
        bool running_ = true;
    };
} // namespace cnetmod::raft
