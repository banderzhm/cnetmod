#pragma once

namespace raft_demo
{
    struct three_node_tcp_cluster_config
    {
        std::array<std::uint16_t, 3> ports{19101, 19102, 19103};
        cnetmod::raft::raft_options raft{
            .election_timeout = std::chrono::milliseconds{50},
            .heartbeat_interval = std::chrono::milliseconds{20},
            .leader_lease_timeout = std::chrono::seconds{5},
            .pre_vote = false,
            .check_quorum = false,
            .max_entries_per_append = 128,
            .max_inflight_append = 256,
        };
        cnetmod::raft::raft_tcp_transport_options transport{
            .retry_backoff = std::chrono::milliseconds{1},
        };
    };

    [[nodiscard]] inline auto loopback_endpoint(std::uint16_t port) -> cnetmod::endpoint
    {
        return cnetmod::endpoint{
            cnetmod::ip_address{cnetmod::ipv4_address::loopback()},
            port,
        };
    }

    template <typename StateMachine>
    class three_node_tcp_service_cluster
    {
    public:
        three_node_tcp_service_cluster(cnetmod::io_context& ctx,
                                       three_node_tcp_cluster_config config = {})
            : ctx_(ctx),
              config_(std::move(config)),
              n1_{
                  cnetmod::raft::raft_config{
                      .id = "n1",
                      .peers = {"n2", "n3"},
                      .options = config_.raft,
                  },
                  s1_,
                  &sm1_
              },
              n2_{
                  cnetmod::raft::raft_config{
                      .id = "n2",
                      .peers = {"n1", "n3"},
                      .options = config_.raft,
                  },
                  s2_,
                  &sm2_
              },
              n3_{
                  cnetmod::raft::raft_config{
                      .id = "n3",
                      .peers = {"n1", "n2"},
                      .options = config_.raft,
                  },
                  s3_,
                  &sm3_
              },
              t1_{ctx_, "n1", config_.transport},
              t2_{ctx_, "n2", config_.transport},
              t3_{ctx_, "n3", config_.transport},
              r1_{ctx_, n1_, t1_, endpoint(0), config_.raft, runtime_options_},
              r2_{ctx_, n2_, t2_, endpoint(1), config_.raft, runtime_options_},
              r3_{ctx_, n3_, t3_, endpoint(2), config_.raft, runtime_options_}
        {
            t1_.add_peer(cnetmod::raft::raft_tcp_peer{.id = "n2", .address = endpoint(1)});
            t1_.add_peer(cnetmod::raft::raft_tcp_peer{.id = "n3", .address = endpoint(2)});
            t2_.add_peer(cnetmod::raft::raft_tcp_peer{.id = "n1", .address = endpoint(0)});
            t2_.add_peer(cnetmod::raft::raft_tcp_peer{.id = "n3", .address = endpoint(2)});
            t3_.add_peer(cnetmod::raft::raft_tcp_peer{.id = "n1", .address = endpoint(0)});
            t3_.add_peer(cnetmod::raft::raft_tcp_peer{.id = "n2", .address = endpoint(1)});
        }

        three_node_tcp_service_cluster(const three_node_tcp_service_cluster&) = delete;
        auto operator=(const three_node_tcp_service_cluster&)
            -> three_node_tcp_service_cluster& = delete;

        ~three_node_tcp_service_cluster()
        {
            stop();
        }

        auto start() -> cnetmod::task<void>
        {
            if (running_) co_return;
            running_ = true;
            r1_.start();
            r2_.start();
            r3_.start();

            (void)co_await cnetmod::async_timer_wait(ctx_, std::chrono::milliseconds{80});
            t1_.broadcast_request_vote(n1_);
            for (auto i = 0; i < 100 && n1_.role() != cnetmod::raft::node_role::leader; ++i)
            {
                tick_all();
                (void)co_await cnetmod::async_timer_wait(ctx_, std::chrono::milliseconds{10});
            }
            if (n1_.role() != cnetmod::raft::node_role::leader)
                throw std::runtime_error("raft tcp cluster failed to elect n1");
        }

        void stop() noexcept
        {
            if (!running_) return;
            running_ = false;
            r1_.stop();
            r2_.stop();
            r3_.stop();
        }

        auto submit(std::string command) -> cnetmod::task<cnetmod::raft::log_index>
        {
            if (!running_)
                throw std::runtime_error("raft tcp cluster is not running");

            auto entry = n1_.append_command(std::move(command));
            if (!entry)
                throw std::runtime_error(entry.error().message);

            for (auto i = 0; i < 200; ++i)
            {
                tick_all();
                if (n1_.commit_index() >= entry->index &&
                    n2_.commit_index() >= entry->index &&
                    n3_.commit_index() >= entry->index)
                {
                    co_return entry->index;
                }
                (void)co_await cnetmod::async_timer_wait(ctx_, std::chrono::milliseconds{10});
            }
            throw std::runtime_error("raft tcp command replication timed out");
        }

        [[nodiscard]] auto leader_id() const noexcept -> std::string_view
        {
            return n1_.id();
        }

        [[nodiscard]] auto term() const noexcept -> cnetmod::raft::term_t
        {
            return n1_.current_term();
        }

        [[nodiscard]] auto endpoint(std::size_t index) const -> cnetmod::endpoint
        {
            return loopback_endpoint(config_.ports.at(index));
        }

        [[nodiscard]] auto listen_ports() const noexcept -> const std::array<std::uint16_t, 3>&
        {
            return config_.ports;
        }

        [[nodiscard]] auto commit_indexes() const
            -> std::array<cnetmod::raft::log_index, 3>
        {
            return {n1_.commit_index(), n2_.commit_index(), n3_.commit_index()};
        }

        [[nodiscard]] auto primary() noexcept -> StateMachine&
        {
            return sm1_;
        }

        [[nodiscard]] auto primary() const noexcept -> const StateMachine&
        {
            return sm1_;
        }

        [[nodiscard]] auto replicas() noexcept -> std::array<StateMachine*, 3>
        {
            return {&sm1_, &sm2_, &sm3_};
        }

        [[nodiscard]] auto replicas() const noexcept -> std::array<const StateMachine*, 3>
        {
            return {&sm1_, &sm2_, &sm3_};
        }

    private:
        void tick_all()
        {
            r1_.tick_now();
            r2_.tick_now();
            r3_.tick_now();
        }

        cnetmod::io_context& ctx_;
        three_node_tcp_cluster_config config_;

        std::shared_ptr<cnetmod::raft::memory_store> s1_ =
            std::make_shared<cnetmod::raft::memory_store>();
        std::shared_ptr<cnetmod::raft::memory_store> s2_ =
            std::make_shared<cnetmod::raft::memory_store>();
        std::shared_ptr<cnetmod::raft::memory_store> s3_ =
            std::make_shared<cnetmod::raft::memory_store>();

        StateMachine sm1_{"n1"};
        StateMachine sm2_{"n2"};
        StateMachine sm3_{"n3"};

        cnetmod::raft::raft_node n1_;
        cnetmod::raft::raft_node n2_;
        cnetmod::raft::raft_node n3_;

        cnetmod::raft::raft_tcp_transport t1_;
        cnetmod::raft::raft_tcp_transport t2_;
        cnetmod::raft::raft_tcp_transport t3_;

        cnetmod::raft::raft_runtime_options runtime_options_{
            .auto_election = false,
            .auto_heartbeat = false,
        };

        cnetmod::raft::raft_node_runtime r1_;
        cnetmod::raft::raft_node_runtime r2_;
        cnetmod::raft::raft_node_runtime r3_;
        bool running_ = false;
    };
}
