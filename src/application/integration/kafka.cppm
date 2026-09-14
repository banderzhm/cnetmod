module;
#include <cnetmod/config.hpp>
/// Managed Kafka client.
export module cnetmod.application.kafka;
#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
import std;
import cnetmod.application.auto_configuration;
import cnetmod.application.configuration;
import cnetmod.application.managed_service;
import cnetmod.application.recovery_policy;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.kafka;
import cnetmod.instrumentation.tracing;
export import cnetmod.observability.kafka_producer;

namespace cnetmod::application {
export class kafka_service final : public managed_service
{
public:
    kafka_service(io_context&, kafka::client_options, std::string,
        service_requirement, recovery_policy, instrumentation::span_exporter = {});
    /**
     * @brief Creates a producer bound to the application's configured trace sink.
     * @details The client service must outlive the producer and all its operations.
     */
    [[nodiscard]] auto make_producer(kafka::producer_options options = {},
        std::unique_ptr<kafka::partitioner> partitioner = {})
        -> kafka::result<observability::instrumented_kafka_producer>;
    [[nodiscard]] auto client() noexcept -> kafka::client_facade&;
    [[nodiscard]] auto key() const -> service_key override;
    [[nodiscard]] auto requirement() const noexcept -> service_requirement override;
    [[nodiscard]] auto recovery() const noexcept -> recovery_policy override;
    auto start(service_context&) -> task<std::expected<void, std::error_code>> override;
    auto stop(service_context&) -> task<std::expected<void, std::error_code>> override;
    auto probe(service_context&) -> task<health_report> override;

private:
    kafka::client_facade client_;
    std::string instance_;
    service_requirement requirement_;
    recovery_policy recovery_;
    instrumentation::span_exporter sink_;
    bool started_ = false;
};

export [[nodiscard]] auto auto_configure_kafka(const configured_service&,
    auto_configuration_context&) -> std::expected<void, std::error_code>;
} // namespace cnetmod::application
#endif
