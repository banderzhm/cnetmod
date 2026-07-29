module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp10:socket_transport;
import std;
import :client_configuration;
import :client_error;
import :reconnect_policy;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import :transport_frame_codec;

export namespace cnetmod::amqp10 {
class socket_transport
{
public:
    explicit socket_transport(io_context&);
    ~socket_transport();
    socket_transport(socket_transport&&) noexcept;
    auto operator=(socket_transport&&) noexcept -> socket_transport&;
    socket_transport(const socket_transport&) = delete;
    auto operator=(const socket_transport&) -> socket_transport& = delete;
    auto connect(const endpoint&, cancel_token&)
        -> task<std::expected<void, error>>;
    auto write_header(protocol_header, cancel_token&)
        -> task<std::expected<void, error>>;
    auto read_header(cancel_token&)
        -> task<std::expected<protocol_header, error>>;
    auto write_frame(const frame&, cancel_token&)
        -> task<std::expected<void, error>>;
    auto read_frame(std::uint32_t maximum_size, cancel_token&)
        -> task<std::expected<frame, error>>;
    auto shutdown() -> task<void>;
    void close() noexcept;
    [[nodiscard]] auto is_open() const noexcept -> bool;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::amqp10
