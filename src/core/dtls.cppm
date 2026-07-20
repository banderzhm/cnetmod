module;

#include <cnetmod/config.hpp>

export module cnetmod.core.dtls;

import std;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.coro.task;

namespace cnetmod {

#ifdef CNETMOD_HAS_SSL

export enum class dtls_role {
    client,
    server,
};

export struct dtls_datagram_options {
    std::size_t mtu = 1400;
    std::size_t recv_buffer_size = 65536;
};

export class dtls_datagram_session {
public:
    using receive_handler =
        std::function<task<std::expected<std::vector<std::byte>, std::error_code>>()>;

    dtls_datagram_session(ssl_context& ssl_ctx,
                          io_context& io_ctx,
                          socket& sock,
                          endpoint peer,
                          dtls_role role,
                          dtls_datagram_options options = {});
    ~dtls_datagram_session();

    dtls_datagram_session(const dtls_datagram_session&) = delete;
    auto operator=(const dtls_datagram_session&) -> dtls_datagram_session& = delete;

    dtls_datagram_session(dtls_datagram_session&&) noexcept;
    auto operator=(dtls_datagram_session&&) noexcept -> dtls_datagram_session&;

    void set_hostname(std::string_view hostname);
    void queue_datagram(const_buffer datagram);
    void set_receive_handler(receive_handler handler);

    [[nodiscard]] auto peer() const noexcept -> const endpoint&;
    [[nodiscard]] auto native() const noexcept -> void*;

    auto async_handshake() -> task<std::expected<void, std::error_code>>;
    auto async_read(mutable_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>;
    auto async_write(const_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>;
    auto async_shutdown() -> task<std::expected<void, std::error_code>>;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

#endif // CNETMOD_HAS_SSL

} // namespace cnetmod
