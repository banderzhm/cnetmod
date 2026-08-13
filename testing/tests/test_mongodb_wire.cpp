#include "test_framework.hpp"

import std;
import cnetmod.protocol.mongodb;

using namespace cnetmod::mongodb;

TEST(mongodb_op_msg_decodes_more_to_come_response_flag)
{
    bson_document reply{{"ok", bson_value{1.0}}, {"cursor", bson_value{std::int64_t{0}}}};
    auto wire = encode_command_message(17, reply, 1024U * 1024U);
    ASSERT_TRUE(wire.has_value());
    // OP_MSG flags immediately follow the 16-byte message header. A response
    // with moreToCome has the same body as a one-shot reply, but clients must
    // keep reading it through command_stream rather than reuse the socket.
    ASSERT_TRUE(wire->size() > 20U);
    (*wire)[16] = std::byte{op_message_more_to_come};
    (*wire)[17] = std::byte{0};
    (*wire)[18] = std::byte{0};
    (*wire)[19] = std::byte{0};

    auto decoded = decode_command_message(*wire, 1024U * 1024U);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE((decoded->flags & op_message_more_to_come) != 0U);
    ASSERT_TRUE(decoded->body.contains("ok"));
}

TEST(mongodb_op_msg_rejects_more_to_come_as_a_request_flag)
{
    bson_document command{{"find", bson_value{"users"}}};
    auto encoded = encode_command_message(1, command, 1024U * 1024U,
        op_message_more_to_come);
    ASSERT_FALSE(encoded.has_value());
}

TEST(mongodb_op_msg_allows_exhaust_capability_on_requests)
{
    bson_document command{{"find", bson_value{"users"}}};
    auto encoded = encode_command_message(1, command, 1024U * 1024U,
        op_message_exhaust_allowed);
    ASSERT_TRUE(encoded.has_value());
}

RUN_TESTS()
