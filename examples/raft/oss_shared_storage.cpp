/// cnetmod example - OSS-style shared object storage on Raft.
///
/// The object payload is intentionally small and kept in memory for the demo.
/// In a real deployment, the Raft command would usually carry object metadata
/// plus a content-addressed blob reference; blob files would live in async
/// storage while Raft protects metadata, ownership, version, and delete order.

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
    struct object_record
    {
        std::string bucket;
        std::string key;
        std::string content;
        std::string etag;
        std::uint64_t version = 0;
        std::chrono::system_clock::time_point updated_at;
    };

    [[nodiscard]] auto object_id(std::string_view bucket, std::string_view key) -> std::string
    {
        return std::format("{}/{}", bucket, key);
    }

    [[nodiscard]] auto make_etag(std::string_view content) -> std::string
    {
        auto hash = std::hash<std::string_view>{}(content);
        return std::format("{:016x}", hash);
    }

    class oss_state_machine final : public raft::state_machine
    {
    public:
        explicit oss_state_machine(std::string node_id)
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

            if (op == "PUT")
            {
                object_record object;
                input >> std::quoted(object.bucket) >> std::quoted(object.key) >>
                    std::quoted(object.content);
                object.version = ++last_version_;
                object.etag = make_etag(object.content);
                object.updated_at = std::chrono::system_clock::now();
                objects_[object_id(object.bucket, object.key)] = std::move(object);
            }
            else if (op == "DELETE")
            {
                std::string bucket;
                std::string key;
                input >> std::quoted(bucket) >> std::quoted(key);
                objects_.erase(object_id(bucket, key));
                ++last_version_;
            }
        }

        [[nodiscard]] auto get(std::string_view bucket, std::string_view key) const
            -> std::optional<object_record>
        {
            auto it = objects_.find(object_id(bucket, key));
            if (it == objects_.end())
                return std::nullopt;
            return it->second;
        }

        [[nodiscard]] auto object_count() const noexcept -> std::size_t
        {
            return objects_.size();
        }

        [[nodiscard]] auto node_id() const noexcept -> std::string_view
        {
            return node_id_;
        }

    private:
        std::string node_id_;
        std::map<std::string, object_record> objects_;
        std::uint64_t last_version_ = 0;
    };

    struct oss_cluster : raft_demo::three_node_tcp_service_cluster<oss_state_machine>
    {
        using three_node_tcp_service_cluster::three_node_tcp_service_cluster;

        auto put_object(std::string_view bucket,
                        std::string_view key,
                        std::string_view content)
            -> cnetmod::task<void>
        {
            std::ostringstream command;
            command << "PUT " << std::quoted(std::string{bucket}) << ' '
                    << std::quoted(std::string{key}) << ' '
                    << std::quoted(std::string{content});
            (void)co_await submit(command.str());
        }

        auto delete_object(std::string_view bucket, std::string_view key)
            -> cnetmod::task<void>
        {
            std::ostringstream command;
            command << "DELETE " << std::quoted(std::string{bucket}) << ' '
                    << std::quoted(std::string{key});
            (void)co_await submit(command.str());
        }

        [[nodiscard]] auto get_object(std::string_view bucket, std::string_view key) const
            -> std::optional<object_record>
        {
            return primary().get(bucket, key);
        }
    };

    struct sharded_oss_cluster
    {
        explicit sharded_oss_cluster(cnetmod::io_context& ctx)
            : shard_a{ctx, raft_demo::three_node_tcp_cluster_config{
                          .ports = {19201, 19202, 19203},
                      }},
              shard_b{ctx, raft_demo::three_node_tcp_cluster_config{
                          .ports = {19301, 19302, 19303},
                      }}
        {
        }

        oss_cluster shard_a;
        oss_cluster shard_b;

        auto start() -> cnetmod::task<void>
        {
            co_await shard_a.start();
            co_await shard_b.start();
        }

        auto put_object(std::string_view bucket,
                        std::string_view key,
                        std::string_view content)
            -> cnetmod::task<void>
        {
            co_await shard_for(bucket, key).put_object(bucket, key, content);
        }

        auto delete_object(std::string_view bucket, std::string_view key)
            -> cnetmod::task<void>
        {
            co_await shard_for(bucket, key).delete_object(bucket, key);
        }

        [[nodiscard]] auto get_object(std::string_view bucket, std::string_view key) const
            -> std::optional<object_record>
        {
            return shard_for(bucket, key).get_object(bucket, key);
        }

        [[nodiscard]] auto shard_name(std::string_view bucket, std::string_view key) const
            -> std::string_view
        {
            return shard_index(bucket, key) == 0 ? "shard-a" : "shard-b";
        }

    private:
        [[nodiscard]] auto shard_for(std::string_view bucket, std::string_view key) -> oss_cluster&
        {
            return shard_index(bucket, key) == 0 ? shard_a : shard_b;
        }

        [[nodiscard]] auto shard_for(std::string_view bucket, std::string_view key) const
            -> const oss_cluster&
        {
            return shard_index(bucket, key) == 0 ? shard_a : shard_b;
        }

        [[nodiscard]] auto shard_index(std::string_view bucket, std::string_view key) const
            -> std::size_t
        {
            return std::hash<std::string>{}(object_id(bucket, key)) % 2;
        }
    };

    void print_object(const oss_state_machine& machine, std::string_view bucket, std::string_view key)
    {
        auto object = machine.get(bucket, key);
        if (!object)
        {
            std::println("  {} GET {:<20} -> 404", machine.node_id(), object_id(bucket, key));
            return;
        }

        std::println("  {} GET {:<20} -> etag={} version={} bytes={}",
                     machine.node_id(),
                     object_id(bucket, key),
                     object->etag,
                     object->version,
                     object->content.size());
    }

    void print_ports(std::string_view name, const oss_cluster& cluster)
    {
        const auto ports = cluster.listen_ports();
        std::println("{} tcp peers: n1=127.0.0.1:{} n2=127.0.0.1:{} n3=127.0.0.1:{}",
                     name,
                     ports[0],
                     ports[1],
                     ports[2]);
    }

    auto run_primary_backup(oss_cluster& cluster) -> cnetmod::task<void>
    {
        std::println("\n=== primary-backup replicated bucket ===");

        co_await cluster.start();
        std::println("leader: {} term={}", cluster.leader_id(), cluster.term());
        print_ports("replica-set", cluster);

        co_await cluster.put_object("media", "avatar.png", "png-bytes-v1");
        co_await cluster.put_object("media", "avatar.png", "png-bytes-v2");
        co_await cluster.put_object("docs", "readme.txt", "hello-object-storage");
        co_await cluster.delete_object("docs", "readme.txt");

        std::println("\nreplicated object metadata:");
        for (const auto* sm : cluster.replicas())
        {
            print_object(*sm, "media", "avatar.png");
            print_object(*sm, "docs", "readme.txt");
        }

        const auto commits = cluster.commit_indexes();
        auto replicas = cluster.replicas();
        std::println("commit index: n1={} n2={} n3={}",
                     commits[0],
                     commits[1],
                     commits[2]);
        std::println("objects: n1={} n2={} n3={}",
                     replicas[0]->object_count(),
                     replicas[1]->object_count(),
                     replicas[2]->object_count());
        cluster.stop();
    }

    auto run_sharded_storage(sharded_oss_cluster& cluster) -> cnetmod::task<void>
    {
        std::println("\n=== sharded object namespace ===");

        co_await cluster.start();
        print_ports("shard-a", cluster.shard_a);
        print_ports("shard-b", cluster.shard_b);

        co_await cluster.put_object("images", "a.png", "image-a");
        co_await cluster.put_object("images", "b.png", "image-b");
        co_await cluster.put_object("logs", "2026-07-04.txt", "log-line-1");
        co_await cluster.delete_object("images", "b.png");

        for (const auto& [bucket, key] : std::array{
                 std::pair{std::string_view{"images"}, std::string_view{"a.png"}},
                 std::pair{std::string_view{"images"}, std::string_view{"b.png"}},
                 std::pair{std::string_view{"logs"}, std::string_view{"2026-07-04.txt"}},
             })
        {
            auto object = cluster.get_object(bucket, key);
            std::println("  route {:<18} -> {:<7} status={}",
                         object_id(bucket, key),
                         cluster.shard_name(bucket, key),
                         object ? "stored" : "deleted");
        }

        const auto a_commits = cluster.shard_a.commit_indexes();
        const auto b_commits = cluster.shard_b.commit_indexes();
        std::println("shard-a commit index: n1={} n2={} n3={}",
                     a_commits[0],
                     a_commits[1],
                     a_commits[2]);
        std::println("shard-b commit index: n1={} n2={} n3={}",
                     b_commits[0],
                     b_commits[1],
                     b_commits[2]);
        cluster.shard_a.stop();
        cluster.shard_b.stop();
    }

    auto run(cnetmod::io_context& ctx,
             oss_cluster& primary,
             sharded_oss_cluster& sharded) -> cnetmod::task<void>
    {
        try
        {
            std::println("=== cnetmod Raft distributed object storage ===");
            co_await run_primary_backup(primary);
            co_await run_sharded_storage(sharded);
        }
        catch (const std::exception& e)
        {
            std::println(std::cerr, "raft oss example failed: {}", e.what());
        }
        ctx.stop();
    }
}

int main()
{
    cnetmod::net_init net;
    auto ctx = cnetmod::make_io_context();
    oss_cluster primary{*ctx, raft_demo::three_node_tcp_cluster_config{
                            .ports = {19101, 19102, 19103},
                        }};
    sharded_oss_cluster sharded{*ctx};
    cnetmod::spawn(*ctx, run(*ctx, primary, sharded));
    ctx->run();
    return 0;
}
