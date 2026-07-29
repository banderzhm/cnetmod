module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.kafka.client_facade;
import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.protocol.kafka.protocol_constants;
import cnetmod.protocol.kafka.client_options;
import cnetmod.protocol.kafka.request_header;
import cnetmod.protocol.kafka.broker_request_codec;
import cnetmod.protocol.kafka.broker_connection;
import cnetmod.protocol.kafka.broker_metadata;
import cnetmod.protocol.kafka.kafka_producer;
import cnetmod.protocol.kafka.partitioner;
import cnetmod.protocol.kafka.kafka_consumer;

export namespace cnetmod::kafka {
class client_facade
{
public:
    client_facade(io_context&, client_options);
    ~client_facade();
    client_facade(client_facade&&) noexcept;
    auto operator=(client_facade&&) noexcept -> client_facade&;
    auto connect(cancel_token* = nullptr) -> task<result<void>>;
    auto refresh_metadata(std::vector<std::string> = {},
        cancel_token* = nullptr) -> task<result<void>>;
    [[nodiscard]] auto metadata() const -> std::shared_ptr<metadata_cache>;
    [[nodiscard]] auto api_versions() const
        -> std::span<const protocol::api_version>;
    auto make_producer(producer_options = {}, std::unique_ptr<partitioner> = {})
        -> result<producer>;
    auto make_consumer(consumer_options) -> result<consumer>;
    void add_connection_observer(std::weak_ptr<connection_observer>);
    void close() noexcept;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::kafka
