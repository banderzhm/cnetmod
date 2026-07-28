#include "test_framework.hpp"

import std;
import cnetmod.protocol.socks5;

using namespace cnetmod::socks5;

TEST(socks5_ipv4_address_roundtrip) {
    socks5_address addr{
        .type = address_type::ipv4,
        .host = "127.0.0.1",
        .port = 1080,
    };

    auto raw = addr.serialize();
    auto parsed = socks5_address::parse(raw.data(), raw.size());

    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->first.host, std::string("127.0.0.1"));
    ASSERT_EQ(parsed->first.port, 1080);
    ASSERT_EQ(parsed->second, raw.size());
}

TEST(socks5_ipv6_address_roundtrip) {
    socks5_address addr{
        .type = address_type::ipv6,
        .host = "::1",
        .port = 443,
    };

    auto raw = addr.serialize();
    auto parsed = socks5_address::parse(raw.data(), raw.size());

    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->first.host, std::string("::1"));
    ASSERT_EQ(parsed->first.port, 443);
    ASSERT_EQ(parsed->second, raw.size());
}

TEST(socks5_udp_datagram_roundtrip) {
    udp_datagram dgram{
        .address = socks5_address{
            .type = address_type::domain_name,
            .host = "example.com",
            .port = 53,
        },
        .payload = {
            std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        },
    };

    auto raw = dgram.serialize();
    auto parsed = udp_datagram::parse(raw.data(), raw.size());

    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->reserved, 0);
    ASSERT_EQ(static_cast<int>(parsed->fragment), 0);
    ASSERT_EQ(parsed->address.host, std::string("example.com"));
    ASSERT_EQ(parsed->address.port, 53);
    ASSERT_EQ(parsed->payload.size(), std::size_t{4});
    ASSERT_EQ(static_cast<int>(std::to_integer<unsigned char>(parsed->payload[2])), 0x03);
}

TEST(socks5_request_supports_all_commands) {
    for (auto cmd : {command::connect, command::bind, command::udp_associate}) {
        socks5_request req{
            .cmd = cmd,
            .address = socks5_address{
                .type = address_type::ipv4,
                .host = "0.0.0.0",
                .port = 0,
            },
        };

        auto raw = req.serialize();
        auto parsed = socks5_request::parse(raw.data(), raw.size());

        ASSERT_TRUE(parsed.has_value());
        ASSERT_EQ(static_cast<int>(parsed->cmd), static_cast<int>(cmd));
    }
}

TEST(socks5_request_ipv6_target_roundtrip) {
    socks5_request req{
        .cmd = command::connect,
        .address = socks5_address{
            .type = address_type::ipv6,
            .host = "2001:db8::1",
            .port = 443,
        },
    };

    auto raw = req.serialize();
    auto parsed = socks5_request::parse(raw.data(), raw.size());

    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->address.host, std::string("2001:db8::1"));
    ASSERT_EQ(parsed->address.port, 443);
    ASSERT_EQ(static_cast<int>(parsed->address.type), static_cast<int>(address_type::ipv6));
}

TEST(socks5_gssapi_authentication_message_roundtrip) {
    gssapi_message message{
        .type = gssapi_message_type::authentication,
        .token = {std::byte{0x10}, std::byte{0x20}, std::byte{0x30}},
    };

    auto raw = message.serialize();
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw->size(), std::size_t{7});
    ASSERT_EQ(std::to_integer<unsigned int>((*raw)[0]), 1U);
    ASSERT_EQ(std::to_integer<unsigned int>((*raw)[1]), 1U);
    ASSERT_EQ(std::to_integer<unsigned int>((*raw)[3]), 3U);

    auto parsed = gssapi_message::parse(raw->data(), raw->size());
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(static_cast<int>(parsed->type),
              static_cast<int>(gssapi_message_type::authentication));
    ASSERT_EQ(parsed->token.size(), std::size_t{3});
    ASSERT_EQ(std::to_integer<unsigned int>(parsed->token[2]), 0x30U);
}

TEST(socks5_gssapi_abort_is_two_octets) {
    gssapi_message message{.type = gssapi_message_type::abort};
    auto raw = message.serialize();
    ASSERT_TRUE(raw.has_value());
    ASSERT_EQ(raw->size(), std::size_t{2});

    auto parsed = gssapi_message::parse(raw->data(), raw->size());
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(static_cast<int>(parsed->type),
              static_cast<int>(gssapi_message_type::abort));
}

TEST(socks5_gssapi_rejects_oversized_token) {
    gssapi_message message{
        .type = gssapi_message_type::encapsulated_data,
        .token = std::vector<std::byte>(65536),
    };
    auto raw = message.serialize();
    ASSERT_FALSE(raw.has_value());
    ASSERT_EQ(raw.error(), std::make_error_code(std::errc::message_size));
}

TEST(socks5_gssapi_context_requires_all_callbacks) {
    gssapi_context context;
    ASSERT_FALSE(static_cast<bool>(context));

    context.step = [](std::span<const std::byte>)
        -> std::expected<gssapi_step_result, std::error_code> {
        return gssapi_step_result{.complete = true};
    };
    context.wrap = [](std::span<const std::byte> payload, bool)
        -> std::expected<std::vector<std::byte>, std::error_code> {
        return std::vector<std::byte>{payload.begin(), payload.end()};
    };
    context.unwrap = [](std::span<const std::byte> token)
        -> std::expected<gssapi_unwrap_result, std::error_code> {
        return gssapi_unwrap_result{
            .payload = std::vector<std::byte>{token.begin(), token.end()}};
    };
    ASSERT_TRUE(static_cast<bool>(context));
}

RUN_TESTS()
