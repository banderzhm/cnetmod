module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.broker_connection;
import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.protocol.kafka.protocol_constants;
import cnetmod.protocol.kafka.client_options;
import cnetmod.protocol.kafka.request_header;
import cnetmod.protocol.kafka.protocol_value_codec;

export namespace cnetmod::kafka {
class connection_observer
{
public:
    virtual ~connection_observer() = default;

    virtual void on_connected(const broker_endpoint&) {}

    virtual void on_disconnected(const broker_endpoint&, const error&) {}

    virtual void on_throttle(const broker_endpoint&, std::chrono::milliseconds)
    {
    }
};

class broker_connection
{
public:
    broker_connection(io_context&, broker_endpoint, client_options);
    ~broker_connection();
    broker_connection(broker_connection&&) noexcept;
    auto operator=(broker_connection&&) noexcept -> broker_connection&;
    broker_connection(const broker_connection&) = delete;
    auto operator=(const broker_connection&) -> broker_connection& = delete;
    auto connect() -> task<result<void>>;
    auto connect(cancel_token&) -> task<result<void>>;
    auto request(protocol::api_key, std::int16_t, std::span<const std::byte>)
        -> task<result<bytes>>;
    auto request(protocol::api_key, std::int16_t, std::span<const std::byte>,
        cancel_token&) -> task<result<bytes>>;
    auto send(protocol::api_key, std::int16_t, std::span<const std::byte>)
        -> task<result<void>>;
    auto send(protocol::api_key, std::int16_t, std::span<const std::byte>,
        cancel_token&) -> task<result<void>>;
    void close() noexcept;
    [[nodiscard]] auto is_open() const noexcept -> bool;
    [[nodiscard]] auto endpoint() const noexcept -> const broker_endpoint&;
    void add_observer(std::weak_ptr<connection_observer>);

private:
    class impl;
    std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::kafka
