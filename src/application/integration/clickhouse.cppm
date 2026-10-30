module;
#include <cnetmod/config.hpp>
export module cnetmod.application.clickhouse;
#ifdef CNETMOD_HAS_PROTOCOL_CLICKHOUSE
import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.application.recovery_policy;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.clickhouse;

export namespace cnetmod::application {
class clickhouse_service final : public managed_service
{
public:
    clickhouse_service(io_context&, clickhouse::connection_pool_options,
        std::string instance, service_requirement, recovery_policy);
    clickhouse_service(std::span<io_context* const>, clickhouse::connection_pool_options,
        std::string instance, service_requirement, recovery_policy);
    ~clickhouse_service() override;
    [[nodiscard]] auto pool() -> clickhouse::connection_pool&;
    [[nodiscard]] auto key() const -> service_key override;
    [[nodiscard]] auto requirement() const noexcept -> service_requirement override;
    [[nodiscard]] auto recovery() const noexcept -> recovery_policy override;
    [[nodiscard]] auto cleanup_required() const noexcept -> bool override;
    auto start(service_context&) -> task<std::expected<void, std::error_code>> override;
    auto stop(service_context&) -> task<std::expected<void, std::error_code>> override;
    auto probe(service_context&) -> task<health_report> override;

private:
    struct pool_shard;
    std::vector<std::unique_ptr<pool_shard>> pools_;
    std::string instance_;
    service_requirement requirement_;
    recovery_policy recovery_;
    bool started_{}, cleanup_pending_{}, rebuild_{};
};

[[nodiscard]] auto auto_configure_clickhouse(const configured_service&, auto_configuration_context&)
    -> std::expected<void, std::error_code>;
} // namespace cnetmod::application
#endif
