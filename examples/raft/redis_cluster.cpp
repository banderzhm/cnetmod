/// cnetmod example - Redis-style replicated KV on Raft.
///
/// This is not a Redis wire-protocol server. It demonstrates the production
/// shape behind a distributed Redis-like database: client commands become Raft
/// log entries, and every replica applies the same command stream.

import std;
import cnetmod.core.address;
import cnetmod.core.net_init;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import cnetmod.protocol.raft;

#include "raft_demo_cluster.hpp"

namespace raft = cnetmod::raft;

namespace
{
    struct kv_value
    {
        std::string value;
        std::optional<std::chrono::steady_clock::time_point> expires_at;
    };

    class redis_state_machine final : public raft::state_machine
    {
    public:
        explicit redis_state_machine(std::string node_id)
            : node_id_(std::move(node_id))
        {
        }

        void on_apply(const raft::log_entry& entry) override
        {
            if (entry.type != raft::entry_type::command)
                return;

            std::istringstream input(entry.command);
            std::string op;
            input >> op;

            if (op == "SET")
            {
                std::string key;
                std::string value;
                std::int64_t ttl_ms = 0;
                input >> std::quoted(key) >> std::quoted(value) >> ttl_ms;

                kv_value next{.value = std::move(value)};
                if (ttl_ms > 0)
                    next.expires_at = std::chrono::steady_clock::now() + std::chrono::milliseconds{ttl_ms};
                data_[std::move(key)] = std::move(next);
            }
            else if (op == "DEL")
            {
                std::string key;
                input >> std::quoted(key);
                data_.erase(key);
            }
            else if (op == "INCR")
            {
                std::string key;
                input >> std::quoted(key);
                auto current = get(key).value_or("0");
                auto next = std::stoll(current) + 1;
                data_[std::move(key)] = kv_value{.value = std::to_string(next)};
            }
        }

        [[nodiscard]] auto get(const std::string& key) -> std::optional<std::string>
        {
            auto it = data_.find(key);
            if (it == data_.end())
                return std::nullopt;
            if (it->second.expires_at && std::chrono::steady_clock::now() >= *it->second.expires_at)
            {
                data_.erase(it);
                return std::nullopt;
            }
            return it->second.value;
        }

        [[nodiscard]] auto size() const noexcept -> std::size_t
        {
            return data_.size();
        }

        [[nodiscard]] auto node_id() const noexcept -> std::string_view
        {
            return node_id_;
        }

    private:
        std::string node_id_;
        std::map<std::string, kv_value> data_;
    };

    struct redis_cluster : raft_demo::three_node_tcp_service_cluster<redis_state_machine>
    {
        using three_node_tcp_service_cluster::three_node_tcp_service_cluster;

        auto set(std::string_view key,
                 std::string_view value,
                 std::chrono::milliseconds ttl = std::chrono::milliseconds{0})
            -> cnetmod::task<void>
        {
            std::ostringstream command;
            command << "SET " << std::quoted(std::string{key}) << ' '
                    << std::quoted(std::string{value}) << ' '
                    << ttl.count();
            (void)co_await submit(command.str());
        }

        auto del(std::string_view key) -> cnetmod::task<void>
        {
            std::ostringstream command;
            command << "DEL " << std::quoted(std::string{key});
            (void)co_await submit(command.str());
        }

        auto incr(std::string_view key) -> cnetmod::task<void>
        {
            std::ostringstream command;
            command << "INCR " << std::quoted(std::string{key});
            (void)co_await submit(command.str());
        }

        [[nodiscard]] auto get(std::string_view key) -> std::optional<std::string>
        {
            return primary().get(std::string{key});
        }
    };

    void print_value(redis_state_machine& machine, std::string_view key)
    {
        auto value = machine.get(std::string{key});
        std::println("  {} GET {:<12} -> {}", machine.node_id(), key, value.value_or("(nil)"));
    }

    auto run(cnetmod::io_context& ctx, redis_cluster& cluster) -> cnetmod::task<void>
    {
        try
        {
            std::println("=== cnetmod Raft Redis-style KV ===");

            co_await cluster.start();
            std::println("leader: {} term={}", cluster.leader_id(), cluster.term());
            const auto ports = cluster.listen_ports();
            std::println("tcp peers: n1=127.0.0.1:{} n2=127.0.0.1:{} n3=127.0.0.1:{}",
                         ports[0],
                         ports[1],
                         ports[2]);

            co_await cluster.set("user:1", "alice");
            co_await cluster.set("counter", "0");
            co_await cluster.incr("counter");
            co_await cluster.incr("counter");
            co_await cluster.set("session:token", "live", std::chrono::seconds{1});
            co_await cluster.del("user:1");

            std::println("\nreplicated reads:");
            for (auto* sm : cluster.replicas())
            {
                print_value(*sm, "user:1");
                print_value(*sm, "counter");
                print_value(*sm, "session:token");
            }

            const auto commits = cluster.commit_indexes();
            std::println("\ncommit index: n1={} n2={} n3={}",
                         commits[0],
                         commits[1],
                         commits[2]);
            auto replicas = cluster.replicas();
            std::println("keys: n1={} n2={} n3={}",
                         replicas[0]->size(),
                         replicas[1]->size(),
                         replicas[2]->size());
            cluster.stop();
        }
        catch (const std::exception& e)
        {
            std::println(std::cerr, "raft redis example failed: {}", e.what());
        }
        ctx.stop();
    }
}

int main()
{
    cnetmod::net_init net;
    auto ctx = cnetmod::make_io_context();
    redis_cluster cluster{*ctx};
    cnetmod::spawn(*ctx, run(*ctx, cluster));
    ctx->run();
    return 0;
}
