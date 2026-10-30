import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.protocol.mongodb;
#include "mongodb_cloud_test_certificate.hpp"
namespace mg = cnetmod::mongodb;

int main(int argc, char** argv)
{
    if (argc == 4 && std::string_view(argv[1]) == "--write-test-certificate")
        return write_mongodb_cloud_test_certificate(argv[2], argv[3]) ? 0 : 1;
    if (argc != 6)
        return 2;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    mg::connection_options options;
    options.port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    options.tls = true;
    options.tls_ca_file = argv[2];
    options.tls_sni = "localhost";
    options.authentication_database = "$external";
    options.authentication_database_explicit = true;
    options.command_timeout = std::chrono::seconds{3};
    options.connect_timeout = std::chrono::seconds{3};
    const std::string mode{argv[4]};
    unsigned callbacks{};
    if (mode.starts_with("aws"))
    {
        options.authentication = mg::authentication_mechanism::aws;
        options.aws_provider = std::make_shared<mg::aws_credential_cache>([&]() -> cnetmod::task<mg::result<mg::aws_credentials>>
            {
                ++callbacks;
                co_return mg::aws_credentials{"AKIDEXAMPLE", "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY", "SESSION", {}};
            });
    }
    else if (mode == "x509")
    {
        options.authentication = mg::authentication_mechanism::x509;
        options.tls_cert_file = argv[2];
        options.tls_key_file = argv[3];
    }
    else
    {
        options.authentication = mg::authentication_mechanism::oidc;
        options.oidc_provider = std::make_shared<mg::oidc_token_cache>(*io,
            [&](mg::oidc_callback_parameters) -> cnetmod::task<mg::result<mg::oidc_access_token>>
            {
                co_return mg::oidc_access_token{"token-" + std::to_string(++callbacks), std::chrono::seconds{3600}};
            });
    }
    int outcome = 1;
    const bool expect_failure = std::string_view(argv[5]) == "failure";
    auto run = [&]() -> cnetmod::task<void>
    {
        try
        {
            mg::connection connection{*io};
            const auto rounds = mode == "oidc_reauth" || mode == "oidc_cached_retry" ? 2U : 1U;
            for (unsigned round{}; round < rounds; ++round)
            {
                auto connected = co_await connection.connect(options);
                if (!connected)
                {
                    if (expect_failure)
                    {
                        outcome = 0;
                        break;
                    }
                    throw std::runtime_error(connected.error().message);
                }
                if (expect_failure)
                    throw std::runtime_error("invalid authentication was accepted");
                auto ping = co_await connection.ping();
                if (mode == "oidc_twice_391")
                {
                    if (ping || ping.error().server_code != 391)
                        throw std::runtime_error("second reauthentication error was not returned");
                }
                else if (mode == "oidc_reauth_denied")
                {
                    if (ping || ping.error().server_code != 18 || connection.is_open())
                        throw std::runtime_error("failed OIDC reauthentication did not retire the connection");
                }
                else if (!ping)
                    throw std::runtime_error(ping.error().message);
                connection.close();
                outcome = 0;
            }
            if (mode.starts_with("oidc") && callbacks != 2)
                throw std::runtime_error("OIDC cache fetched an unexpected number of tokens");
        }
        catch (const std::exception& error)
        {
            std::println("[FAIL] {}", error.what());
            outcome = 1;
        }
        io->stop();
    };
    cnetmod::spawn(*io, run());
    io->run();
    if (!outcome)
        std::println("[PASS] {} callbacks={}", mode, callbacks);
    return outcome;
}
