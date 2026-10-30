#include "test_framework.hpp"
#include <cnetmod/config.hpp>
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor.async_op;
import cnetmod.protocol.mongodb;
using namespace cnetmod::mongodb;

TEST(mongodb_aws_sigv4_matches_independent_reference)
{
#ifdef CNETMOD_HAS_SSL
    std::array<std::byte, 64> nonce{};
    for (std::size_t i{}; i < nonce.size(); ++i)
        nonce[i] = static_cast<std::byte>(i);
    const aws_credentials credentials{"AKIDEXAMPLE", "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY", "SESSION", {}};
    const auto signed_request = sign_aws_sts_request("sts.us-east-1.amazonaws.com", nonce, credentials, "20150830T123600Z");
    ASSERT_TRUE(signed_request.has_value());
    ASSERT_EQ(signed_request->amz_date, std::string{"20150830T123600Z"});
    ASSERT_EQ(signed_request->authorization, std::string{"AWS4-HMAC-SHA256 Credential=AKIDEXAMPLE/20150830/us-east-1/sts/aws4_request, " "SignedHeaders=content-length;content-type;host;x-amz-date;x-amz-security-token;x-mongodb-gs2-cb-flag;x-mongodb-server-nonce, " "Signature=4d3f23e270c5d2c81182ebb259f2da1a2bf87abefb6d6da291ca3443480b3702"});
    const std::array<std::byte, 32> client{};
    auto challenge = encode_bson_document(bson_document{{"s", bson_binary{0, {nonce.begin(), nonce.end()}}}, {"h", "sts.amazonaws.com"}});
    ASSERT_TRUE(challenge.has_value());
    ASSERT_FALSE(aws_challenge_response(*challenge, client, credentials, "20150830T123600Z").has_value());
#endif
}

TEST(mongodb_aws_rejects_invalid_hosts_and_partial_credentials)
{
    ASSERT_EQ(*aws_sts_region("sts.amazonaws.com"), std::string{"us-east-1"});
    ASSERT_EQ(*aws_sts_region("sts.cn-north-1.amazonaws.com.cn"), std::string{"cn-north-1"});
    for (const auto host : {"", "sts..com", ".amazonaws.com", "sts.amazonaws.", "sts.amazonaws.com/path", "host\r\nX:evil"})
        ASSERT_FALSE(aws_sts_region(host).has_value());
    ASSERT_FALSE(validate_aws_credentials({"access", "", "", {}}).has_value());
    ASSERT_FALSE(validate_aws_credentials({"access", "secret", "bad\nheader", {}}).has_value());
}

TEST(mongodb_sasl_reply_enforces_conversation_identity)
{
    const bson_document reply{{"ok", 1.0}, {"conversationId", 7}, {"done", true}, {"payload", bson_binary{}}};
    ASSERT_TRUE(parse_sasl_reply(reply, 7).has_value());
    ASSERT_FALSE(parse_sasl_reply(reply, 8).has_value());
    auto failed_speculation = reply;
    failed_speculation.set("ok", 0.0);
    ASSERT_FALSE(parse_sasl_reply(failed_speculation).has_value());
    ASSERT_FALSE(parse_sasl_reply(bson_document{{"conversationId", 7}, {"done", 1}, {"payload", bson_binary{}}}).has_value());
}

TEST(mongodb_oidc_cache_serializes_fetches_and_conditionally_invalidates)
{
    auto io = cnetmod::make_io_context();
    unsigned calls{};
    oidc_token_cache cache{*io, [&](oidc_callback_parameters parameters) -> cnetmod::task<result<oidc_access_token>>
        {
            ASSERT_EQ(parameters.version, 1);
            ++calls;
            co_return oidc_access_token{"token-" + std::to_string(calls), std::chrono::seconds{3600}};
        }};
    auto work = [&]() -> cnetmod::task<void>
    {
        auto [one, two, three] = co_await cnetmod::when_all(cache.get(), cache.get(), cache.get());
        ASSERT_TRUE(one.has_value() && two.has_value() && three.has_value());
        ASSERT_EQ(calls, 1U);
        ASSERT_EQ(one->access_token, two->access_token);
        co_await cache.invalidate("old-unrelated-token");
        ASSERT_TRUE((co_await cache.peek()).has_value());
        co_await cache.invalidate(one->access_token);
        auto fresh = co_await cache.get();
        ASSERT_TRUE(fresh.has_value());
        ASSERT_EQ(calls, 2U);
        ASSERT_EQ(fresh->access_token, std::string{"token-2"});
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
}

TEST(mongodb_oidc_waiters_honor_their_own_deadline)
{
    auto io = cnetmod::make_io_context();
    unsigned calls{};
    bool first_finished{}, second_finished_before_first{};
    oidc_token_cache cache{*io, [&](oidc_callback_parameters) -> cnetmod::task<result<oidc_access_token>>
        {
            ++calls;
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{100});
            co_return oidc_access_token{"shared-token", std::chrono::seconds{3600}};
        }};
    auto first = [&]() -> cnetmod::task<void>
    {
        const auto token = co_await cache.get(std::chrono::seconds{1});
        ASSERT_TRUE(token.has_value());
        first_finished = true;
    };
    auto second = [&]() -> cnetmod::task<void>
    {
        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{10});
        const auto token = co_await cache.get(std::chrono::milliseconds{5});
        ASSERT_FALSE(token.has_value());
        ASSERT_TRUE(token.error().code == error_code::operation_timed_out);
        second_finished_before_first = !first_finished;
    };
    auto work = [&]() -> cnetmod::task<void>
    {
        co_await cnetmod::when_all(first(), second());
        ASSERT_EQ(calls, 1U);
        ASSERT_TRUE(second_finished_before_first);
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
}
RUN_TESTS()
