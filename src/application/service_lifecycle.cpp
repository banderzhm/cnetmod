module cnetmod.application.service_lifecycle;

import std;
import cnetmod.instrumentation.error;
import cnetmod.instrumentation.operation_scope;
import cnetmod.instrumentation.operation_result;
import cnetmod.instrumentation.tracing;
import cnetmod.application.managed_service;
import cnetmod.application.task_supervisor;
import cnetmod.core.log;
import cnetmod.coro.cancel;
import cnetmod.coro.task_group;
import cnetmod.coro.timer;
import cnetmod.observability.otlp;
import cnetmod.protocol.http.middleware.tracing;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::application {
namespace {

    /**
     * @brief Forwards parent stop to one attempt without sharing timeout state.
     *
     * Both tokens outlive this registration. The attempt must settle before
     * destruction; parent completion synchronizes with an in-flight notifier.
     */
    class recovery_cancellation_link final
    {
    public:
        recovery_cancellation_link(cancel_token& parent, cancel_token& child) noexcept
            : parent_(parent), child_(child)
        {
            if (!parent_.register_callback(this, [](void* value) noexcept
                    {
                        static_cast<recovery_cancellation_link*>(value)->child_.cancel();
                    }))
                child_.cancel();
        }

        ~recovery_cancellation_link()
        {
            (void)parent_.complete_callback(this);
            parent_.finish_callback(this);
        }

        recovery_cancellation_link(const recovery_cancellation_link&) = delete;
        auto operator=(const recovery_cancellation_link&) -> recovery_cancellation_link& = delete;

    private:
        cancel_token& parent_;
        cancel_token& child_;
    };

    auto begin_operation(observability::telemetry_hub& telemetry,
        const service_key& key, std::string_view operation)
        -> instrumentation::operation_scope
    {
        auto scope = instrumentation::operation_scope::start(telemetry.spans(), [&]
            {
                auto span = instrumentation::start_client_span({},
                    std::format("application.service.{}", operation));
                span.kind = instrumentation::span_kind::internal;
                return span;
            });
        scope.annotate([&]
            {
                return std::vector<std::pair<std::string, std::string>>{
                    {"service.name", key.name}, {"service.instance", key.instance}};
            });
        return scope;
    }

    void record_operation(observability::telemetry_hub& telemetry,
        instrumentation::operation_scope scope, const service_key& key,
        std::string_view operation, std::error_code error) noexcept
    {
        const bool failed = static_cast<bool>(error);
        const auto attributes = [&]
        {
            return std::vector<std::pair<std::string, std::string>>{
                {"service.name", key.name}, {"service.instance", key.instance},
                {"operation", std::string{operation}},
                {"outcome", failed ? "failure" : "success"}};
        };
        (void)telemetry.submit_metric_lazy([&]
            {
                return observability::otel_metric_record{
                    .name = "application.service.operations",
                    .value = 1.0,
                    .kind = observability::otel_metric_kind::counter,
                    .unit = "{operation}",
                    .attributes = attributes()};
            });
        (void)telemetry.submit_log_lazy([&]
            {
                const auto* trace = scope.context();
                return observability::otel_log_record{
                    .severity = failed ? "ERROR" : "INFO",
                    .body = std::format("service {} {} {}", key.canonical_name(),
                        operation, failed ? "failed" : "completed"),
                    .trace_id = trace ? trace->trace_id : std::string{},
                    .span_id = trace ? trace->span_id : std::string{},
                    .attributes = attributes()};
            });
        scope.complete(instrumentation::classify_error(error));
    }

} // namespace

service_lifecycle::service_lifecycle(io_context& io,
    observability::telemetry_hub& telemetry, service_registry& services,
    task_supervisor& supervisor, health_registry& health,
    lifecycle_policy policy)
    : io_(io), telemetry_(telemetry), services_(services), supervisor_(supervisor), health_(health), policy_(policy)
{
}

auto service_lifecycle::start(std::chrono::milliseconds rollback_reserve)
    -> task<std::expected<void, std::error_code>>
{
    if (rollback_reserve < std::chrono::milliseconds::zero() || rollback_reserve > policy_.total_stop_timeout)
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    std::expected<std::vector<std::vector<service_key>>, std::error_code> graph;
    {
        concurrent_containers::exclusive_latch_guard lock{started_latch_};
        rollback_deadline_ = {};
    }
    try
    {
        graph = services_.validate_dependencies();
        if (!graph)
            co_return std::unexpected(graph.error());
        concurrent_containers::exclusive_latch_guard lock{started_latch_};
        auto prepared_layers = *graph;
        auto prepared_states = service_started_;
        for (const auto& layer : *graph)
            for (const auto& key : layer)
                prepared_states.try_emplace(key, false);
        started_layers_.swap(prepared_layers);
        service_started_.swap(prepared_states);
    }
    catch (const std::bad_alloc&)
    {
        co_return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
    catch (const std::system_error& error)
    {
        co_return std::unexpected(error.code());
    }
    catch (...)
    {
        co_return std::unexpected(std::make_error_code(std::errc::io_error));
    }

    const auto total_deadline = deadline::after(policy_.total_start_timeout);
    for (const auto& layer : *graph)
    {
        if (total_deadline.expired())
        {
            (void)co_await rollback(rollback_reserve);
            co_return std::unexpected(
                std::make_error_code(std::errc::timed_out));
        }

        auto layer_deadline = total_deadline.constrain(
            deadline::after(policy_.service_start_timeout));
        std::optional<task_group> group;
        std::expected<void, std::error_code> result;
        try
        {
            /**
             * The group owns the application startup budget. Each child owns
             * its service timeout so an optional timeout can be degraded
             * without poisoning the entire dependency layer.
             */
            group.emplace(io_, total_deadline);
            for (const auto& key : layer)
            {
                auto service = services_.managed(key);
                const auto dispatched = group->run([this, service, key, layer_deadline, total_deadline](cancel_token& token)
                                                       -> task<std::expected<void, std::error_code>>
                    {
                        auto span = begin_operation(telemetry_, key, "start");
                        service_context context{io_, telemetry_, supervisor_, token,
                            layer_deadline};
                        std::expected<void, std::error_code> result;
                        try
                        {
                            /**
                             * Preserve ownership before deadline normalization.
                             * A late successful start still acquired resources
                             * that rollback or final shutdown must release.
                             */
                            auto acquire = [&]() -> task<std::expected<void, std::error_code>>
                            {
                                auto acquired = co_await service->start(context);
                                if (acquired)
                                    mark_started(key);
                                co_return acquired;
                            };
                            result = co_await with_deadline(io_, layer_deadline,
                                acquire(), token);
                        }
                        catch (const std::system_error& error)
                        {
                            result = std::unexpected(error.code());
                        }
                        catch (const std::bad_alloc&)
                        {
                            result = std::unexpected(std::make_error_code(std::errc::not_enough_memory));
                        }
                        catch (...)
                        {
                            result = std::unexpected(std::make_error_code(std::errc::io_error));
                        }
                        if (service->shutdown_required())
                            mark_started(key);
                        record_operation(telemetry_, std::move(span), key,
                            "start", result ? std::error_code{} : result.error());
                        if (telemetry_.records_metrics())
                            telemetry_.increment_local_counter(
                                result ? "application_service_starts_total"
                                       : "application_service_start_failures_total");
                        if (result)
                        {
                            health_.update(key, {
                                                    .status = service_health::up,
                                                    .message = "started",
                                                });
                            co_return {};
                        }

                        health_.update(key, {
                                                .status = service_health::down,
                                                .message = "startup failed",
                                                .error = result.error(),
                                            });
                        const bool total_budget_cancelled =
                            layer_deadline.at_time() == total_deadline.at_time() &&
                            token.reason() == cancellation_reason::deadline_exceeded;
                        if (service->requirement() == service_requirement::optional &&
                            !total_budget_cancelled)
                        {
                            (void)schedule_recovery(key);
                            co_return {};
                        }
                        {
                            concurrent_containers::exclusive_latch_guard lock{
                                started_latch_};
                            last_failure_ = lifecycle_failure{key,
                                lifecycle_phase::startup, result.error()};
                        }
                        co_return std::unexpected(result.error());
                    });
                if (!dispatched)
                {
                    result = std::unexpected(std::make_error_code(std::errc::operation_canceled));
                    break;
                }
            }
            if (result)
                result = co_await group->join();
            if (result && total_deadline.expired())
                result = std::unexpected(std::make_error_code(std::errc::timed_out));
        }
        catch (const std::bad_alloc&)
        {
            result = std::unexpected(std::make_error_code(std::errc::not_enough_memory));
        }
        catch (const std::system_error& error)
        {
            result = std::unexpected(error.code());
        }
        catch (...)
        {
            result = std::unexpected(std::make_error_code(std::errc::io_error));
        }
        if (!result)
        {
            if (group)
            {
                group->cancel();
                co_await group->settle();
            }
            (void)co_await rollback(rollback_reserve);
            co_return std::unexpected(result.error());
        }
    }
    co_return {};
}

auto service_lifecycle::stop(deadline budget)
    -> task<std::expected<void, std::error_code>>
{
    std::optional<std::error_code> first_error;
    supervisor_.request_stop();
    const auto total_deadline = budget.constrain(deadline::after(policy_.total_stop_timeout));
    std::unordered_set<service_key, service_key_hash> started;
    {
        concurrent_containers::shared_latch_guard lock{started_latch_};
        for (const auto& [key, active] : service_started_)
            if (active)
                started.insert(key);
    }
    const auto graph = services_.validate_dependencies();
    const auto& layers = graph ? *graph : started_layers_;
    std::unordered_map<service_key, std::vector<service_key>, service_key_hash> dependencies;
    for (const auto& key : started)
        if (const auto service = services_.managed(key))
            dependencies.emplace(key, service->dependencies());
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer)
    {
        for (auto key = layer->rbegin(); key != layer->rend(); ++key)
        {
            if (!started.contains(*key))
                continue;
            const bool retained_dependent = std::ranges::any_of(started, [&](const auto& candidate)
                {
                    const auto found = dependencies.find(candidate);
                    return found != dependencies.end() &&
                        std::ranges::find(found->second, *key) != found->second.end();
                });
            if (retained_dependent)
            {
                health_.update(*key, {
                                         .status = service_health::stopping,
                                         .message = "waiting for dependent shutdown",
                                     });
                continue;
            }
            if (total_deadline.expired())
            {
                if (!first_error)
                    first_error = std::make_error_code(std::errc::timed_out);
                break;
            }
            const auto service = services_.managed(*key);
            if (!service)
                continue;
            health_.update(*key, {
                                     .status = service_health::stopping,
                                     .message = "stopping",
                                 });
            cancel_token token;
            service_context context{io_, telemetry_, supervisor_, token,
                total_deadline.constrain(
                    deadline::after(policy_.service_stop_timeout))};
            std::error_code stop_error;
            bool released = false;
            auto span = begin_operation(telemetry_, *key, "stop");
            try
            {
                /**
                 * Resource release and deadline compliance are independent.
                 * Retire a successful release before timeout normalization so
                 * cleanup retries never close that component a second time.
                 */
                auto release = [&]() -> task<std::expected<void, std::error_code>>
                {
                    auto result = co_await service->stop(context);
                    if (result)
                    {
                        concurrent_containers::exclusive_latch_guard lock{started_latch_};
                        service_started_.at(*key) = false;
                        started.erase(*key);
                        released = true;
                    }
                    co_return result;
                };
                auto result = co_await with_deadline(io_, context.operation_deadline,
                    release(), token);
                stop_error = result ? std::error_code{} : result.error();
            }
            catch (const std::system_error& error)
            {
                stop_error = error.code();
            }
            catch (const std::bad_alloc&)
            {
                stop_error = std::make_error_code(std::errc::not_enough_memory);
            }
            catch (...)
            {
                stop_error = std::make_error_code(std::errc::io_error);
            }
            if (stop_error && !first_error)
            {
                first_error = stop_error;
                concurrent_containers::exclusive_latch_guard lock{started_latch_};
                last_failure_ = lifecycle_failure{*key, lifecycle_phase::shutdown, stop_error};
            }
            record_operation(telemetry_, std::move(span), *key, "stop", stop_error);
            if (telemetry_.records_metrics())
                telemetry_.increment_local_counter(stop_error
                        ? "application_service_stop_failures_total"
                        : "application_service_stops_total");
            health_.update(*key, {
                                     .status = released ? service_health::stopped : service_health::stopping,
                                     .message = released ? "stopped" : "stop failed",
                                     .error = stop_error,
                                 });
        }
    }
    auto joined = co_await supervisor_.join();
    if (!joined && !first_error)
        first_error = joined.error();
    {
        concurrent_containers::exclusive_latch_guard lock{started_latch_};
        if (std::ranges::none_of(service_started_, [](const auto& entry)
                {
                    return entry.second;
                }))
            started_layers_.clear();
    }
    if (first_error)
        co_return std::unexpected(*first_error);
    co_return {};
}

auto service_lifecycle::schedule_recovery(const service_key& key)
    -> std::expected<void, std::error_code>
{
    const auto service = services_.managed(key);
    if (!service)
        return std::unexpected(
            std::make_error_code(std::errc::no_such_file_or_directory));
    const auto task_name = std::format("service-recovery:{}",
        key.canonical_name());
    const auto current = supervisor_.state(task_name);
    if (current && *current != supervised_task_state::failed &&
        *current != supervised_task_state::stopped)
        return {};
    auto recovery = service->recovery();
    deadline recovery_deadline;
    {
        concurrent_containers::exclusive_latch_guard lock{started_latch_};
        service_started_.try_emplace(key, false);
        if (const auto found = recovery_overrides_.find(key);
            found != recovery_overrides_.end())
            recovery = found->second;
        const auto [episode, inserted] = recovery_deadlines_.try_emplace(key, deadline::after(recovery.budget));
        if (!inserted && current && *current == supervised_task_state::failed)
        {
            if (service->requirement() == service_requirement::required)
                return {};
            episode->second = deadline::after(recovery.budget);
        }
        recovery_deadline = episode->second;
    }
    return supervisor_.supervise(task_name, [this, service, key, recovery_deadline](cancel_token& token) -> task<std::expected<void, std::error_code>>
        {
            auto span = begin_operation(telemetry_, key, "recover");
            cancel_token attempt_token;
            recovery_cancellation_link cancellation{token, attempt_token};
            service_context context{io_, telemetry_, supervisor_, attempt_token,
                recovery_deadline.constrain(deadline::after(policy_.service_start_timeout))};
            auto attempt = [&]() -> task<std::expected<void, std::error_code>>
            {
                if (recovery_deadline.expired())
                    co_return std::unexpected(std::make_error_code(std::errc::timed_out));
                /**
                 * Complete retained cleanup before attempting another start.
                 * Both operations consume the same recovery attempt budget.
                 */
                if (service->cleanup_required())
                {
                    auto cleaned = co_await service->stop(context);
                    if (!cleaned)
                        co_return cleaned;
                    concurrent_containers::exclusive_latch_guard lock{started_latch_};
                    service_started_.at(key) = false;
                }
                auto started = co_await service->start(context);
                if (!started)
                    co_return started;
                mark_started(key);
                const auto probe = co_await service->probe(context);
                if (probe.status != service_health::up)
                    co_return std::unexpected(probe.error ? probe.error
                                                          : std::make_error_code(std::errc::resource_unavailable_try_again));
                co_return {};
            };
            std::expected<void, std::error_code> result;
            try
            {
                result = co_await with_deadline(io_, context.operation_deadline,
                    attempt(), attempt_token);
            }
            catch (const std::system_error& error)
            {
                result = std::unexpected(error.code());
            }
            catch (const std::bad_alloc&)
            {
                result = std::unexpected(std::make_error_code(std::errc::not_enough_memory));
            }
            catch (...)
            {
                result = std::unexpected(std::make_error_code(std::errc::io_error));
            }
            if (service->shutdown_required())
                mark_started(key);
            record_operation(telemetry_, std::move(span), key,
                "recover", result ? std::error_code{} : result.error());
            if (telemetry_.records_metrics())
                telemetry_.increment_local_counter(
                    result ? "application_service_recoveries_total"
                           : "application_service_recovery_failures_total");
            if (result)
            {
                health_.update(key, {
                                        .status = service_health::starting,
                                        .message = "reconnected; awaiting health confirmation",
                                    });
            }
            else
            {
                concurrent_containers::exclusive_latch_guard lock{
                    started_latch_};
                last_failure_ = lifecycle_failure{key,
                    lifecycle_phase::health_recovery, result.error()};
            }
            co_return result;
        },
        recovery, service->requirement() == service_requirement::required, {}, recovery_deadline);
}

auto service_lifecycle::reconcile_health(const service_health_snapshot& snapshot)
    -> std::expected<void, std::error_code>
{
    if (snapshot.report.status == service_health::up)
    {
        const auto state = supervisor_.state(std::format("service-recovery:{}", snapshot.key.canonical_name()));
        if (state && *state != supervised_task_state::stopped &&
            *state != supervised_task_state::failed)
            return {};
    }
    bool expired = false;
    {
        concurrent_containers::exclusive_latch_guard lock{started_latch_};
        if (!health_.is_current(snapshot))
            return {};
        if (snapshot.report.status == service_health::up)
        {
            recovery_deadlines_.erase(snapshot.key);
            return {};
        }
        const auto episode = recovery_deadlines_.find(snapshot.key);
        expired = episode != recovery_deadlines_.end() && episode->second.expired();
    }
    if (snapshot.report.status == service_health::down ||
        (expired && (snapshot.report.status == service_health::starting || snapshot.report.status == service_health::degraded)))
        return schedule_recovery(snapshot.key);
    return {};
}

auto service_lifecycle::update_recovery_policies(
    std::span<const std::pair<service_key, recovery_policy>> updates)
    -> std::expected<void, std::error_code>
{
    try
    {
        for (const auto& [key, policy] : updates)
            if (!valid_recovery_policy(policy) || policy.budget.count() <= 0)
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        if (updates.empty())
            return {};
        concurrent_containers::exclusive_latch_guard lock{started_latch_};
        auto staged = recovery_overrides_;
        for (const auto& [key, policy] : updates)
            staged.insert_or_assign(key, policy);
        recovery_overrides_.swap(staged);
        return {};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
    catch (...)
    {
        return std::unexpected(std::make_error_code(std::errc::io_error));
    }
}

auto service_lifecycle::recovery_policy_override(const service_key& key) const
    -> std::optional<recovery_policy>
{
    concurrent_containers::exclusive_latch_guard lock{started_latch_};
    const auto found = recovery_overrides_.find(key);
    if (found == recovery_overrides_.end())
        return std::nullopt;
    return found->second;
}

void service_lifecycle::mark_started(const service_key& key)
{
    concurrent_containers::exclusive_latch_guard lock{started_latch_};
    service_started_.at(key) = true;
}

auto service_lifecycle::rollback(std::chrono::milliseconds reserve)
    -> task<std::expected<void, std::error_code>>
{
    const auto budget = deadline::after(policy_.total_stop_timeout);
    {
        concurrent_containers::exclusive_latch_guard lock{started_latch_};
        rollback_deadline_ = budget;
    }
    try
    {
        logger::warn("application startup failed; rolling back started services");
    }
    catch (...)
    {
        // Diagnostic failure must not prevent resource cleanup.
    }
    std::optional<lifecycle_failure> initiating_failure;
    {
        concurrent_containers::exclusive_latch_guard lock{started_latch_};
        initiating_failure = std::move(last_failure_);
        last_failure_.reset();
    }
    std::expected<void, std::error_code> result;
    try
    {
        const auto cleanup_budget = budget.constrain(deadline::after(
            std::max(deadline::duration::zero(), budget.remaining() - reserve)));
        result = co_await stop(cleanup_budget);
    }
    catch (const std::bad_alloc&)
    {
        result = std::unexpected(std::make_error_code(std::errc::not_enough_memory));
    }
    catch (const std::system_error& error)
    {
        result = std::unexpected(error.code());
    }
    catch (...)
    {
        result = std::unexpected(std::make_error_code(std::errc::io_error));
    }
    {
        concurrent_containers::exclusive_latch_guard lock{started_latch_};
        rollback_failure_.reset();
        rollback_error_ = result ? std::error_code{} : result.error();
        if (!result && last_failure_ && last_failure_->phase == lifecycle_phase::shutdown)
        {
            rollback_failure_ = std::move(last_failure_);
            rollback_failure_->phase = lifecycle_phase::rollback;
        }
        last_failure_ = std::move(initiating_failure);
    }
    co_return result;
}

auto service_lifecycle::last_rollback_error() const noexcept -> std::error_code
{
    concurrent_containers::shared_latch_guard lock{started_latch_};
    return rollback_error_;
}

auto service_lifecycle::rollback_deadline() const noexcept -> deadline
{
    concurrent_containers::shared_latch_guard lock{started_latch_};
    return rollback_deadline_;
}

auto service_lifecycle::last_rollback_failure() const
    -> std::optional<lifecycle_failure>
{
    concurrent_containers::shared_latch_guard lock{started_latch_};
    return rollback_failure_;
}

auto service_lifecycle::active_service_count() const noexcept -> std::size_t
{
    concurrent_containers::shared_latch_guard lock{started_latch_};
    return static_cast<std::size_t>(std::ranges::count_if(service_started_, [](const auto& item)
        {
            return item.second;
        }));
}

auto service_lifecycle::started_services() const -> std::vector<service_key>
{
    concurrent_containers::shared_latch_guard lock{started_latch_};
    std::vector<service_key> result;
    result.reserve(service_started_.size());
    for (const auto& [key, active] : service_started_)
        if (active)
            result.push_back(key);
    std::ranges::sort(result, {}, &service_key::canonical_name);
    return result;
}

auto service_lifecycle::last_failure() const
    -> std::optional<lifecycle_failure>
{
    concurrent_containers::shared_latch_guard lock{started_latch_};
    return last_failure_;
}

} // namespace cnetmod::application
