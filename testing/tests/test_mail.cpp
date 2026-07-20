#include "test_framework.hpp"

import std;
import cnetmod.protocol.mail;

using namespace cnetmod::mail;

TEST(smtp_multiline_reply_parses) {
  const auto parsed =
      parse_reply("250-mail.example.test greets client.example.test\r\n"
                  "250-SIZE 10485760\r\n"
                  "250 AUTH PLAIN LOGIN\r\n");

  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQ(parsed->code, std::uint16_t{250});
  ASSERT_TRUE(parsed->successful());
  ASSERT_EQ(parsed->lines.size(), std::size_t{3});
}

TEST(smtp_command_rejects_line_injection) {
  const std::array<std::string_view, 1> arguments{
      "sender@example.test\r\nRCPT TO:<victim@example.test>"};
  const auto wire = serialize_command("MAIL", arguments);

  ASSERT_FALSE(wire.has_value());
}

TEST(smtp_data_dot_stuff_roundtrip) {
  constexpr std::string_view body =
      "first line\r\n.leading dot\r\n..double dot\r\n";
  const auto stuffed = dot_stuff(body);

  ASSERT_TRUE(stuffed.has_value());
  const auto unstuffed = dot_unstuff(*stuffed);
  ASSERT_TRUE(unstuffed.has_value());
  ASSERT_EQ(*unstuffed, std::string(body));
}

TEST(smtp_envelope_requires_sender_and_recipient) {
  envelope delivery;
  ASSERT_FALSE(delivery.valid());

  delivery.sender = "sender@example.test";
  delivery.add_recipient("recipient@example.test");
  ASSERT_TRUE(delivery.valid());
}

RUN_TESTS()
