module;

#include <cnetmod/config.hpp>

/// Application-managed MySQL connection pool.
export module cnetmod.application.mysql;

#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
import std;
import cnetmod.application.http;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.mysql;

namespace cnetmod::application {

export class mysql_service final
{
public:
    mysql_service(io_context& context, mysql::pool_params options);

    [[nodiscard]] auto pool() noexcept -> mysql::connection_pool&;
    [[nodiscard]] auto start() -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto stop() -> task<std::expected<void, std::error_code>>;

private:
    io_context& context_;
    mysql::connection_pool pool_;
    bool started_ = false;
};

/// Register one MySQL pool and bind its lifetime to the application.
export auto install_mysql(http_application& application,
    mysql::pool_params options = {}) -> mysql_service&;

} // namespace cnetmod::application
#endif
