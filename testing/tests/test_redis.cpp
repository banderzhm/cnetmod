#include "test_framework.hpp"

import std;
import cnetmod.protocol.redis;

using namespace cnetmod::redis;

TEST(redis_cluster_key_slot_uses_hash_tag) {
    auto a = client::key_slot("user:{42}:name");
    auto b = client::key_slot("cart:{42}:items");
    auto c = client::key_slot("user:42:name");

    ASSERT_EQ(a, b);
    ASSERT_NE(a, c);
    ASSERT_EQ(client::key_slot("123456789"), 12739);
}

TEST(redis_parse_moved_redirect) {
    std::vector<resp3_node> nodes{
        resp3_node{
            .data_type = resp3_type::simple_error,
            .value = "MOVED 3999 127.0.0.1:7001",
        },
    };

    auto r = client::parse_redirect(nodes);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(static_cast<int>(r->kind), static_cast<int>(redirect_kind::moved));
    ASSERT_EQ(r->slot, 3999);
    ASSERT_EQ(r->endpoint.host, std::string("127.0.0.1"));
    ASSERT_EQ(r->endpoint.port, 7001);
}

TEST(redis_parse_ask_redirect) {
    std::vector<resp3_node> nodes{
        resp3_node{
            .data_type = resp3_type::simple_error,
            .value = "ASK 1234 redis-node.local:6380",
        },
    };

    auto r = client::parse_redirect(nodes);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(static_cast<int>(r->kind), static_cast<int>(redirect_kind::ask));
    ASSERT_EQ(r->slot, 1234);
    ASSERT_EQ(r->endpoint.host, std::string("redis-node.local"));
    ASSERT_EQ(r->endpoint.port, 6380);
}

TEST(redis_parse_moved_redirect_ipv6_endpoint) {
    std::vector<resp3_node> nodes{
        resp3_node{
            .data_type = resp3_type::simple_error,
            .value = "MOVED 42 [2001:db8::10]:7002",
        },
    };

    auto r = client::parse_redirect(nodes);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(static_cast<int>(r->kind), static_cast<int>(redirect_kind::moved));
    ASSERT_EQ(r->slot, 42);
    ASSERT_EQ(r->endpoint.host, std::string("2001:db8::10"));
    ASSERT_EQ(r->endpoint.port, 7002);
}

TEST(redis_resp3_push_parse) {
    auto parsed = parse_response(">3\r\n$7\r\nmessage\r\n$6\r\nevents\r\n$5\r\nhello\r\n");
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->size(), std::size_t{4});
    ASSERT_EQ(static_cast<int>(parsed->at(0).data_type), static_cast<int>(resp3_type::push));
    ASSERT_EQ(parsed->at(0).aggregate_size, std::size_t{3});
    ASSERT_EQ(parsed->at(1).value, std::string("message"));
    ASSERT_EQ(parsed->at(2).value, std::string("events"));
    ASSERT_EQ(parsed->at(3).value, std::string("hello"));
}

TEST(redis_cluster_slots_parse_and_cache_ipv6) {
    std::vector<resp3_node> nodes{
        resp3_node{.data_type = resp3_type::array, .aggregate_size = 2},
        resp3_node{.data_type = resp3_type::array, .aggregate_size = 4},
        resp3_node{.data_type = resp3_type::number, .value = "0"},
        resp3_node{.data_type = resp3_type::number, .value = "8191"},
        resp3_node{.data_type = resp3_type::array, .aggregate_size = 3},
        resp3_node{.data_type = resp3_type::blob_string, .value = "127.0.0.1"},
        resp3_node{.data_type = resp3_type::number, .value = "7000"},
        resp3_node{.data_type = resp3_type::blob_string, .value = "node-a"},
        resp3_node{.data_type = resp3_type::array, .aggregate_size = 3},
        resp3_node{.data_type = resp3_type::blob_string, .value = "127.0.0.1"},
        resp3_node{.data_type = resp3_type::number, .value = "7003"},
        resp3_node{.data_type = resp3_type::blob_string, .value = "node-a-replica"},
        resp3_node{.data_type = resp3_type::array, .aggregate_size = 3},
        resp3_node{.data_type = resp3_type::number, .value = "8192"},
        resp3_node{.data_type = resp3_type::number, .value = "16383"},
        resp3_node{.data_type = resp3_type::array, .aggregate_size = 3},
        resp3_node{.data_type = resp3_type::blob_string, .value = "2001:db8::10"},
        resp3_node{.data_type = resp3_type::number, .value = "7001"},
        resp3_node{.data_type = resp3_type::blob_string, .value = "node-b"},
    };

    auto ranges = client::parse_cluster_slots(nodes);
    ASSERT_TRUE(ranges.has_value());
    ASSERT_EQ(ranges->size(), std::size_t{2});
    ASSERT_EQ(ranges->at(0).start, 0);
    ASSERT_EQ(ranges->at(0).end, 8191);
    ASSERT_EQ(ranges->at(0).master.host, std::string("127.0.0.1"));
    ASSERT_EQ(ranges->at(0).master.port, 7000);
    ASSERT_EQ(ranges->at(0).replicas.size(), std::size_t{1});
    ASSERT_EQ(ranges->at(1).master.host, std::string("2001:db8::10"));
    ASSERT_EQ(ranges->at(1).master.port, 7001);

    cluster_slot_cache cache;
    cache.update(*ranges);
    ASSERT_EQ(cache.covered_slots(), std::size_t{16384});
    auto first = cache.endpoint_for_slot(1);
    auto last = cache.endpoint_for_slot(16383);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(last.has_value());
    ASSERT_EQ(first->port, 7000);
    ASSERT_EQ(last->host, std::string("2001:db8::10"));
}

RUN_TESTS()
