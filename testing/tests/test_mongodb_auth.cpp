#include "test_framework.hpp"
#include <cnetmod/config.hpp>
import std;
import cnetmod.protocol.mongodb;
using namespace cnetmod::mongodb;

TEST(mongodb_scram_password_uses_rfc4013_preparation)
{
    auto mapped = prepare_scram_sha256_password("I\xc2\xad" "X");
    ASSERT_TRUE(mapped.has_value());
    ASSERT_EQ(*mapped, std::string{"IX"});
    auto normalized = prepare_scram_sha256_password("\xc2\xaa");
    ASSERT_TRUE(normalized.has_value());
    ASSERT_EQ(*normalized, std::string{"a"});
    ASSERT_FALSE(prepare_scram_sha256_password("bad\x07").has_value());
    ASSERT_FALSE(prepare_scram_sha256_password("\xff").has_value());
}

TEST(mongodb_scram_rejects_duplicate_mandatory_and_invalid_base64_attributes)
{
#ifdef CNETMOD_HAS_SSL
    for (const auto mode : {0, 1, 2, 3})
    {
        scram_sha256_client client{"user", "secret"};
        auto first = client.initial_message();
        ASSERT_TRUE(first.has_value());
        const std::string message(reinterpret_cast<const char*>(first->data()), first->size());
        auto nonce = message.substr(message.find(",r=") + 3);
        auto server = "r=" + nonce + "server,s=AAAA,i=4096";
        if (mode == 0)
            server += ",r=duplicate";
        else if (mode == 1)
            server += ",m=unsupported";
        else if (mode == 2)
            server = "r=" + nonce + "server,s=====,i=4096";
        auto reply = client.respond(std::as_bytes(std::span(server)));
        if (mode == 3)
            ASSERT_TRUE(reply.has_value());
        else
            ASSERT_FALSE(reply.has_value());
    }
#endif
}
RUN_TESTS()
