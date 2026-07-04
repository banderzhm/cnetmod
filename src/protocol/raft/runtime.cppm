export module cnetmod.protocol.raft:runtime;

import std;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import :types;
import :node;
import :tcp_transport;

namespace cnetmod::raft
{
    export struct raft_runtime_options
    {
        bool start_tcp_server = true;
        bool auto_election = true;
        bool auto_heartbeat = true;
        bool auto_snapshot = false;
        raft_snapshot_policy snapshot_policy;
    };

    export class raft_node_runtime
    {
    public:
        raft_node_runtime(io_context& ctx,
                          raft_node& node,
                          raft_tcp_transport& transport,
                          endpoint listen_endpoint,
                          raft_options options,
                          raft_runtime_options runtime_options = {})
            : ctx_(ctx),
              node_(node),
              transport_(transport),
              listen_endpoint_(std::move(listen_endpoint)),
              options_(options),
              runtime_options_(runtime_options)
        {
            transport_.set_node(node_);
        }

        raft_node_runtime(const raft_node_runtime&) = delete;
        auto operator=(const raft_node_runtime&) -> raft_node_runtime& = delete;

        void start()
        {
            bool expected = false;
            if (!running_.compare_exchange_strong(expected, true))
                return;

            if (runtime_options_.start_tcp_server)
                spawn(ctx_, serve_loop());
            if (runtime_options_.auto_election)
                spawn(ctx_, election_loop());
            if (runtime_options_.auto_heartbeat)
                spawn(ctx_, heartbeat_loop());
        }

        void stop() noexcept
        {
            if (!running_.exchange(false))
                return;
            transport_.stop();
            accept_token_.cancel();
            election_token_.cancel();
            heartbeat_token_.cancel();
        }

        [[nodiscard]] auto running() const noexcept -> bool
        {
            return running_.load(std::memory_order_acquire);
        }

        void tick_now()
        {
            if (node_.role() == node_role::leader)
            {
                transport_.replicate_to_all(node_);
            }
            else if (node_.election_timeout_elapsed(options_.election_timeout))
            {
                if (options_.pre_vote)
                    transport_.broadcast_pre_vote(node_);
                else
                    transport_.broadcast_request_vote(node_);
            }
        }

        auto transfer_leader(const node_id& target) -> std::expected<void, raft_error>
        {
            return transport_.transfer_leader(node_, target);
        }

        auto maybe_snapshot_now()
            -> std::expected<std::optional<snapshot_metadata>, raft_error>
        {
            return node_.maybe_create_snapshot(runtime_options_.snapshot_policy);
        }

        auto async_read_index(read_index_request request)
            -> task<std::expected<read_index_response, raft_error>>
        {
            auto submitted = node_.read_index(std::move(request));
            if (!submitted)
                co_return std::unexpected(submitted.error());
            if (submitted->ready)
            {
                auto consumed = node_.consume_read_index(submitted->id);
                co_return consumed ? *consumed : *submitted;
            }

            transport_.replicate_to_all(node_);
            const auto deadline = std::chrono::steady_clock::now() + options_.read_index_timeout;
            while (running() || !runtime_options_.auto_heartbeat)
            {
                if (auto ready = node_.consume_read_index(submitted->id))
                    co_return *ready;
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    (void)node_.expire_read_indexes(std::chrono::milliseconds{0});
                    co_return std::unexpected(raft_error{
                        .code = raft_errc::stopped,
                        .message = "read-index request timed out",
                    });
                }
                auto delay = std::min(options_.heartbeat_interval,
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          deadline - std::chrono::steady_clock::now()));
                (void)co_await async_timer_wait(ctx_, delay);
            }

            co_return std::unexpected(raft_error{
                .code = raft_errc::stopped,
                .message = "raft runtime stopped",
            });
        }

    private:
        auto serve_loop() -> task<void>
        {
            auto r = co_await transport_.serve(listen_endpoint_, accept_token_);
            if (!r)
                stop();
        }

        auto election_loop() -> task<void>
        {
            while (running())
            {
                election_token_.reset();
                auto delay = randomized_election_timeout();
                auto r = co_await async_timer_wait(ctx_, delay, election_token_);
                if (!r || !running())
                    continue;
                if (!node_.election_timeout_elapsed(delay))
                    continue;

                if (options_.pre_vote)
                {
                    transport_.broadcast_pre_vote(node_);
                }
                else
                {
                    transport_.broadcast_request_vote(node_);
                }
            }
        }

        auto heartbeat_loop() -> task<void>
        {
            while (running())
            {
                heartbeat_token_.reset();
                auto r = co_await async_timer_wait(ctx_, options_.heartbeat_interval, heartbeat_token_);
                if (!r || !running())
                    continue;
                if (node_.role() == node_role::leader)
                {
                    (void)node_.expire_read_indexes(options_.read_index_timeout);
                    if (options_.check_quorum &&
                        !node_.check_leader_quorum(options_.leader_lease_timeout))
                    {
                        continue;
                    }
                    if (runtime_options_.auto_snapshot)
                    {
                        auto snapshotted = node_.maybe_create_snapshot(runtime_options_.snapshot_policy);
                        if (!snapshotted)
                        {
                            node_.stop(snapshotted.error());
                            continue;
                        }
                }
                transport_.cleanup_snapshot_sessions();
                if (auto cleaned = co_await transport_.cleanup_snapshot_files(); !cleaned) {
                    node_.stop(raft_error{
                        .code = raft_errc::storage_error,
                        .message = cleaned.error().message(),
                    });
                    continue;
                }
                transport_.replicate_to_all(node_);
            }
            }
        }

        auto randomized_election_timeout() -> std::chrono::milliseconds
        {
            const auto base = std::max<std::int64_t>(1, options_.election_timeout.count());
            std::uniform_int_distribution<std::int64_t> dist{0, base};
            return std::chrono::milliseconds{base + dist(rng_)};
        }

        io_context& ctx_;
        raft_node& node_;
        raft_tcp_transport& transport_;
        endpoint listen_endpoint_;
        raft_options options_;
        raft_runtime_options runtime_options_;
        std::atomic<bool> running_{false};
        cancel_token accept_token_;
        cancel_token election_token_;
        cancel_token heartbeat_token_;
        std::mt19937_64 rng_{std::random_device{}()};
    };
} // namespace cnetmod::raft
