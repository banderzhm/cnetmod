module;
#include <cnetmod/config.hpp>
module cnetmod.observability.kafka_consumer;

#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
import std;
import cnetmod.observability.messaging;

namespace cnetmod::observability {
auto start_kafka_processing(const kafka::consumed_record& record,
    const instrumentation::span_exporter& sink) noexcept -> instrumentation::operation_scope
{
    return instrumentation::operation_scope::start(sink, [&]
        {
            const auto parent = messaging::extract(record);
            auto span = instrumentation::start_client_span(
                parent ? *parent : instrumentation::trace_context{}, "kafka process",
                {{"messaging.system", "kafka"}, {"messaging.operation.type", "process"}});
            span.kind = instrumentation::span_kind::consumer;
            return span;
        });
}
} // namespace cnetmod::observability
#endif
