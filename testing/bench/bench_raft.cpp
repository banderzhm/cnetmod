/// cnetmod benchmark - Raft throughput report

#include <iostream>

import std;
import cnetmod.protocol.raft;

using namespace cnetmod::raft;

namespace
{
    using clock_type = std::chrono::steady_clock;

    struct bench_row
    {
        std::string name;
        std::size_t operations = 0;
        double elapsed_ms = 0.0;
        double ops_per_sec = 0.0;
        double ns_per_op = 0.0;
        log_index commit_index = 0;
    };

    auto make_five_node_cluster(raft_options options)
    {
        struct cluster
        {
            std::shared_ptr<memory_store> s1 = std::make_shared<memory_store>();
            std::shared_ptr<memory_store> s2 = std::make_shared<memory_store>();
            std::shared_ptr<memory_store> s3 = std::make_shared<memory_store>();
            std::shared_ptr<memory_store> s4 = std::make_shared<memory_store>();
            std::shared_ptr<memory_store> s5 = std::make_shared<memory_store>();
            raft_node n1;
            raft_node n2;
            raft_node n3;
            raft_node n4;
            raft_node n5;

            explicit cluster(raft_options options)
                : n1{raft_config{.id = "n1", .peers = {"n2", "n3", "n4", "n5"}, .options = options}, s1},
                  n2{raft_config{.id = "n2", .peers = {"n1", "n3", "n4", "n5"}, .options = options}, s2},
                  n3{raft_config{.id = "n3", .peers = {"n1", "n2", "n4", "n5"}, .options = options}, s3},
                  n4{raft_config{.id = "n4", .peers = {"n1", "n2", "n3", "n5"}, .options = options}, s4},
                  n5{raft_config{.id = "n5", .peers = {"n1", "n2", "n3", "n4"}, .options = options}, s5}
            {
            }
        };

        return cluster{options};
    }

    void elect_leader(raft_node& leader)
    {
        leader.begin_election();
        (void)leader.handle_vote_response("n2", {.term = leader.current_term(), .vote_granted = true});
        (void)leader.handle_vote_response("n3", {.term = leader.current_term(), .vote_granted = true});
    }

    void replicate_to(raft_node& leader, raft_node& follower, const node_id& peer)
    {
        auto req = leader.make_append_entries(peer);
        const auto last_sent = req.entries.empty() ? req.prev_log_index : req.entries.back().index;
        leader.mark_append_sent(peer, last_sent);
        auto resp = follower.handle_append_entries(req);
        (void)leader.handle_append_entries_response(peer, resp);
    }

    auto finalize_row(std::string name,
                      std::size_t operations,
                      clock_type::time_point started,
                      clock_type::time_point ended,
                      log_index commit_index) -> bench_row
    {
        const auto elapsed_ns = std::chrono::duration<double, std::nano>(ended - started).count();
        const auto elapsed_ms = elapsed_ns / 1'000'000.0;
        const auto ns_per_op = elapsed_ns / static_cast<double>(std::max<std::size_t>(1, operations));
        return bench_row{
            .name = std::move(name),
            .operations = operations,
            .elapsed_ms = elapsed_ms,
            .ops_per_sec = 1'000'000'000.0 / ns_per_op,
            .ns_per_op = ns_per_op,
            .commit_index = commit_index,
        };
    }

    auto bench_single_node_append(std::size_t operations) -> bench_row
    {
        auto store = std::make_shared<memory_store>();
        raft_node node{raft_config{.id = "n1"}, store};
        node.begin_election();

        auto started = clock_type::now();
        for (std::size_t i = 0; i < operations; ++i)
            (void)node.append_command(std::format("cmd-{}", i));
        auto ended = clock_type::now();

        return finalize_row("single-node append+commit", operations, started, ended, node.commit_index());
    }

    auto bench_five_node_majority_replication(std::size_t operations) -> bench_row
    {
        auto cluster = make_five_node_cluster(raft_options{.max_entries_per_append = 256});
        elect_leader(cluster.n1);

        auto started = clock_type::now();
        for (std::size_t i = 0; i < operations; ++i)
        {
            (void)cluster.n1.append_command(std::format("cmd-{}", i));
            replicate_to(cluster.n1, cluster.n2, "n2");
            replicate_to(cluster.n1, cluster.n3, "n3");
        }
        auto ended = clock_type::now();

        return finalize_row("5-node majority replication", operations, started, ended, cluster.n1.commit_index());
    }

    auto bench_five_node_lagging_recovery(std::size_t operations) -> bench_row
    {
        auto cluster = make_five_node_cluster(raft_options{.max_entries_per_append = 512});
        elect_leader(cluster.n1);

        auto started = clock_type::now();
        for (std::size_t i = 0; i < operations; ++i)
        {
            (void)cluster.n1.append_command(std::format("cmd-{}", i));
            replicate_to(cluster.n1, cluster.n2, "n2");
            replicate_to(cluster.n1, cluster.n3, "n3");
        }
        while (cluster.n4.commit_index() < cluster.n1.commit_index() ||
               cluster.n5.commit_index() < cluster.n1.commit_index())
        {
            replicate_to(cluster.n1, cluster.n4, "n4");
            replicate_to(cluster.n1, cluster.n5, "n5");
        }
        auto ended = clock_type::now();

        return finalize_row("lagging follower catch-up", operations, started, ended, cluster.n1.commit_index());
    }

    void print_row(const bench_row& row)
    {
        std::cout << std::format("{:<32} {:>10} {:>12.2f} {:>14.0f} {:>12.1f} {:>10}\n",
                                 row.name,
                                 row.operations,
                                 row.elapsed_ms,
                                 row.ops_per_sec,
                                 row.ns_per_op,
                                 row.commit_index);
        std::cout.flush();
    }
}

int main()
{
#ifdef NDEBUG
    const std::size_t operations = 50'000;
#else
    const std::size_t operations = 5'000;
#endif

    std::cout << "================================================================\n";
    std::cout << "  cnetmod Raft Throughput Report\n";
    std::cout << "================================================================\n";
#ifdef NDEBUG
    std::cout << "  Build     : Release\n";
#else
    std::cout << "  Build     : Debug (use Release for production numbers)\n";
#endif
    std::cout << "  Operations: " << operations << "\n";
    std::cout << "  Notes     : in-process deterministic Raft core benchmark\n";
    std::cout << "================================================================\n";
    std::cout << std::format("{:<32} {:>10} {:>12} {:>14} {:>12} {:>10}\n",
                             "Benchmark", "ops", "ms", "ops/sec", "ns/op", "commit");
    std::cout << std::string(96, '-') << "\n";

    print_row(bench_single_node_append(operations));
    print_row(bench_five_node_majority_replication(operations));
    print_row(bench_five_node_lagging_recovery(operations));

    std::cout << "================================================================\n";
    return 0;
}
