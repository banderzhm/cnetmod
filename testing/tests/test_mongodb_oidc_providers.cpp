#include "test_framework.hpp"
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor.pool;
import cnetmod.protocol.mongodb;
import cnetmod.application.mongodb_credentials;
#include "mongodb_provider_environment.hpp"
namespace mg = cnetmod::mongodb;
namespace cloud = cnetmod::application::mongodb_credentials;

TEST(mongodb_oidc_metadata_requests_are_encoded_and_tokens_cached)
{
    auto io = cnetmod::make_io_context();
    cnetmod::thread_pool pool{1};
    unsigned azure_calls{}, gcp_calls{};
    cloud::metadata_transport transport = [&](cloud::metadata_request request) -> cnetmod::task<mg::result<cloud::metadata_response>>
    {
        if (request.url.starts_with("http://169.254.169.254/"))
        {
            ++azure_calls;
            ASSERT_TRUE(request.url.ends_with("resource=api%3A%2F%2Fresource%3Fx%3D1%26y%3D2&client_id=user%26name"));
            ASSERT_EQ(request.headers.at("Metadata"), std::string{"true"});
            co_return cloud::metadata_response{200, R"({"access_token":"azure-token","expires_in":"3600"})"};
        }
        ++gcp_calls;
        ASSERT_TRUE(request.url.ends_with("audience=api%3A%2F%2Faudience"));
        ASSERT_EQ(request.headers.at("Metadata-Flavor"), std::string{"Google"});
        co_return cloud::metadata_response{200, "gcp-token"};
    };
    auto azure = cloud::make_oidc_workload_provider(*io, pool, {mg::oidc_environment::azure, "api://resource?x=1&y=2", "user&name"}, transport);
    auto gcp = cloud::make_oidc_workload_provider(*io, pool, {mg::oidc_environment::gcp, "api://audience", {}}, transport);
    ASSERT_TRUE(azure.has_value() && gcp.has_value());
    auto work = [&]() -> cnetmod::task<void>
    {
        auto a = co_await (*azure)->get();
        auto a2 = co_await (*azure)->get();
        auto g = co_await (*gcp)->get();
        ASSERT_TRUE(a.has_value() && a2.has_value() && g.has_value());
        ASSERT_EQ(a->access_token, std::string{"azure-token"});
        ASSERT_EQ(g->access_token, std::string{"gcp-token"});
        ASSERT_EQ(azure_calls, 1U);
        ASSERT_EQ(gcp_calls, 1U);
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
}

TEST(mongodb_oidc_rejects_invalid_provider_configuration_and_error_bodies)
{
    auto io = cnetmod::make_io_context();
    cnetmod::thread_pool pool{1};
    ASSERT_FALSE(cloud::make_oidc_workload_provider(*io, pool, {mg::oidc_environment::azure, {}, {}}).has_value());
    ASSERT_FALSE(cloud::make_oidc_workload_provider(*io, pool, {mg::oidc_environment::kubernetes, "forbidden-resource", {}}).has_value());
    mg::connection_options options;
    options.authentication = mg::authentication_mechanism::oidc;
    options.oidc_provider = std::make_shared<mg::oidc_token_cache>(*io, mg::oidc_token_callback{});
    options.oidc_application_environment = mg::oidc_environment::azure;
    ASSERT_FALSE(cloud::configure_cloud_credentials(options, *io, pool).has_value());
    cloud::metadata_transport failure = [](cloud::metadata_request) -> cnetmod::task<mg::result<cloud::metadata_response>>
    {
        co_return cloud::metadata_response{500, "SECRET-RESPONSE-MUST-NOT-ESCAPE"};
    };
    auto provider = cloud::make_oidc_workload_provider(*io, pool, {mg::oidc_environment::gcp, "audience", {}}, failure);
    ASSERT_TRUE(provider.has_value());
    auto work = [&]() -> cnetmod::task<void>
    {
        const auto token = co_await (*provider)->get();
        ASSERT_FALSE(token.has_value());
        ASSERT_TRUE(token.error().message.find("SECRET") == std::string::npos);
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
}

TEST(mongodb_kubernetes_and_test_token_files_use_the_correct_precedence)
{
    mongodb_test::provider_environment environment;
    mongodb_test::token_file azure{"azure-file-token\n"}, aws{"aws-file-token"};
    mongodb_test::provider_environment::set("AZURE_FEDERATED_TOKEN_FILE", azure.path.string());
    mongodb_test::provider_environment::set("AWS_WEB_IDENTITY_TOKEN_FILE", aws.path.string());
    mongodb_test::provider_environment::set("OIDC_TOKEN_FILE", aws.path.string());
    auto io = cnetmod::make_io_context();
    cnetmod::thread_pool pool{1};
    auto kubernetes = cloud::make_oidc_workload_provider(*io, pool, {mg::oidc_environment::kubernetes, {}, {}});
    auto test = cloud::make_oidc_workload_provider(*io, pool, {mg::oidc_environment::test, {}, {}});
    ASSERT_TRUE(kubernetes.has_value() && test.has_value());
    auto work = [&]() -> cnetmod::task<void>
    {
        const auto k = co_await (*kubernetes)->get();
        const auto t = co_await (*test)->get();
        ASSERT_TRUE(k.has_value() && t.has_value());
        ASSERT_EQ(k->access_token, std::string{"azure-file-token"});
        ASSERT_EQ(t->access_token, std::string{"aws-file-token"});
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
}
RUN_TESTS()
