module cnetmod.instrumentation.operation_scope;

import std;
import cnetmod.instrumentation.tracing;
import cnetmod.instrumentation.operation_result;

namespace cnetmod::instrumentation {

operation_scope::~operation_scope()
{
    complete({operation_status::abandoned, {}});
}

operation_scope::operation_scope(operation_scope&& other) noexcept
    : span_(std::move(other.span_)), sink_(std::move(other.sink_))
{
    other.span_.reset();
}

auto operation_scope::operator=(operation_scope&& other) noexcept
    -> operation_scope&
{
    if (this != &other)
    {
        complete({operation_status::abandoned, {}});
        span_ = std::move(other.span_);
        sink_ = std::move(other.sink_);
        other.span_.reset();
    }
    return *this;
}

auto operation_scope::context() const noexcept -> const trace_context*
{
    return span_ ? &span_->context : nullptr;
}

void operation_scope::complete(operation_result result) noexcept
{
    if (!span_)
        return;
    auto active = std::move(*span_);
    span_.reset();
    auto sink = std::move(sink_);
    if ((active.context.flags & 1U) == 0)
        return;
    try
    {
        auto completed = finish_client_span(std::move(active),
            result.status == operation_status::error ||
                result.status == operation_status::timeout ||
                result.status == operation_status::abandoned);
        completed.result = result;
        sink(completed);
    }
    catch (...)
    {
        // Observability must not replace the result of the observed operation.
    }
}

} // namespace cnetmod::instrumentation
