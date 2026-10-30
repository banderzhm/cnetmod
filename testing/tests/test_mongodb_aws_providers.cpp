#include "test_framework.hpp"
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor.pool;
import cnetmod.protocol.mongodb;
import cnetmod.integration.mongodb_cloud;
#include "mongodb_provider_environment.hpp"
namespace mg = cnetmod::mongodb;
namespace cloud = mg::cloud;
using mongodb_test::provider_environment;
constexpr std::string_view credentials_json{R"({"AccessKeyId":"METAKEY","SecretAccessKey":"test-secret","Token":"test-session","Expiration":"2099-01-01T00:00:00Z"})"};

TEST(mongodb_aws_environment_credentials_are_read_dynamically)
{
    provider_environment environment;
    provider_environment::set("AWS_ACCESS_KEY_ID", "FIRST");
    provider_environment::set("AWS_SECRET_ACCESS_KEY", "secret");
    auto io = cnetmod::make_io_context();
    cnetmod::thread_pool pool{1};
    unsigned calls{};
    auto provider = cloud::make_default_aws_provider(*io, pool,
        [&](cloud::metadata_request) -> cnetmod::task<mg::result<cloud::metadata_response>>
        {
            ++calls;
            co_return std::unexpected(mg::make_error(mg::error_code::authentication_failed, "unexpected HTTP"));
        });
    auto work = [&]() -> cnetmod::task<void>
    {
        auto first = co_await provider->get();
        ASSERT_TRUE(first.has_value());
        ASSERT_EQ(first->access_key_id, std::string{"FIRST"});
        provider_environment::set("AWS_ACCESS_KEY_ID", "SECOND");
        auto second = co_await provider->get();
        ASSERT_TRUE(second.has_value());
        ASSERT_EQ(second->access_key_id, std::string{"SECOND"});
        ASSERT_EQ(calls, 0U);
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
}

TEST(mongodb_aws_ecs_credentials_and_authorization_are_cached)
{
    provider_environment environment;
    provider_environment::set("AWS_CONTAINER_CREDENTIALS_RELATIVE_URI", "/creds/path");
    provider_environment::set("AWS_CONTAINER_AUTHORIZATION_TOKEN", "ecs-authorization");
    auto io = cnetmod::make_io_context();
    cnetmod::thread_pool pool{1};
    unsigned calls{};
    auto provider = cloud::make_default_aws_provider(*io, pool,
        [&](cloud::metadata_request request) -> cnetmod::task<mg::result<cloud::metadata_response>>
        {
            ++calls;
            ASSERT_EQ(request.url, std::string{"http://169.254.170.2/creds/path"});
            ASSERT_EQ(request.headers.at("Authorization"), std::string{"ecs-authorization"});
            co_return cloud::metadata_response{200, std::string(credentials_json)};
        });
    auto work = [&]() -> cnetmod::task<void>
    {
        auto first = co_await provider->get();
        ASSERT_TRUE(first.has_value());
        ASSERT_EQ(first->session_token, std::string{"test-session"});
        auto second = co_await provider->get();
        ASSERT_TRUE(second.has_value());
        ASSERT_EQ(calls, 1U);
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
}

TEST(mongodb_aws_imdsv2_performs_token_role_and_credential_steps)
{
    provider_environment environment;
    auto io = cnetmod::make_io_context();
    cnetmod::thread_pool pool{1};
    unsigned calls{};
    auto provider = cloud::make_default_aws_provider(*io, pool,
        [&](cloud::metadata_request request) -> cnetmod::task<mg::result<cloud::metadata_response>>
        {
            ++calls;
            if (calls == 1)
            {
                ASSERT_TRUE(request.method == cloud::metadata_method::put);
                ASSERT_EQ(request.url, std::string{"http://169.254.169.254/latest/api/token"});
                co_return cloud::metadata_response{200, "imds-session"};
            }
            ASSERT_EQ(request.headers.at("X-aws-ec2-metadata-token"), std::string{"imds-session"});
            if (calls == 2)
                co_return cloud::metadata_response{200, "InstanceRole\n"};
            ASSERT_TRUE(request.url.ends_with("/InstanceRole"));
            co_return cloud::metadata_response{200, std::string(credentials_json)};
        });
    auto work = [&]() -> cnetmod::task<void>
    {
        auto first = co_await provider->get();
        ASSERT_TRUE(first.has_value());
        ASSERT_EQ(first->access_key_id, std::string{"METAKEY"});
        ASSERT_EQ(calls, 3U);
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
}

TEST(mongodb_aws_web_identity_reads_file_encodes_form_and_parses_fractional_expiry)
{
    provider_environment environment;
    mongodb_test::token_file token{"token-+/&\n"};
    provider_environment::set("AWS_WEB_IDENTITY_TOKEN_FILE", token.path.string());
    provider_environment::set("AWS_ROLE_ARN", "arn:aws:iam::123:role/role");
    provider_environment::set("AWS_ROLE_SESSION_NAME", "test-session");
    provider_environment::set("AWS_REGION", "cn-north-1");
    auto io = cnetmod::make_io_context();
    cnetmod::thread_pool pool{1};
    unsigned calls{};
    auto provider = cloud::make_default_aws_provider(*io, pool,
        [&](cloud::metadata_request request) -> cnetmod::task<mg::result<cloud::metadata_response>>
        {
            ++calls;
            ASSERT_TRUE(request.method == cloud::metadata_method::post);
            ASSERT_EQ(request.url, std::string{"https://sts.cn-north-1.amazonaws.com.cn/"});
            ASSERT_TRUE(request.body.ends_with("WebIdentityToken=token-%2B%2F%26"));
            co_return cloud::metadata_response{200, R"(<AssumeRoleWithWebIdentityResponse xmlns="https://sts.amazonaws.com/doc/2011-06-15/"><AssumeRoleWithWebIdentityResult><Credentials><AccessKeyId>WEBKEY</AccessKeyId><SecretAccessKey>secret</SecretAccessKey><SessionToken>session</SessionToken><Expiration>2099-01-01T00:00:00.125Z</Expiration></Credentials></AssumeRoleWithWebIdentityResult></AssumeRoleWithWebIdentityResponse>)"};
        });
    auto work = [&]() -> cnetmod::task<void>
    {
        auto credentials = co_await provider->get();
        ASSERT_TRUE(credentials.has_value());
        ASSERT_EQ(credentials->access_key_id, std::string{"WEBKEY"});
        ASSERT_EQ(calls, 1U);
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
}

TEST(mongodb_aws_rejects_untrusted_ecs_http_and_disabled_imds)
{
    provider_environment environment;
    provider_environment::set("AWS_CONTAINER_CREDENTIALS_FULL_URI", "http://attacker.example/credentials");
    auto io = cnetmod::make_io_context();
    cnetmod::thread_pool pool{1};
    unsigned calls{};
    auto provider = cloud::make_default_aws_provider(*io, pool,
        [&](cloud::metadata_request) -> cnetmod::task<mg::result<cloud::metadata_response>>
        {
            ++calls;
            co_return cloud::metadata_response{500, "not reached"};
        });
    auto work = [&]() -> cnetmod::task<void>
    {
        ASSERT_FALSE((co_await provider->get()).has_value());
        provider_environment::set("AWS_CONTAINER_CREDENTIALS_FULL_URI", {});
        provider_environment::set("AWS_EC2_METADATA_DISABLED", "true");
        ASSERT_FALSE((co_await provider->get()).has_value());
        ASSERT_EQ(calls, 0U);
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
}
RUN_TESTS()
