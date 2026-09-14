module cnetmod.application.health_registry;

import std;
import cnetmod.instrumentation.error;
import cnetmod.instrumentation.operation_scope;
import cnetmod.instrumentation.operation_result;
import cnetmod.instrumentation.tracing;
import nlohmann.json;
import cnetmod.coro.cancel;
import cnetmod.coro.task_group;
import cnetmod.protocol.http.middleware.tracing;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::application {

namespace {

    auto status_name(service_health status) -> std::string_view
    {
        switch (status)
        {
        case service_health::starting:
            return "STARTING";
        case service_health::up:
            return "UP";
        case service_health::degraded:
            return "DEGRADED";
        case service_health::down:
            return "DOWN";
        case service_health::stopping:
            return "STOPPING";
        case service_health::stopped:
            return "STOPPED";
        }
        return "UNKNOWN";
    }

} // namespace

class health_registry::implementation
{
public:
    struct entry
    {
        std::shared_ptr<managed_service> service;
        service_health_snapshot snapshot;
    };

    mutable concurrent_containers::atomic_rw_latch latch;
    std::unordered_map<service_key, entry, service_key_hash> entries;
    health_policy policy;
    bool alive = true;
    bool running = false;
    bool stopping = false;

    /**
     * @brief Invalidates cached success when a probe cycle cannot be prepared.
     *
     * No child exists at this boundary. Updating in place requires no allocation
     * and preserves the configured failure threshold under memory pressure.
     */
    void fail_preparation(std::error_code error) noexcept
    {
        concurrent_containers::exclusive_latch_guard lock{latch};
        if (stopping)
            return;
        const auto now = std::chrono::system_clock::now();
        for (auto& [key, entry] : entries)
        {
            (void)key;
            auto& snapshot = entry.snapshot;
            if (snapshot.report.status == service_health::stopping ||
                snapshot.report.status == service_health::stopped)
                continue;
            ++snapshot.revision;
            snapshot.consecutive_successes = 0;
            ++snapshot.consecutive_failures;
            snapshot.report.status = snapshot.consecutive_failures < policy.failures_before_down
                ? service_health::degraded
                : service_health::down;
            snapshot.report.message.clear();
            snapshot.report.error = error;
            snapshot.report.checked_at = now;
        }
    }
};

health_registry::health_registry(health_policy policy)
    : implementation_(std::make_shared<implementation>())
{
    implementation_->policy = policy;
}

auto health_registry::add(std::shared_ptr<managed_service> service)
    -> std::expected<void, std::error_code>
{
    if (!service)
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    auto key = service->key();
    const auto requirement = service->requirement();
    implementation::entry entry{
        .service = std::move(service),
        .snapshot = {
            .key = key,
            .requirement = requirement,
            .report = {},
        },
    };
    concurrent_containers::exclusive_latch_guard lock{implementation_->latch};
    if (implementation_->entries.contains(key))
        return std::unexpected(std::make_error_code(std::errc::file_exists));
    implementation_->entries.emplace(std::move(key), std::move(entry));
    return {};
}

void health_registry::mark_starting() noexcept
{
    concurrent_containers::exclusive_latch_guard lock{implementation_->latch};
    implementation_->running = false;
    implementation_->stopping = false;
}

void health_registry::mark_running() noexcept
{
    concurrent_containers::exclusive_latch_guard lock{implementation_->latch};
    implementation_->running = true;
    implementation_->stopping = false;
}

void health_registry::mark_stopping() noexcept
{
    concurrent_containers::exclusive_latch_guard lock{implementation_->latch};
    implementation_->running = false;
    implementation_->stopping = true;
}

void health_registry::update(const service_key& key, health_report report)
{
    update_report(key, std::move(report), std::nullopt);
}

void health_registry::update_report(const service_key& key, health_report report,
    std::optional<std::uint64_t> source_revision)
{
    concurrent_containers::exclusive_latch_guard lock{implementation_->latch};
    const auto found = implementation_->entries.find(key);
    if (found == implementation_->entries.end())
        return;
    auto& snapshot = found->second.snapshot;
    if (source_revision && (snapshot.revision != *source_revision || implementation_->stopping))
        return;
    ++snapshot.revision;
    if (report.status == service_health::stopping || report.status == service_health::stopped)
    {
        snapshot.consecutive_successes = 0;
        snapshot.consecutive_failures = 0;
        snapshot.report = std::move(report);
        return;
    }
    const auto successful = report.status == service_health::up;
    snapshot.consecutive_successes = successful
        ? snapshot.consecutive_successes + 1U
        : 0U;
    snapshot.consecutive_failures = successful
        ? 0U
        : snapshot.consecutive_failures + 1U;
    if (successful && snapshot.consecutive_successes < implementation_->policy.successes_before_up &&
        snapshot.report.status != service_health::up)
        report.status = service_health::starting;
    if (!successful && snapshot.consecutive_failures < implementation_->policy.failures_before_down)
        report.status = service_health::degraded;
    snapshot.report = std::move(report);
}

auto health_registry::refresh(service_context& context) -> task<void>
{
    struct pending_probe
    {
        service_key key;
        std::shared_ptr<managed_service> service;
        std::uint64_t revision = 0;
        bool completed = false;
    };

    std::vector<pending_probe> services;
    std::optional<task_group> prepared;
    try
    {
        {
            concurrent_containers::shared_latch_guard lock{implementation_->latch};
            if (implementation_->stopping)
                co_return;
            services.reserve(implementation_->entries.size());
            for (const auto& [key, entry] : implementation_->entries)
            {
                if (entry.snapshot.report.status == service_health::stopping ||
                    entry.snapshot.report.status == service_health::stopped)
                    continue;
                services.push_back({key, entry.service, entry.snapshot.revision});
            }
        }
        prepared.emplace(context.io, context.operation_deadline);
    }
    catch (const std::bad_alloc&)
    {
        implementation_->fail_preparation(std::make_error_code(std::errc::not_enough_memory));
        co_return;
    }
    catch (...)
    {
        implementation_->fail_preparation(std::make_error_code(std::errc::io_error));
        co_return;
    }
    auto& probes = *prepared;
    std::error_code dispatch_error;
    for (auto& pending : services)
    {
        try
        {
            const auto accepted = probes.run([this, &pending, &context](cancel_token& token)
                                                 -> task<std::expected<void, std::error_code>>
                {
                    const auto& key = pending.key;
                    const auto& service = pending.service;
                    health_report report;
                    auto span = instrumentation::operation_scope::start(
                        context.telemetry.spans(), [&]
                        {
                            auto active = instrumentation::start_client_span({},
                                "application.health.probe");
                            active.kind = instrumentation::span_kind::internal;
                            return active;
                        });
                    span.annotate([&]
                        {
                            return std::vector<std::pair<std::string, std::string>>{
                                {"service.name", key.name}, {"service.instance", key.instance}};
                        });
                    try
                    {
                        service_context probe_context{context.io,
                            context.telemetry, context.supervisor, token,
                            context.operation_deadline};
                        report = co_await service->probe(probe_context);
                    }
                    catch (const std::system_error& error)
                    {
                        report.status = service_health::down;
                        report.message.clear();
                        report.error = error.code();
                    }
                    catch (const std::bad_alloc&)
                    {
                        report.status = service_health::down;
                        report.message.clear();
                        report.error = std::make_error_code(std::errc::not_enough_memory);
                    }
                    catch (...)
                    {
                        report.status = service_health::down;
                        report.message.clear();
                        report.error = std::make_error_code(std::errc::io_error);
                    }
                    auto outcome = instrumentation::classify_error(report.error);
                    if (!report.error && report.status == service_health::down)
                        outcome.status = instrumentation::operation_status::error;
                    span.complete(outcome);
                    report.checked_at = std::chrono::system_clock::now();
                    const auto failed = report.status == service_health::down;
                    update_report(key, std::move(report), pending.revision);
                    pending.completed = true;
                    if (context.telemetry.records_metrics())
                        context.telemetry.increment_local_counter(
                            "application_health_probes_total");
                    if (context.telemetry.records_metrics() &&
                        failed)
                        context.telemetry.increment_local_counter(
                            "application_health_probe_failures_total");
                    co_return {};
                });
            if (!accepted)
                dispatch_error = std::make_error_code(std::errc::operation_canceled);
        }
        catch (const std::bad_alloc&)
        {
            dispatch_error = std::make_error_code(std::errc::not_enough_memory);
        }
        catch (...)
        {
            dispatch_error = std::make_error_code(std::errc::io_error);
        }
    }
    try
    {
        const auto joined = co_await probes.join();
        if (!joined && !dispatch_error)
            dispatch_error = joined.error();
    }
    catch (const std::bad_alloc&)
    {
        dispatch_error = std::make_error_code(std::errc::not_enough_memory);
    }
    catch (...)
    {
        dispatch_error = std::make_error_code(std::errc::io_error);
    }
    if (dispatch_error)
    {
        probes.cancel();
        co_await probes.settle();
    }
    for (const auto& pending : services)
    {
        if (!pending.completed)
            update_report(pending.key, {
                                           .status = service_health::down,
                                           .message = {},
                                           .error = dispatch_error ? dispatch_error : std::make_error_code(std::errc::io_error),
                                           .checked_at = std::chrono::system_clock::now(),
                                       },
                pending.revision);
    }
}

auto health_registry::live() const noexcept -> bool
{
    concurrent_containers::shared_latch_guard lock{implementation_->latch};
    return implementation_->alive;
}

auto health_registry::ready() const noexcept -> bool
{
    concurrent_containers::shared_latch_guard lock{implementation_->latch};
    if (!implementation_->running || implementation_->stopping)
        return false;
    for (const auto& [key, entry] : implementation_->entries)
    {
        (void)key;
        if (entry.snapshot.report.status != service_health::up)
            return false;
    }
    return true;
}

auto health_registry::is_current(const service_health_snapshot& snapshot) const noexcept -> bool
{
    concurrent_containers::shared_latch_guard lock{implementation_->latch};
    const auto found = implementation_->entries.find(snapshot.key);
    return found != implementation_->entries.end() &&
        found->second.snapshot.revision == snapshot.revision;
}

auto health_registry::snapshots() const
    -> std::vector<service_health_snapshot>
{
    std::vector<service_health_snapshot> result;
    concurrent_containers::shared_latch_guard lock{implementation_->latch};
    result.reserve(implementation_->entries.size());
    for (const auto& [key, entry] : implementation_->entries)
    {
        (void)key;
        result.push_back(entry.snapshot);
    }
    std::ranges::sort(result, {}, [](const auto& value)
        {
            return value.key.canonical_name();
        });
    return result;
}

auto health_registry::json(bool readiness_only) const -> std::string
{
    nlohmann::json components = nlohmann::json::object();
    for (const auto& item : snapshots())
    {
        components[item.key.canonical_name()] = {
            {"status", status_name(item.report.status)},
            {"required", item.requirement == service_requirement::required},
            {"message", item.report.message},
            {"error", item.report.error ? item.report.error.category().name() : ""},
            {"checkedAtUnixMs", std::chrono::duration_cast<std::chrono::milliseconds>(item.report.checked_at.time_since_epoch()).count()},
            {"consecutiveFailures", item.consecutive_failures},
            {"consecutiveSuccesses", item.consecutive_successes},
        };
    }
    auto healthy = readiness_only ? ready() : live();
    if (!readiness_only)
    {
        for (const auto& item : snapshots())
        {
            if (item.report.status == service_health::down)
            {
                healthy = false;
                break;
            }
        }
    }
    return nlohmann::json{{"status", healthy ? "UP" : "DOWN"},
        {"components", std::move(components)}}
        .dump();
}

auto health_registry::policy() const noexcept -> health_policy
{
    concurrent_containers::shared_latch_guard lock{implementation_->latch};
    return implementation_->policy;
}

void health_registry::update_policy(health_policy policy)
{
    if (policy.failures_before_down == 0U ||
        policy.successes_before_up == 0U || policy.interval.count() <= 0 ||
        policy.timeout.count() <= 0)
        throw std::invalid_argument("invalid health policy");
    concurrent_containers::exclusive_latch_guard lock{implementation_->latch};
    implementation_->policy = policy;
}

} // namespace cnetmod::application
