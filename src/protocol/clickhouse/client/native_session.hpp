#pragma once
#include "../wire/server_packet.hpp"

// Private session state: transport ownership and operation lifetime guard.
namespace cnetmod::clickhouse {
struct client::implementation
{
    io_context& context;
    socket transport;
    connection_options options;
    server_info server;
    std::uint64_t revision{54401};
    bool connected{}, busy{}, compression_active{}, querying{}, cancelling{}, cancel_requested{};
    std::vector<std::byte> incoming;
    deadline expires;
#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> tls_context;
    std::unique_ptr<ssl_stream> tls_stream;
#endif
    explicit implementation(io_context&);
    void close() noexcept;

    struct operation
    {
        implementation& owner;
        bool complete{};

        inline explicit operation(implementation& state) : owner(state)
        {
            if (owner.busy || owner.cancelling)
                detail::fail("another ClickHouse operation is in progress", error_kind::busy);
            owner.busy = true;
            owner.expires = deadline::after(owner.options.operation_timeout);
        }

        inline ~operation()
        {
            if (!complete)
                owner.close();
            owner.busy = false;
            owner.querying = false;
        }
    };

    void check_cancel(cancel_token&);
    auto write(std::span<const std::byte>, cancel_token&) -> task<void>;
    auto receive(cancel_token&) -> task<detail::packet>;
    auto send_query(std::string_view, const query_options&, cancel_token&) -> task<void>;
    auto collect(cancel_token&, const block_sink& = {}) -> task<query_result>;
};
} // namespace cnetmod::clickhouse
