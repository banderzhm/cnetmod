#include "test_framework.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

import cnetmod.protocol.quic;
import cnetmod.core.ssl;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.protocol.http.v3.server;
import cnetmod.protocol.http.v3.session;

using namespace cnetmod::quic;

TEST(early_data_replay_cache_consumes_ticket_once)
{
    early_data_replay_cache cache(2);
    constexpr std::array ticket{std::byte{0x01}, std::byte{0x02}};
    const auto expiry = std::chrono::steady_clock::now() + std::chrono::minutes(1);

    ASSERT_TRUE(cache.consume(ticket, expiry));
    ASSERT_FALSE(cache.consume(ticket, expiry));
    ASSERT_EQ(cache.size(), std::size_t{1});
}

TEST(early_data_replay_cache_rejects_expired_and_evicts)
{
    early_data_replay_cache cache(1);
    constexpr std::array first{std::byte{0x01}};
    constexpr std::array second{std::byte{0x02}};
    const auto now = std::chrono::steady_clock::now();

    ASSERT_FALSE(cache.consume(first, now));
    ASSERT_TRUE(cache.consume(first, now + std::chrono::minutes(1)));
    ASSERT_TRUE(cache.consume(second, now + std::chrono::minutes(2)));
    ASSERT_EQ(cache.size(), std::size_t{1});
    ASSERT_TRUE(cache.consume(first, now + std::chrono::minutes(1)));
}

TEST(early_data_replay_cache_consumes_concurrently_once)
{
    early_data_replay_cache cache(8);
    constexpr std::array ticket{std::byte{0x7a}};
    const auto expiry = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    std::atomic_uint accepted{};
    std::array<std::thread, 8> workers;
    for (auto& worker : workers)
    {
        worker = std::thread([&]
            {
                if (cache.consume(ticket, expiry))
                    ++accepted;
            });
    }
    for (auto& worker : workers)
        worker.join();
    ASSERT_EQ(accepted.load(), 1U);
}

TEST(server_ticket_callbacks_require_shared_replay_cache)
{
    auto context = cnetmod::ssl_context::quic_server();
    ASSERT_TRUE(context.has_value());

    server_early_data_ticket_callbacks callbacks;
    callbacks.max_overhead = 16;
    callbacks.seal = [](std::span<const std::byte> plaintext)
        -> std::expected<std::vector<std::byte>, std::error_code>
    {
        return std::vector<std::byte>{plaintext.begin(), plaintext.end()};
    };
    callbacks.open = [](std::span<const std::byte> ciphertext)
        -> std::expected<server_early_data_ticket, std::error_code>
    {
        return server_early_data_ticket{
            .plaintext = {ciphertext.begin(), ciphertext.end()},
            .identity = {std::byte{0x01}},
            .early_data_expires_at = std::chrono::steady_clock::now() + std::chrono::minutes(1),
        };
    };
    ASSERT_FALSE(configure_server_early_data_tickets(*context, callbacks).has_value());

    callbacks.replay_cache = std::make_shared<early_data_replay_cache>();
    ASSERT_TRUE(configure_server_early_data_tickets(*context, callbacks).has_value());
}

TEST(http3_server_requires_explicit_early_data_context)
{
    auto tls = cnetmod::ssl_context::quic_server();
    ASSERT_TRUE(tls.has_value());
    auto context = cnetmod::make_io_context();
    auto address = cnetmod::ip_address::from_string("127.0.0.1");
    ASSERT_TRUE(address.has_value());
    auto server = cnetmod::http::v3::make_http3_server(*context, *tls,
        cnetmod::endpoint{*address, 0},
        [](cnetmod::http::v3::http3_request&, cnetmod::http::v3::http3_response&)
            -> std::error_code
        {
            return {};
        });

    auto callbacks = std::make_shared<server_early_data_ticket_callbacks>();
    ASSERT_FALSE(server->set_early_data_tickets(callbacks, {}).has_value());
    callbacks->seal = [](std::span<const std::byte> value)
        -> std::expected<std::vector<std::byte>, std::error_code>
    {
        return std::vector<std::byte>{value.begin(), value.end()};
    };
    callbacks->open = [](std::span<const std::byte> value)
        -> std::expected<server_early_data_ticket, std::error_code>
    {
        return server_early_data_ticket{
            .plaintext = {value.begin(), value.end()},
            .identity = {std::byte{0x01}},
            .early_data_expires_at = std::chrono::steady_clock::now() + std::chrono::minutes(1)};
    };
    callbacks->replay_cache = std::make_shared<early_data_replay_cache>();
    ASSERT_FALSE(server->set_early_data_tickets(callbacks, {}).has_value());
    ASSERT_TRUE(server->set_early_data_tickets(std::move(callbacks),
                          {std::byte{0x01}, std::byte{0x02}})
            .has_value());
}

TEST(http3_server_inbox_limits_require_bounded_nonzero_values)
{
    auto tls = cnetmod::ssl_context::quic_server();
    ASSERT_TRUE(tls.has_value());
    auto context = cnetmod::make_io_context();
    auto address = cnetmod::ip_address::from_string("127.0.0.1");
    ASSERT_TRUE(address.has_value());
    auto server = cnetmod::http::v3::make_http3_server(*context, *tls,
        cnetmod::endpoint{*address, 0},
        [](cnetmod::http::v3::http3_request&, cnetmod::http::v3::http3_response&)
            -> std::error_code
        {
            return {};
        });

    ASSERT_FALSE(server->set_inbox_limits({.per_connection_datagrams = 1U,
                                              .max_queued_datagrams = 16U,
                                              .max_connection_inboxes = 8U})
            .has_value());
    ASSERT_FALSE(server->set_inbox_limits({.per_connection_datagrams = 8U,
                                              .max_queued_datagrams = 0U,
                                              .max_connection_inboxes = 8U})
            .has_value());
    ASSERT_FALSE(server->set_inbox_limits({.per_connection_datagrams = 8U,
                                              .max_queued_datagrams = 16U,
                                              .max_connection_inboxes = 0U})
            .has_value());
    ASSERT_TRUE(server->set_inbox_limits({.per_connection_datagrams = 8U,
                                             .max_queued_datagrams = 64U,
                                             .max_connection_inboxes = 16U})
            .has_value());
}

TEST(application_key_update_requires_authenticated_1rtt_secrets)
{
    auto context = cnetmod::ssl_context::quic_client();
    ASSERT_TRUE(context.has_value());
    auto session = quic_tls_session::client(*context);
    ASSERT_TRUE(session.has_value());

    // A peer must never be able to make an endpoint manufacture a key phase
    // before TLS has installed authenticated application traffic secrets.
    ASSERT_TRUE((*session)->application_read_key_candidates().empty());
    ASSERT_FALSE((*session)->initiate_key_update().has_value());
    ASSERT_FALSE((*session)->application_write_key_phase());
    ASSERT_FALSE((*session)->application_read_key_phase());
}

TEST(multipath_transport_parameter_roundtrip_is_capability_only)
{
    auto context = cnetmod::ssl_context::quic_client();
    ASSERT_TRUE(context.has_value());

    transport_params advertised{};
    advertised.initial_max_path_id = 7U;
    auto sender = quic_tls_session::client(*context, advertised);
    auto receiver = quic_tls_session::client(*context);
    ASSERT_TRUE(sender.has_value());
    ASSERT_TRUE(receiver.has_value());

    const auto encoded = (*sender)->encode_transport_params();
    ASSERT_TRUE(encoded.has_value());
    ASSERT_TRUE((*receiver)->decode_transport_params(*encoded).has_value());
    ASSERT_TRUE((*receiver)->received_transport_params().initial_max_path_id.has_value());
    ASSERT_EQ(*(*receiver)->received_transport_params().initial_max_path_id, 7U);
}

int main()
{
    return cnetmod::test::run_all();
}
