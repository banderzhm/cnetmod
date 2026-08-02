module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp091:amqp091_client;
import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import :protocol_constants;
import :connection_options;
import :protocol_connection;
import :logical_channel;

export namespace cnetmod::amqp091 {
/// Facade over one AMQP connection and its logical channels.
class amqp091_client final
{
public:
    explicit amqp091_client(io_context& context);
    ~amqp091_client();
    amqp091_client(const amqp091_client&) = delete;
    auto operator=(const amqp091_client&) -> amqp091_client& = delete;
    auto async_connect(connection_options options) -> task<result<void>>;
    auto async_connect(connection_options options, cancel_token& token)
        -> task<result<void>>;
    auto async_open_channel() -> task<result<std::shared_ptr<logical_channel>>>;
    auto async_run(cancel_token& token) -> task<result<void>>;
    auto async_recover(cancel_token& token) -> task<result<void>>;
    auto async_close() -> task<result<void>>;
    [[nodiscard]] auto state() const noexcept -> connection_state;
    [[nodiscard]] auto connection() const noexcept
        -> std::shared_ptr<protocol_connection>;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::amqp091
