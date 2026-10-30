module cnetmod.application.mongodb;
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import std;
import cnetmod.executor.pool;
    #include "mongodb_cluster_runtime.hpp"

namespace cnetmod::application {
mongodb_operation_guard::mongodb_operation_guard(std::shared_ptr<mongodb_cluster_runtime> value)
    : runtime_(std::move(value))
{
    ++runtime_->active_operations;
}

mongodb_operation_guard::~mongodb_operation_guard()
{
    --runtime_->active_operations;
}

mongodb_cluster_runtime::mongodb_cluster_runtime(io_context& owner, mongodb_service_options configuration)
    : io(owner), options(std::move(configuration)) {}

mongodb_service::mongodb_service(mongodb_service_options options, io_context& io,
    std::string instance, service_requirement requirement, recovery_policy recovery)
    : cluster_(std::make_shared<mongodb_cluster_runtime>(io, std::move(options))),
      instance_(std::move(instance)),
      requirement_(requirement),
      recovery_(recovery) {}

mongodb_service::~mongodb_service() = default;

auto mongodb_service::topology_pool() noexcept -> mongodb::topology_connection_pool*
{
    return cluster_ ? cluster_->pool.get() : nullptr;
}

auto mongodb_service::acquire() -> task<mongodb::result<mongodb::pooled_connection>>
{
    if (!started_ || cleanup_pending_)
        co_return std::unexpected(mongodb::make_error(mongodb::error_code::connection_closed, "MongoDB application service is not running"));
    if (cluster_)
    {
        mongodb_operation_guard active{cluster_};
        auto pool = cluster_->pool;
        co_return co_await pool->acquire(cluster_->selection);
    }
    auto pool = pool_;
    co_return co_await pool->acquire();
}

auto mongodb_service::command(mongodb::bson_document document)
    -> task<mongodb::result<mongodb::bson_document>>
{
    const auto database = cluster_ ? cluster_->database : options_.connection.database;
    co_return co_await command(database, std::move(document));
}

auto mongodb_service::command(std::string_view database, mongodb::bson_document document)
    -> task<mongodb::result<mongodb::bson_document>>
{
    if (!started_ || cleanup_pending_)
        co_return std::unexpected(mongodb::make_error(mongodb::error_code::connection_closed, "MongoDB application service is not running"));
    if (cluster_)
    {
        mongodb_operation_guard active{cluster_};
        auto pool = cluster_->pool;
        co_return co_await pool->command(database, std::move(document), cluster_->selection);
    }
    auto acquired = co_await acquire();
    if (!acquired)
        co_return std::unexpected(acquired.error());
    co_return co_await (*acquired)->command(database, std::move(document));
}
} // namespace cnetmod::application
#endif
