module cnetmod.application.configuration;

import std;
import cnetmod.application.recovery_policy;

namespace cnetmod::application {
namespace {

    /**
     * @brief Releases a temporary JSON tree without allocating a traversal stack.
     * Removes leaves before their parents so JSON destructors only see scalars
     * or empty containers. Traversal uses constant auxiliary storage and does
     * not recurse, including for deeply nested configuration documents.
     */
    class document_cleanup
    {
    public:
        explicit document_cleanup(nlohmann::json& document) noexcept
            : document_(document)
        {
        }

        document_cleanup(const document_cleanup&) = delete;
        auto operator=(const document_cleanup&) -> document_cleanup& = delete;

        ~document_cleanup() noexcept
        {
            while (document_.is_structured() && !document_.empty())
            {
                auto* parent = &document_;
                auto* node = &document_;
                while (node->is_structured() && !node->empty())
                {
                    parent = node;
                    node = node->is_object()
                        ? &node->get_ref<nlohmann::json::object_t&>().begin()->second
                        : &node->get_ref<nlohmann::json::array_t&>().back();
                }
                if (parent->is_object())
                {
                    auto& object = parent->get_ref<nlohmann::json::object_t&>();
                    object.erase(object.begin());
                }
                else
                    parent->get_ref<nlohmann::json::array_t&>().pop_back();
            }
        }

    private:
        nlohmann::json& document_;
    };

    auto environment(std::string_view name) -> std::optional<std::string>
    {
        const auto owned = std::string{name};
        if (const auto* value = std::getenv(owned.c_str());
            value && *value != '\0')
            return std::string{value};
        return std::nullopt;
    }

    auto read_document(const std::filesystem::path& path)
        -> std::expected<nlohmann::json, std::error_code>
    {
        std::ifstream input{path, std::ios::binary};
        if (!input)
            return std::unexpected(
                std::make_error_code(std::errc::no_such_file_or_directory));
        try
        {
            auto result = nlohmann::json::parse(input, nullptr, true, true);
            const document_cleanup cleanup{result};
            if (!result.is_object())
                return std::unexpected(
                    std::make_error_code(std::errc::invalid_argument));
            return result;
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(
                std::make_error_code(std::errc::not_enough_memory));
        }
        catch (...)
        {
            return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
        }
    }

    auto keys_are_known(const nlohmann::json& object,
        std::initializer_list<std::string_view> allowed) -> bool
    {
        if (!object.is_object())
            return false;
        for (auto item = object.begin(); item != object.end(); ++item)
        {
            if (std::ranges::find(allowed, item.key()) == allowed.end())
                return false;
        }
        return true;
    }

    template <class Value>
    void assign(const nlohmann::json& object, std::string_view key,
        Value& destination)
    {
        if (const auto found = object.find(key); found != object.end())
            destination = found->template get<Value>();
    }

    void assign_duration(const nlohmann::json& object, std::string_view key,
        std::chrono::milliseconds& destination)
    {
        if (const auto found = object.find(key); found != object.end())
            destination = std::chrono::milliseconds{
                found->get<std::int64_t>()};
    }

    auto parse_log_level(std::string_view value)
        -> std::optional<logger::level>
    {
        if (value == "trace")
            return logger::level::trace;
        if (value == "debug")
            return logger::level::debug;
        if (value == "info")
            return logger::level::info;
        if (value == "warn")
            return logger::level::warn;
        if (value == "error")
            return logger::level::error;
        if (value == "critical")
            return logger::level::critical;
        if (value == "off")
            return logger::level::off;
        return std::nullopt;
    }

    auto parse_boolean(std::string_view value) -> std::optional<bool>
    {
        if (value == "true" || value == "1")
            return true;
        if (value == "false" || value == "0")
            return false;
        return std::nullopt;
    }

    auto parse_key_values(std::string_view value)
        -> std::map<std::string, std::string, std::less<>>
    {
        std::map<std::string, std::string, std::less<>> result;
        std::size_t begin = 0;
        while (begin <= value.size())
        {
            const auto end = value.find(',', begin);
            const auto entry = value.substr(begin,
                end == std::string_view::npos
                    ? value.size() - begin
                    : end - begin);
            const auto separator = entry.find('=');
            if (separator != std::string_view::npos && separator != 0U)
                result.insert_or_assign(
                    std::string{entry.substr(0, separator)},
                    std::string{entry.substr(separator + 1U)});
            if (end == std::string_view::npos)
                break;
            begin = end + 1U;
        }
        return result;
    }

    auto expand_string(std::string source)
        -> std::expected<std::string, std::error_code>
    {
        auto begin = source.find("${");
        while (begin != std::string::npos)
        {
            const auto end = source.find('}', begin + 2U);
            if (end == std::string::npos)
                return std::unexpected(
                    std::make_error_code(std::errc::invalid_argument));
            const auto value = environment(
                source.substr(begin + 2U, end - begin - 2U));
            if (!value)
                return std::unexpected(
                    std::make_error_code(std::errc::no_such_file_or_directory));
            source.replace(begin, end - begin + 1U, *value);
            begin = source.find("${", begin + value->size());
        }
        return source;
    }

    auto expand_environment(nlohmann::json& value)
        -> std::expected<void, std::error_code>
    {
        if (value.is_string())
        {
            auto expanded = expand_string(value.get<std::string>());
            if (!expanded)
                return std::unexpected(expanded.error());
            value = std::move(*expanded);
        }
        else if (value.is_structured())
        {
            for (auto& child : value)
            {
                auto expanded = expand_environment(child);
                if (!expanded)
                    return expanded;
            }
        }
        return {};
    }

    auto apply_document(application_configuration& result,
        const nlohmann::json& root) -> bool
    {
        if (!keys_are_known(root, {"application", "logging", "http", "management", "observability", "lifecycle", "health", "services"}))
            return false;
        try
        {
            if (const auto item = root.find("application"); item != root.end())
            {
                if (!keys_are_known(*item,
                        {"name", "install_signal_handlers"}))
                    return false;
                assign(*item, "name", result.name);
                assign(*item, "install_signal_handlers",
                    result.install_signal_handlers);
            }
            if (const auto item = root.find("logging"); item != root.end())
            {
                if (!keys_are_known(*item,
                        {"manage_lifecycle", "level", "format"}))
                    return false;
                assign(*item, "manage_lifecycle",
                    result.logging.manage_lifecycle);
                if (const auto level = item->find("level");
                    level != item->end())
                {
                    const auto parsed = parse_log_level(
                        level->get<std::string>());
                    if (!parsed)
                        return false;
                    result.logging.level = *parsed;
                }
                if (const auto format = item->find("format");
                    format != item->end())
                {
                    const auto name = format->get<std::string>();
                    if (name != "text" && name != "json")
                        return false;
                    result.logging.format = name == "json"
                        ? logger::output_format::json
                        : logger::output_format::text;
                }
            }
            if (const auto item = root.find("http"); item != root.end())
            {
                if (!keys_are_known(*item, {"address", "port", "max_connections", "request_timeout_ms", "request_ids", "access_logging", "recover_exceptions"}))
                    return false;
                assign(*item, "address", result.http.address);
                assign(*item, "port", result.http.port);
                assign(*item, "max_connections", result.http.max_connections);
                if (const auto timeout = item->find("request_timeout_ms");
                    timeout != item->end())
                    result.http.request_timeout = std::chrono::milliseconds{
                        timeout->get<std::int64_t>()};
                assign(*item, "request_ids", result.http.request_ids);
                assign(*item, "access_logging", result.http.access_logging);
                assign(*item, "recover_exceptions",
                    result.http.recover_exceptions);
            }
            if (const auto item = root.find("management"); item != root.end())
            {
                if (!keys_are_known(*item, {"enabled", "address", "port", "same_port", "live_path", "ready_path", "health_path", "metrics_path"}))
                    return false;
                assign(*item, "enabled", result.management.enabled);
                assign(*item, "address", result.management.address);
                assign(*item, "port", result.management.port);
                assign(*item, "same_port", result.management.same_port);
                assign(*item, "live_path", result.management.live_path);
                assign(*item, "ready_path", result.management.ready_path);
                assign(*item, "health_path", result.management.health_path);
                assign(*item, "metrics_path", result.management.metrics_path);
            }
            if (const auto item = root.find("observability");
                item != root.end())
            {
                if (!keys_are_known(*item, {"tracing", "metrics", "logs", "sampling_ratio", "otlp"}))
                    return false;
                assign(*item, "tracing", result.observability.tracing);
                assign(*item, "metrics", result.observability.metrics);
                assign(*item, "logs", result.observability.logs);
                assign(*item, "sampling_ratio",
                    result.observability.sampling_ratio);
                if (const auto otlp = item->find("otlp"); otlp != item->end())
                {
                    if (!keys_are_known(*otlp, {"traces_endpoint", "metrics_endpoint", "logs_endpoint", "service_name", "service_version", "service_namespace", "service_instance_id", "deployment_environment", "resource_attributes", "headers", "queue_capacity", "max_batch_size", "request_timeout_ms", "max_attempts", "initial_retry_delay_ms", "max_retry_delay_ms", "max_metric_instruments", "max_metric_attribute_sets", "capture_framework_logs"}))
                        return false;
                    assign(*otlp, "traces_endpoint",
                        result.observability.otlp.endpoint);
                    assign(*otlp, "metrics_endpoint",
                        result.observability.otlp.metrics_endpoint);
                    assign(*otlp, "logs_endpoint",
                        result.observability.otlp.logs_endpoint);
                    assign(*otlp, "service_name",
                        result.observability.otlp.service_name);
                    assign(*otlp, "service_version",
                        result.observability.otlp.service_version);
                    assign(*otlp, "service_namespace",
                        result.observability.otlp.service_namespace);
                    assign(*otlp, "service_instance_id",
                        result.observability.otlp.service_instance_id);
                    assign(*otlp, "deployment_environment",
                        result.observability.otlp.deployment_environment);
                    assign(*otlp, "resource_attributes",
                        result.observability.otlp.resource_attributes);
                    assign(*otlp, "headers", result.observability.otlp.headers);
                    assign(*otlp, "queue_capacity",
                        result.observability.otlp.queue_capacity);
                    assign(*otlp, "max_batch_size",
                        result.observability.otlp.max_batch_size);
                    assign(*otlp, "max_metric_instruments",
                        result.observability.otlp.max_metric_instruments);
                    assign(*otlp, "max_metric_attribute_sets",
                        result.observability.otlp.max_metric_attribute_sets);
                    assign(*otlp, "capture_framework_logs",
                        result.observability.otlp.capture_framework_logs);
                    assign_duration(*otlp, "request_timeout_ms",
                        result.observability.otlp.request_timeout);
                    assign(*otlp, "max_attempts",
                        result.observability.otlp.max_attempts);
                    assign_duration(*otlp, "initial_retry_delay_ms",
                        result.observability.otlp.initial_retry_delay);
                    assign_duration(*otlp, "max_retry_delay_ms",
                        result.observability.otlp.max_retry_delay);
                }
            }
            if (const auto item = root.find("lifecycle"); item != root.end())
            {
                if (!keys_are_known(*item, {"service_start_timeout_ms", "total_start_timeout_ms", "service_stop_timeout_ms", "total_stop_timeout_ms", "http_drain_timeout_ms", "telemetry_flush_timeout_ms"}))
                    return false;
                assign_duration(*item, "service_start_timeout_ms",
                    result.lifecycle.service_start_timeout);
                assign_duration(*item, "total_start_timeout_ms",
                    result.lifecycle.total_start_timeout);
                assign_duration(*item, "service_stop_timeout_ms",
                    result.lifecycle.service_stop_timeout);
                assign_duration(*item, "total_stop_timeout_ms",
                    result.lifecycle.total_stop_timeout);
                assign_duration(*item, "http_drain_timeout_ms",
                    result.lifecycle.http_drain_timeout);
                assign_duration(*item, "telemetry_flush_timeout_ms",
                    result.lifecycle.telemetry_flush_timeout);
            }
            if (const auto item = root.find("health"); item != root.end())
            {
                if (!keys_are_known(*item, {"interval_ms", "timeout_ms", "failures_before_down", "successes_before_up"}))
                    return false;
                assign_duration(*item, "interval_ms", result.health.interval);
                assign_duration(*item, "timeout_ms", result.health.timeout);
                assign(*item, "failures_before_down",
                    result.health.failures_before_down);
                assign(*item, "successes_before_up",
                    result.health.successes_before_up);
            }
            if (const auto item = root.find("services"); item != root.end())
            {
                if (!item->is_object())
                    return false;
                for (auto service_item = item->begin();
                    service_item != item->end(); ++service_item)
                {
                    const auto& name = service_item.key();
                    const auto& value = service_item.value();
                    if (!value.is_object())
                        return false;
                    configured_service service{.name = name};
                    assign(value, "type", service.name);
                    assign(value, "enabled", service.enabled);
                    assign(value, "instance", service.instance);
                    if (const auto required = value.find("required");
                        required != value.end())
                        service.requirement = required->get<bool>()
                            ? service_requirement::required
                            : service_requirement::optional;
                    if (const auto recovery = value.find("recovery");
                        recovery != value.end())
                    {
                        if (!keys_are_known(*recovery, {"initial_delay_ms", "maximum_delay_ms", "budget_ms", "multiplier", "jitter"}))
                            return false;
                        assign_duration(*recovery, "initial_delay_ms",
                            service.recovery.initial_delay);
                        assign_duration(*recovery, "maximum_delay_ms",
                            service.recovery.maximum_delay);
                        assign_duration(*recovery, "budget_ms",
                            service.recovery.budget);
                        assign(*recovery, "multiplier",
                            service.recovery.multiplier);
                        assign(*recovery, "jitter", service.recovery.jitter);
                    }
                    for (auto property = value.begin(); property != value.end(); ++property)
                    {
                        const auto& key = property.key();
                        if (key != "enabled" && key != "type" && key != "instance" &&
                            key != "required" && key != "recovery")
                            service.properties[key] = property.value();
                    }
                    result.services.insert_or_assign(name, std::move(service));
                }
            }
            return true;
        }
        catch (const std::bad_alloc&)
        {
            throw;
        }
        catch (...)
        {
            return false;
        }
    }

    auto apply_process_environment(application_configuration& result)
        -> std::expected<void, std::error_code>
    {
        if (auto value = environment("CNETMOD_APPLICATION_NAME"))
            result.name = std::move(*value);
        if (auto value = environment("CNETMOD_HTTP_ADDRESS"))
            result.http.address = std::move(*value);
        if (auto value = environment("CNETMOD_HTTP_PORT"))
        {
            unsigned int parsed{};
            const auto [end, error] = std::from_chars(value->data(),
                value->data() + value->size(), parsed);
            if (error == std::errc{} && end == value->data() + value->size() &&
                parsed > 0U && parsed <= 65535U)
                result.http.port = static_cast<std::uint16_t>(parsed);
            else
                return std::unexpected(
                    std::make_error_code(std::errc::invalid_argument));
        }
        if (auto value = environment("CNETMOD_LOG_LEVEL"))
        {
            const auto level = parse_log_level(*value);
            if (!level)
                return std::unexpected(
                    std::make_error_code(std::errc::invalid_argument));
            result.logging.level = *level;
        }
        if (auto value = environment("OTEL_EXPORTER_OTLP_TRACES_ENDPOINT"))
            result.observability.otlp.endpoint = std::move(*value);
        else if (auto value = environment("OTEL_EXPORTER_OTLP_ENDPOINT"))
        {
            while (value->ends_with('/'))
                value->pop_back();
            result.observability.otlp.endpoint =
                std::move(*value) + "/v1/traces";
        }
        if (auto value = environment("OTEL_EXPORTER_OTLP_METRICS_ENDPOINT"))
            result.observability.otlp.metrics_endpoint = std::move(*value);
        if (auto value = environment("OTEL_EXPORTER_OTLP_LOGS_ENDPOINT"))
            result.observability.otlp.logs_endpoint = std::move(*value);
        if (auto value = environment("OTEL_SERVICE_NAME"))
            result.observability.otlp.service_name = std::move(*value);
        if (auto value = environment("OTEL_SERVICE_VERSION"))
            result.observability.otlp.service_version = std::move(*value);
        if (auto value = environment("OTEL_RESOURCE_ATTRIBUTES"))
            result.observability.otlp.resource_attributes =
                parse_key_values(*value);
        if (auto value = environment("OTEL_EXPORTER_OTLP_HEADERS"))
            result.observability.otlp.headers = parse_key_values(*value);
        if (auto value = environment("CNETMOD_OTLP_CAPTURE_FRAMEWORK_LOGS"))
        {
            const auto enabled = parse_boolean(*value);
            if (!enabled)
                return std::unexpected(
                    std::make_error_code(std::errc::invalid_argument));
            result.observability.otlp.capture_framework_logs = *enabled;
        }
        return {};
    }

} // namespace

auto load_configuration(const std::optional<std::filesystem::path>& file)
    -> std::expected<application_configuration, std::error_code>
try
{
    application_configuration result;
    if (file)
    {
        auto document = read_document(*file);
        if (!document)
            return std::unexpected(document.error());
        const document_cleanup cleanup{*document};
        auto expanded = expand_environment(*document);
        if (!expanded)
            return std::unexpected(expanded.error());
        if (!apply_document(result, *document))
            return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
    }
    auto environment_applied = apply_process_environment(result);
    if (!environment_applied)
        return std::unexpected(environment_applied.error());
    if (result.observability.otlp.service_name.empty() ||
        result.observability.otlp.service_name == "cnetmod")
        result.observability.otlp.service_name = result.name;
    auto validation = validate_configuration(result);
    if (!validation)
        return std::unexpected(validation.error());
    return result;
}
catch (const std::bad_alloc&)
{
    return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
}
catch (...)
{
    return std::unexpected(std::make_error_code(std::errc::io_error));
}

auto validate_configuration(const application_configuration& value)
    -> std::expected<void, std::error_code>
{
    const auto positive = [](auto duration)
    {
        const auto horizon = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::duration::max() / 2);
        return duration.count() > 0 && duration <= horizon;
    };
    if (value.name.empty() || value.http.address.empty() ||
        value.http.port == 0U ||
        (value.http.request_timeout && !positive(*value.http.request_timeout)) ||
        !std::isfinite(value.observability.sampling_ratio) || value.observability.sampling_ratio < 0.0 ||
        value.observability.sampling_ratio > 1.0 ||
        !positive(value.lifecycle.service_start_timeout) ||
        !positive(value.lifecycle.total_start_timeout) ||
        !positive(value.lifecycle.service_stop_timeout) ||
        !positive(value.lifecycle.total_stop_timeout) ||
        !positive(value.lifecycle.http_drain_timeout) ||
        !positive(value.lifecycle.telemetry_flush_timeout) ||
        !positive(value.health.interval) || !positive(value.health.timeout) ||
        value.health.failures_before_down == 0U ||
        value.health.successes_before_up == 0U ||
        value.observability.otlp.queue_capacity < 2U ||
        value.observability.otlp.max_metric_instruments == 0U ||
        value.observability.otlp.max_batch_size == 0U ||
        value.observability.otlp.max_batch_size >
            value.observability.otlp.queue_capacity ||
        value.observability.otlp.max_attempts == 0U ||
        !positive(value.observability.otlp.request_timeout) ||
        value.observability.otlp.initial_retry_delay.count() < 0 ||
        value.observability.otlp.max_retry_delay <
            value.observability.otlp.initial_retry_delay)
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    if (value.management.enabled &&
        (value.management.address.empty() || value.management.port == 0U))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    for (const auto& [name, service] : value.services)
    {
        if (name.empty() || service.name.empty() || service.instance.empty() ||
            !valid_recovery_policy(service.recovery) ||
            !positive(service.recovery.budget))
            return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
    }
    std::unordered_set<service_key, service_key_hash> service_keys;
    for (const auto& [binding, service] : value.services)
    {
        (void)binding;
        if (!service_keys.emplace(service_key{service.name, service.instance}).second)
            return std::unexpected(
                std::make_error_code(std::errc::file_exists));
    }
    return {};
}

static auto prepare_configuration_reload(application_configuration& active,
    const application_configuration& candidate) -> configuration_reload_result
{
    configuration_reload_result result;
    const auto same_recovery = [](const recovery_policy& left,
                                   const recovery_policy& right)
    {
        return left.initial_delay == right.initial_delay &&
            left.maximum_delay == right.maximum_delay &&
            left.budget == right.budget &&
            left.multiplier == right.multiplier &&
            left.jitter == right.jitter;
    };
    const auto same_otlp = [](const observability::otlp_http_options& left,
                               const observability::otlp_http_options& right)
    {
        return left.export_traces == right.export_traces &&
            left.export_metrics == right.export_metrics &&
            left.export_logs == right.export_logs &&
            left.capture_framework_logs == right.capture_framework_logs &&
            left.endpoint == right.endpoint &&
            left.metrics_endpoint == right.metrics_endpoint &&
            left.logs_endpoint == right.logs_endpoint &&
            left.service_name == right.service_name &&
            left.service_version == right.service_version &&
            left.service_namespace == right.service_namespace &&
            left.service_instance_id == right.service_instance_id &&
            left.deployment_environment == right.deployment_environment &&
            left.resource_attributes == right.resource_attributes &&
            left.headers == right.headers &&
            left.queue_capacity == right.queue_capacity &&
            left.max_batch_size == right.max_batch_size &&
            left.max_metric_instruments == right.max_metric_instruments &&
            left.max_metric_attribute_sets == right.max_metric_attribute_sets &&
            left.request_timeout == right.request_timeout &&
            left.max_attempts == right.max_attempts &&
            left.initial_retry_delay == right.initial_retry_delay &&
            left.max_retry_delay == right.max_retry_delay;
    };
    if (active.logging.level != candidate.logging.level)
    {
        active.logging.level = candidate.logging.level;
        result.changed.push_back("logging.level");
    }
    if (active.observability.sampling_ratio !=
        candidate.observability.sampling_ratio)
    {
        active.observability.sampling_ratio =
            candidate.observability.sampling_ratio;
        result.changed.push_back("observability.sampling_ratio");
    }
    if (active.health.interval != candidate.health.interval ||
        active.health.timeout != candidate.health.timeout ||
        active.health.failures_before_down !=
            candidate.health.failures_before_down ||
        active.health.successes_before_up !=
            candidate.health.successes_before_up)
    {
        active.health = candidate.health;
        result.changed.push_back("health");
    }
    for (auto& [binding, service] : active.services)
    {
        const auto found = candidate.services.find(binding);
        if (found != candidate.services.end() &&
            found->second.name == service.name &&
            found->second.instance == service.instance &&
            !same_recovery(service.recovery, found->second.recovery))
        {
            service.recovery = found->second.recovery;
            result.changed.push_back(
                std::format("services.{}.recovery", binding));
        }
    }
    result.applied = !result.changed.empty();
    result.restart_required = active.name != candidate.name ||
        active.install_signal_handlers != candidate.install_signal_handlers ||
        active.logging.manage_lifecycle != candidate.logging.manage_lifecycle ||
        active.logging.format != candidate.logging.format ||
        active.http.address != candidate.http.address ||
        active.http.port != candidate.http.port ||
        active.http.max_connections != candidate.http.max_connections ||
        active.http.request_timeout != candidate.http.request_timeout ||
        active.http.request_ids != candidate.http.request_ids ||
        active.http.access_logging != candidate.http.access_logging ||
        active.http.recover_exceptions != candidate.http.recover_exceptions ||
        active.management.enabled != candidate.management.enabled ||
        active.management.address != candidate.management.address ||
        active.management.port != candidate.management.port ||
        active.management.same_port != candidate.management.same_port ||
        active.management.live_path != candidate.management.live_path ||
        active.management.ready_path != candidate.management.ready_path ||
        active.management.health_path != candidate.management.health_path ||
        active.management.metrics_path != candidate.management.metrics_path ||
        active.observability.tracing != candidate.observability.tracing ||
        active.observability.metrics != candidate.observability.metrics ||
        active.observability.logs != candidate.observability.logs ||
        !same_otlp(active.observability.otlp, candidate.observability.otlp) ||
        active.lifecycle.service_start_timeout != candidate.lifecycle.service_start_timeout ||
        active.lifecycle.total_start_timeout != candidate.lifecycle.total_start_timeout ||
        active.lifecycle.service_stop_timeout != candidate.lifecycle.service_stop_timeout ||
        active.lifecycle.total_stop_timeout != candidate.lifecycle.total_stop_timeout ||
        active.lifecycle.http_drain_timeout != candidate.lifecycle.http_drain_timeout ||
        active.lifecycle.telemetry_flush_timeout != candidate.lifecycle.telemetry_flush_timeout ||
        active.services.size() != candidate.services.size();
    if (!result.restart_required)
    {
        for (const auto& [binding, service] : active.services)
        {
            const auto found = candidate.services.find(binding);
            if (found == candidate.services.end() ||
                service.name != found->second.name ||
                service.instance != found->second.instance ||
                service.enabled != found->second.enabled ||
                service.requirement != found->second.requirement ||
                service.properties != found->second.properties)
            {
                result.restart_required = true;
                break;
            }
        }
    }
    return result;
}

auto reload_safe_configuration(application_configuration& active,
    const application_configuration& candidate)
    -> std::expected<configuration_reload_result, std::error_code>
{
    try
    {
        if (auto valid = validate_configuration(candidate); !valid)
            return std::unexpected(valid.error());
        auto staged = active;
        auto result = prepare_configuration_reload(staged, candidate);
        static_assert(std::is_nothrow_move_assignable_v<application_configuration>);
        static_assert(std::is_nothrow_move_constructible_v<configuration_reload_result>);
        active = std::move(staged);
        return result;
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

auto redact_configuration(const nlohmann::json& value) -> nlohmann::json
{
    if (value.is_array())
    {
        auto result = nlohmann::json::array();
        for (const auto& child : value)
            result.push_back(redact_configuration(child));
        return result;
    }
    if (value.is_string())
    {
        auto text = value.get<std::string>();
        const auto scheme = text.find("://");
        const auto credentials = scheme == std::string::npos
            ? std::string::npos
            : text.find('@', scheme + 3U);
        if (credentials != std::string::npos)
            text.replace(scheme + 3U, credentials - scheme - 3U,
                "[REDACTED]");
        return text;
    }
    if (!value.is_object())
        return value;
    auto result = nlohmann::json::object();
    for (auto item = value.begin(); item != value.end(); ++item)
    {
        const auto& key = item.key();
        const auto& child = item.value();
        auto lowered = key;
        std::ranges::transform(lowered, lowered.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        const auto secret = lowered.contains("password") ||
            lowered.contains("secret") || lowered.contains("token") ||
            lowered.contains("authorization") || lowered.contains("api_key") ||
            lowered.contains("credential") || lowered.contains("connection_string") ||
            lowered == "dsn";
        result[key] = secret ? nlohmann::json("[REDACTED]")
                             : redact_configuration(child);
    }
    return result;
}

} // namespace cnetmod::application
