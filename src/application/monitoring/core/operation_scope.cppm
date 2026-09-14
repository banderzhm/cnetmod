/**
 * @brief Move-only ownership of an observed asynchronous operation.
 */
export module cnetmod.instrumentation.operation_scope;

import std;
export import cnetmod.instrumentation.tracing;
import cnetmod.instrumentation.operation_result;

namespace cnetmod::instrumentation {

/**
 * @brief Completes an operation once and isolates instrumentation failures.
 *
 * The scope belongs to the coroutine frame, never to a thread. Exporters are
 * synchronous nonblocking sinks; asynchronous delivery belongs to the exporter.
 * Destruction reports abandonment only when no explicit outcome was recorded.
 */
export class operation_scope
{
public:
    operation_scope() noexcept = default;
    ~operation_scope();
    operation_scope(const operation_scope&) = delete;
    auto operator=(const operation_scope&) -> operation_scope& = delete;
    operation_scope(operation_scope&& other) noexcept;
    auto operator=(operation_scope&& other) noexcept -> operation_scope&;

    /**
     * @brief Evaluates metadata only for an installed sink, containing failures.
     * @param sink Empty means disabled; the factory is never invoked.
     * @param factory Produces an active span including lazy attributes.
     */
    template <typename Factory>
    [[nodiscard]] static auto start(const span_exporter& sink, Factory&& factory) noexcept -> operation_scope
    {
        if (!sink)
            return {};
        try
        {
            operation_scope scope;
            scope.sink_ = sink;
            scope.span_.emplace(std::invoke(std::forward<Factory>(factory)));
            scope.sink_.sample(scope.span_->context, !scope.span_->parent_span_id.empty());
            return scope;
        }
        catch (...)
        {
            return {};
        }
    }

    /**
     * @brief Returns the current identity or null when observation is disabled.
     */
    [[nodiscard]] auto context() const noexcept -> const trace_context*;

    /**
     * @brief Ends the observation exactly once, preserving its terminal outcome.
     */
    void complete(operation_result result = {}) noexcept;

    /**
     * @brief Appends lazily constructed attributes while containing allocation errors.
     */
    template <typename Factory>
    void annotate(Factory&& factory) noexcept
    {
        if (!span_ || (span_->context.flags & 1U) == 0)
            return;
        try
        {
            auto attributes = std::invoke(std::forward<Factory>(factory));
            span_->attributes.insert(span_->attributes.end(),
                std::make_move_iterator(attributes.begin()),
                std::make_move_iterator(attributes.end()));
        }
        catch (...)
        {
        }
    }

private:
    std::optional<active_span> span_;
    span_exporter sink_;
};

} // namespace cnetmod::instrumentation
