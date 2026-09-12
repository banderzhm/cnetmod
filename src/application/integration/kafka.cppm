module;

#include <cnetmod/config.hpp>

/// Application-managed Kafka client facade.
export module cnetmod.application.kafka;

#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
import std;
import cnetmod.application.http;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.kafka;

namespace cnetmod::application {

export class kafka_service final
{
public:
    kafka_service(io_context& context, kafka::client_options options);

    [[nodiscard]] auto client() noexcept -> kafka::client_facade&;
    [[nodiscard]] auto start() -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto stop() -> task<std::expected<void, std::error_code>>;

private:
    kafka::client_facade client_;
    bool started_ = false;
};

/// Connect Kafka during startup and close it during application shutdown.
export auto install_kafka(http_application& application,
    kafka::client_options options = {}) -> kafka_service&;

} // namespace cnetmod::application
#endif
