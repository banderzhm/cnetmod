module;

#include <cnetmod/config.hpp>

/// Managed MongoDB connection pool and maintenance task.
export module cnetmod.application.mongodb;

#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.application.recovery_policy;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.mongodb;

namespace cnetmod::application {

/** URI-mode options. Callbacks execute on the service owner loop. */
export struct mongodb_service_options
{
    std::string uri;
    mongodb::srv_lookup_callback srv_lookup;
    /// Applied after URI/TXT resolution, before default cloud providers are installed.
    std::function<mongodb::result<void>(mongodb::connection_options&)> configure_connection;
    std::optional<std::size_t> minimum_size;
    std::optional<std::size_t> maximum_size;
    std::chrono::milliseconds heartbeat_frequency{10000};
};

class mongodb_cluster_runtime;

export class mongodb_service final : public managed_service
{
public:
    mongodb_service(mongodb_service_options options, io_context& io,
        std::string instance, service_requirement requirement, recovery_policy recovery);
    ~mongodb_service() override;
    mongodb_service(io_context& io, mongodb::connection_pool_options options,
        std::string instance, service_requirement requirement,
        recovery_policy recovery);
    /**
     * Returns the pool for the current service run. Do not retain this
     * reference across stop and restart; a successful restart replaces it.
     */
    /// Legacy single-server pool. Throws logic_error for a URI-mode service.
    [[nodiscard]] auto pool() -> mongodb::connection_pool&;
    /// Available after URI startup; invalidated by restart, like pool().
    [[nodiscard]] auto topology_pool() noexcept -> mongodb::topology_connection_pool*;
    auto acquire() -> task<mongodb::result<mongodb::pooled_connection>>;
    auto command(mongodb::bson_document document)
        -> task<mongodb::result<mongodb::bson_document>>;
    auto command(std::string_view database, mongodb::bson_document document)
        -> task<mongodb::result<mongodb::bson_document>>;
    [[nodiscard]] auto key() const -> service_key override;
    [[nodiscard]] auto requirement() const noexcept
        -> service_requirement override;
    [[nodiscard]] auto recovery() const noexcept -> recovery_policy override;
    [[nodiscard]] auto cleanup_required() const noexcept -> bool override;
    auto start(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto stop(service_context& context)
        -> task<std::expected<void, std::error_code>> override;
    auto probe(service_context& context) -> task<health_report> override;

private:
    auto start_runtime(service_context& context)
        -> task<std::expected<void, std::error_code>>;
    auto start_cluster(service_context& context) -> task<std::expected<void, std::error_code>>;
    auto stop_cluster(service_context& context) -> task<std::expected<void, std::error_code>>;
    auto probe_cluster(service_context& context) -> task<health_report>;
    std::shared_ptr<mongodb_cluster_runtime> cluster_;
    mongodb::connection_pool_options options_;
    std::shared_ptr<mongodb::connection_pool> pool_;
    std::stop_source maintenance_stop_;
    std::string instance_;
    service_requirement requirement_;
    recovery_policy recovery_;
    bool started_ = false;
    bool cleanup_pending_ = false;
    bool rebuild_pool_ = false;
};

export [[nodiscard]] auto auto_configure_mongodb(
    const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>;

} // namespace cnetmod::application
#endif
