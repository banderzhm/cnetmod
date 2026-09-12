module;

#include <cnetmod/config.hpp>

/// Application-managed AMQP 1.0 client.
export module cnetmod.application.amqp10;

#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
import std;
import cnetmod.application.http;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.amqp10;

namespace cnetmod::application {

export class amqp10_service final
{
public:
    amqp10_service(io_context& context, amqp10::client_options options);

    [[nodiscard]] auto client() noexcept -> amqp10::client&;
    [[nodiscard]] auto start() -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto stop() -> task<std::expected<void, std::error_code>>;

private:
    amqp10::client_options options_;
    amqp10::client client_;
    cancel_token operation_cancel_;
    bool started_ = false;
};

/// Connect AMQP 1.0 during startup and close it during shutdown.
export auto install_amqp10(http_application& application,
    amqp10::client_options options = {}) -> amqp10_service&;

} // namespace cnetmod::application
#endif
