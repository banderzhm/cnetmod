#include "test_framework.hpp"

import std;
import cnetmod.protocol.grpc;

using namespace cnetmod::grpc;

namespace {

auto hex(std::span<const std::byte> bytes) -> std::string {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (auto b : bytes) {
        auto v = std::to_integer<unsigned char>(b);
        out.push_back(digits[v >> 4]);
        out.push_back(digits[v & 0x0f]);
    }
    return out;
}

} // namespace

TEST(grpc_proto_wire_matches_known_python_oracle_vector) {
    byte_buffer msg;
    proto::append_string(msg, 1, "Ada");
    proto::append_uint64(msg, 2, 150);
    proto::append_bool(msg, 3, true);

    ASSERT_EQ(hex(msg), std::string("0a034164611096011801"));

    auto fields = proto::decode_message(msg);
    ASSERT_TRUE(fields.has_value());
    ASSERT_EQ(fields->size(), std::size_t{3});

    auto* name = proto::find_first(*fields, 1);
    ASSERT_TRUE(name != nullptr);
    ASSERT_EQ(proto::field_string(*name).value_or(""), std::string("Ada"));

    auto* sequence = proto::find_first(*fields, 2);
    ASSERT_TRUE(sequence != nullptr);
    ASSERT_EQ(proto::field_uint64(*sequence).value_or(0), std::uint64_t{150});

    auto* urgent = proto::find_first(*fields, 3);
    ASSERT_TRUE(urgent != nullptr);
    ASSERT_TRUE(proto::field_bool(*urgent).value_or(false));
}

TEST(grpc_proto_schema_parses_messages_and_streaming_service) {
    constexpr std::string_view schema = R"proto(
syntax = "proto3";
package cnetmod.testing.grpc;

message EchoRequest {
  string name = 1;
  uint64 sequence = 2;
  bool urgent = 3;
}

message EchoReply {
  string message = 1;
  uint64 sequence = 2;
}

service EchoService {
  rpc Say(EchoRequest) returns (EchoReply);
  rpc Chat(stream EchoRequest) returns (stream EchoReply);
}
)proto";

    auto parsed = proto::parse_schema(schema);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->package, std::string("cnetmod.testing.grpc"));
    ASSERT_EQ(parsed->messages.size(), std::size_t{2});
    ASSERT_EQ(parsed->messages[0].name, std::string("EchoRequest"));
    ASSERT_EQ(parsed->messages[0].fields.size(), std::size_t{3});
    ASSERT_EQ(parsed->messages[0].fields[0].name, std::string("name"));
    ASSERT_EQ(parsed->messages[0].fields[0].number, std::uint32_t{1});
    ASSERT_EQ(parsed->services.size(), std::size_t{1});
    ASSERT_EQ(parsed->services[0].name, std::string("EchoService"));
    ASSERT_EQ(parsed->services[0].rpcs.size(), std::size_t{2});
    ASSERT_FALSE(parsed->services[0].rpcs[0].client_streaming);
    ASSERT_FALSE(parsed->services[0].rpcs[0].server_streaming);
    ASSERT_TRUE(parsed->services[0].rpcs[1].client_streaming);
    ASSERT_TRUE(parsed->services[0].rpcs[1].server_streaming);

    auto* service = proto::find_service(*parsed, "EchoService");
    ASSERT_TRUE(service != nullptr);
    auto* say = proto::find_rpc(*service, "Say");
    ASSERT_TRUE(say != nullptr);
    ASSERT_EQ(proto::rpc_path(*parsed, *service, *say),
              std::string("/cnetmod.testing.grpc.EchoService/Say"));
    ASSERT_EQ(static_cast<int>(proto::rpc_kind(*say)),
              static_cast<int>(call_kind::unary));

    auto* chat = proto::find_rpc(*service, "Chat");
    ASSERT_TRUE(chat != nullptr);
    ASSERT_EQ(proto::rpc_path(*parsed, *service, *chat),
              std::string("/cnetmod.testing.grpc.EchoService/Chat"));
    ASSERT_EQ(static_cast<int>(proto::rpc_kind(*chat)),
              static_cast<int>(call_kind::bidi_streaming));
}

RUN_TESTS()
