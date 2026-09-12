module;

#include <cnetmod/config.hpp>

/// Application-managed AMQP 0-9-1 client.
export module cnetmod.application.amqp091;

#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
import std;
import cnetmod.application.http;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.amqp091;

namespace cnetmod::application {

export class amqp091_service final
{
public:
    amqp091_service(io_context& context,
        amqp091::connection_options options);

    [[nodiscard]] auto client() noexcept -> amqp091::amqp091_client&;
    [[nodiscard]] auto start() -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto stop() -> task<std::expected<void, std::error_code>>;

private:
    auto run() -> task<void>;

    io_context& context_;
    amqp091::connection_options options_;
    amqp091::amqp091_client client_;
    cancel_token run_cancel_;
    bool started_ = false;
};

/// Connect AMQP 0-9-1 and supervise its frame pump for the application.
export auto install_amqp091(http_application& application,
    amqp091::connection_options options = {}) -> amqp091_service&;

} // namespace cnetmod::application
#endif
