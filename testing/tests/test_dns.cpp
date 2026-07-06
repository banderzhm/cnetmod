#include "test_framework.hpp"

import std;
import cnetmod.core.address;
import cnetmod.protocol.dns;

using namespace cnetmod::dns;

TEST(dns_query_roundtrip) {
    auto msg = make_query("example.com", record_type::AAAA, 0x1234);
    auto raw = serialize_message(msg);
    ASSERT_TRUE(raw.has_value());

    auto parsed = parse_message(*raw);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->id, 0x1234);
    ASSERT_TRUE(parsed->query);
    ASSERT_EQ(parsed->questions.size(), std::size_t{1});
    ASSERT_EQ(parsed->questions[0].name, std::string("example.com"));
    ASSERT_EQ(static_cast<int>(parsed->questions[0].type), static_cast<int>(record_type::AAAA));
}

TEST(dns_response_a_record_roundtrip) {
    auto addr = cnetmod::ipv4_address::from_string("127.0.0.1");
    ASSERT_TRUE(addr.has_value());

    message resp;
    resp.id = 7;
    resp.query = false;
    resp.recursion_available = true;
    resp.questions.push_back(question{.name = "local.test", .type = record_type::A});
    resp.answers.push_back(a_record("local.test", *addr, 30));

    auto raw = serialize_message(resp);
    ASSERT_TRUE(raw.has_value());
    auto parsed = parse_message(*raw);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_FALSE(parsed->query);
    ASSERT_EQ(parsed->answers.size(), std::size_t{1});
    ASSERT_EQ(parsed->answers[0].data.size(), std::size_t{4});
}

RUN_TESTS()
