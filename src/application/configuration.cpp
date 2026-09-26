module cnetmod.application.configuration;

import std;
import cnetmod.application.recovery_policy;
#if defined(CNETMOD_HAS_YAML_CONFIGURATION)
import cnetmod.application.yaml_configuration;
#endif

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
        explicit document_cleanup(cnetmod::json::document& document) noexcept
            : document_(document)
        {
        }

        document_cleanup(const document_cleanup&) = delete;
        auto operator=(const document_cleanup&) -> document_cleanup& = delete;

        ~document_cleanup() noexcept
        {
            while ((document_.is_object() || document_.is_array()) &&
                !document_.empty())
            {
                auto* parent = &document_;
                auto* node = &document_;
                while ((node->is_object() || node->is_array()) &&
                    !node->empty())
                {
                    parent = node;
                    node = node->is_object()
                        ? &node->get_object().begin()->second
                        : &node->get_array().back();
                }
                if (parent->is_object())
                {
                    auto& object = parent->get_object();
                    object.erase(object.begin());
                }
                else
                    parent->get_array().pop_back();
            }
        }

    private:
        cnetmod::json::document& document_;
    };

    /**
     * @brief Internal control-flow carrier converted to configuration_error at
     *        the public boundary. It never escapes this translation unit.
     */
    struct configuration_failure
    {
        configuration_error error;
    };

    [[noreturn]] void fail(std::string path, std::string message,
        std::errc code = std::errc::invalid_argument)
    {
        throw configuration_failure{configuration_error{
            .code = std::make_error_code(code),
            .path = std::move(path),
            .message = std::move(message)}};
    }

    [[nodiscard]] auto join(std::string_view parent, std::string_view key)
        -> std::string
    {
        if (parent.empty())
            return std::string{key};
        return std::format("{}.{}", parent, key);
    }

    auto environment(std::string_view name) -> std::optional<std::string>
    {
        const auto owned = std::string{name};
        if (const auto* value = std::getenv(owned.c_str());
            value && *value != '\0')
            return std::string{value};
        return std::nullopt;
    }

    auto read_document(const std::filesystem::path& path)
        -> cnetmod::json::document
    {
        const auto extension = path.extension().string();
        const auto location = path.string();
#if defined(CNETMOD_HAS_YAML_CONFIGURATION)
        if (extension == ".yaml" || extension == ".yml")
        {
            auto loaded = load_yaml_configuration_document(path);
            if (!loaded)
                fail({}, std::format("cannot load YAML configuration '{}': {}",
                             location, loaded.error().message()),
                    std::errc::invalid_argument);
            if (!loaded->is_object())
                fail({}, std::format(
                             "configuration '{}' must contain a mapping at the root",
                             location));
            return std::move(*loaded);
        }
#else
        if (extension == ".yaml" || extension == ".yml")
            fail({}, std::format(
                         "configuration '{}' is YAML but YAML support was not built",
                         location),
                std::errc::not_supported);
#endif
        std::ifstream input{path, std::ios::binary};
        if (!input)
            fail({}, std::format("configuration file '{}' cannot be opened",
                         location),
                std::errc::no_such_file_or_directory);
        const std::string text{std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
        auto parsed = cnetmod::json::parse_document(text);
        if (!parsed)
            fail({}, std::format("configuration '{}' is not valid JSON: {}",
                         location, parsed.error().message()));
        if (!parsed->is_object())
            fail({}, std::format(
                         "configuration '{}' must contain an object at the root",
                         location));
        return std::move(*parsed);
    }

    void require_object(const cnetmod::json::document& value,
        std::string_view path)
    {
        if (!value.is_object())
            fail(std::string{path}, "expected a mapping");
    }

    void require_known(const cnetmod::json::document& object,
        std::string_view path, std::initializer_list<std::string_view> allowed)
    {
        require_object(object, path);
        for (const auto& [key, unused] : object.get_object())
        {
            (void)unused;
            if (std::ranges::find(allowed, key) == allowed.end())
                fail(join(path, key), "unknown key");
        }
    }

    [[nodiscard]] auto type_name(const cnetmod::json::document& value)
        -> std::string_view
    {
        if (value.is_object())
            return "mapping";
        if (value.is_array())
            return "sequence";
        if (value.is_string())
            return "string";
        if (value.is_boolean())
            return "boolean";
        if (value.is_null())
            return "null";
        return "number";
    }

    template <class Value>
    void assign(const cnetmod::json::document& object, std::string_view path,
        std::string_view key, Value& destination)
    {
        const auto* found = cnetmod::json::find(object, key);
        if (found == nullptr)
            return;
        auto decoded = cnetmod::json::from_document<Value>(*found);
        if (!decoded)
            fail(join(path, key), std::format("invalid value of type {}",
                                      type_name(*found)));
        destination = std::move(*decoded);
    }

    void assign_duration(const cnetmod::json::document& object,
        std::string_view path, std::string_view key,
        std::chrono::milliseconds& destination)
    {
        const auto* found = cnetmod::json::find(object, key);
        if (found == nullptr)
            return;
        if (!found->is_int64() && !found->is_uint64())
            fail(join(path, key), std::format(
                                      "expected an integer number of milliseconds, got {}",
                                      type_name(*found)));
        destination = std::chrono::milliseconds{found->as<std::int64_t>()};
    }

    [[nodiscard]] auto string_value(const cnetmod::json::document& value,
        std::string_view path) -> std::string
    {
        if (!value.is_string())
            fail(std::string{path},
                std::format("expected a string, got {}", type_name(value)));
        return value.get<std::string>();
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

    [[nodiscard]] auto valid_variable_name(std::string_view name) noexcept -> bool
    {
        if (name.empty())
            return false;
        const auto first = static_cast<unsigned char>(name.front());
        if (!(std::isalpha(first) || first == '_'))
            return false;
        return std::ranges::all_of(name, [](const char character)
            {
                const auto byte = static_cast<unsigned char>(character);
                return std::isalnum(byte) || byte == '_';
            });
    }

    /**
     * @brief Expands references in one string; failures carry the document path.
     */
    auto expand_string(std::string_view source, std::string_view path)
        -> std::string
    {
        std::string result;
        result.reserve(source.size());
        std::size_t position = 0;
        while (position < source.size())
        {
            const auto marker = source.find('$', position);
            if (marker == std::string_view::npos)
            {
                result.append(source.substr(position));
                break;
            }
            result.append(source.substr(position, marker - position));
            // "$${" is an escaped literal "${".
            if (source.substr(marker).starts_with("$${"))
            {
                result.append("${");
                position = marker + 3U;
                continue;
            }
            if (!source.substr(marker).starts_with("${"))
            {
                result.push_back('$');
                position = marker + 1U;
                continue;
            }
            const auto close = source.find('}', marker + 2U);
            if (close == std::string_view::npos)
                fail(std::string{path}, "unterminated ${...} reference");
            const auto reference = source.substr(marker + 2U, close - marker - 2U);
            const auto separator = reference.find(":-");
            const auto name = separator == std::string_view::npos
                ? reference
                : reference.substr(0, separator);
            if (!valid_variable_name(name))
                fail(std::string{path},
                    std::format("invalid environment variable name '{}'", name));
            if (auto value = environment(name))
                result.append(*value);
            else if (separator != std::string_view::npos)
                result.append(reference.substr(separator + 2U));
            else
                fail(std::string{path},
                    std::format("environment variable '{}' is not set and has "
                                "no ${{{}:-default}}",
                        name, name),
                    std::errc::no_such_file_or_directory);
            position = close + 1U;
        }
        return result;
    }

    /**
     * @brief Expands every string in a document tree without recursion limits
     *        beyond the document depth itself.
     */
    void expand_tree(cnetmod::json::document& value, std::string_view path)
    {
        if (value.is_string())
        {
            value = expand_string(value.get<std::string>(), path);
            return;
        }
        if (value.is_object())
        {
            for (auto& [key, child] : value.get_object())
                expand_tree(child, join(path, key));
            return;
        }
        if (value.is_array())
        {
            std::size_t index = 0;
            for (auto& child : value.get_array())
                expand_tree(child, std::format("{}[{}]", path, index++));
        }
    }

    constexpr std::array<std::string_view, 11> framework_sections{"application",
        "logging", "http", "management", "observability", "crash_dump",
        "lifecycle", "health", "orm", "security", "services"};

    void apply_application(application_configuration& result,
        const cnetmod::json::document& item)
    {
        constexpr std::string_view path = "application";
        require_known(item, path,
            {"name", "install_signal_handlers", "io_threads", "cpu_threads"});
        assign(item, path, "name", result.name);
        assign(item, path, "install_signal_handlers",
            result.install_signal_handlers);
        assign(item, path, "io_threads", result.execution.io_threads);
        assign(item, path, "cpu_threads", result.execution.cpu_threads);
    }

    void apply_logging(application_configuration& result,
        const cnetmod::json::document& item)
    {
        constexpr std::string_view path = "logging";
        require_known(item, path, {"manage_lifecycle", "level", "format"});
        assign(item, path, "manage_lifecycle", result.logging.manage_lifecycle);
        if (const auto* level = cnetmod::json::find(item, "level"))
        {
            const auto text = string_value(*level, "logging.level");
            const auto parsed = parse_log_level(text);
            if (!parsed)
                fail("logging.level", std::format(
                                          "unknown level '{}' (expected trace, debug, "
                                          "info, warn, error, critical or off)",
                                          text));
            result.logging.level = *parsed;
        }
        if (const auto* format = cnetmod::json::find(item, "format"))
        {
            const auto name = string_value(*format, "logging.format");
            if (name != "text" && name != "json")
                fail("logging.format",
                    std::format("unknown format '{}' (expected text or json)", name));
            result.logging.format = name == "json"
                ? logger::output_format::json
                : logger::output_format::text;
        }
    }

    void apply_http(application_configuration& result,
        const cnetmod::json::document& item)
    {
        constexpr std::string_view path = "http";
        require_known(item, path, {"address", "port", "max_connections",
                                      "request_timeout_ms", "sse", "request_ids",
                                      "access_logging", "recover_exceptions"});
        assign(item, path, "address", result.http.address);
        assign(item, path, "port", result.http.port);
        assign(item, path, "max_connections", result.http.max_connections);
        if (cnetmod::json::find(item, "request_timeout_ms") != nullptr)
        {
            std::chrono::milliseconds timeout{};
            assign_duration(item, path, "request_timeout_ms", timeout);
            result.http.request_timeout = timeout;
        }
        if (const auto* sse = cnetmod::json::find(item, "sse"))
        {
            constexpr std::string_view sse_path = "http.sse";
            require_known(*sse, sse_path,
                {"max_duration_ms", "write_timeout_ms"});
            assign_duration(*sse, sse_path, "max_duration_ms",
                result.http.sse_max_duration);
            assign_duration(*sse, sse_path, "write_timeout_ms",
                result.http.sse_write_timeout);
        }
        assign(item, path, "request_ids", result.http.request_ids);
        assign(item, path, "access_logging", result.http.access_logging);
        assign(item, path, "recover_exceptions", result.http.recover_exceptions);
    }

    void apply_management(application_configuration& result,
        const cnetmod::json::document& item)
    {
        constexpr std::string_view path = "management";
        require_known(item, path, {"enabled", "address", "port", "same_port",
                                      "live_path", "ready_path", "health_path",
                                      "metrics_path"});
        assign(item, path, "enabled", result.management.enabled);
        assign(item, path, "address", result.management.address);
        assign(item, path, "port", result.management.port);
        assign(item, path, "same_port", result.management.same_port);
        assign(item, path, "live_path", result.management.live_path);
        assign(item, path, "ready_path", result.management.ready_path);
        assign(item, path, "health_path", result.management.health_path);
        assign(item, path, "metrics_path", result.management.metrics_path);
    }

    void apply_observability(application_configuration& result,
        const cnetmod::json::document& item)
    {
        constexpr std::string_view path = "observability";
        require_known(item, path,
            {"tracing", "metrics", "logs", "sampling_ratio", "otlp"});
        assign(item, path, "tracing", result.observability.tracing);
        assign(item, path, "metrics", result.observability.metrics);
        assign(item, path, "logs", result.observability.logs);
        assign(item, path, "sampling_ratio", result.observability.sampling_ratio);
        const auto* otlp = cnetmod::json::find(item, "otlp");
        if (otlp == nullptr)
            return;
        constexpr std::string_view otlp_path = "observability.otlp";
        require_known(*otlp, otlp_path,
            {"traces_endpoint", "metrics_endpoint", "logs_endpoint",
                "service_name", "service_version", "service_namespace",
                "service_instance_id", "deployment_environment",
                "resource_attributes", "headers", "queue_capacity",
                "max_batch_size", "request_timeout_ms", "max_attempts",
                "initial_retry_delay_ms", "max_retry_delay_ms",
                "max_metric_instruments", "max_metric_attribute_sets",
                "capture_framework_logs"});
        auto& options = result.observability.otlp;
        assign(*otlp, otlp_path, "traces_endpoint", options.endpoint);
        assign(*otlp, otlp_path, "metrics_endpoint", options.metrics_endpoint);
        assign(*otlp, otlp_path, "logs_endpoint", options.logs_endpoint);
        assign(*otlp, otlp_path, "service_name", options.service_name);
        assign(*otlp, otlp_path, "service_version", options.service_version);
        assign(*otlp, otlp_path, "service_namespace", options.service_namespace);
        assign(*otlp, otlp_path, "service_instance_id",
            options.service_instance_id);
        assign(*otlp, otlp_path, "deployment_environment",
            options.deployment_environment);
        assign(*otlp, otlp_path, "resource_attributes",
            options.resource_attributes);
        assign(*otlp, otlp_path, "headers", options.headers);
        assign(*otlp, otlp_path, "queue_capacity", options.queue_capacity);
        assign(*otlp, otlp_path, "max_batch_size", options.max_batch_size);
        assign(*otlp, otlp_path, "max_metric_instruments",
            options.max_metric_instruments);
        assign(*otlp, otlp_path, "max_metric_attribute_sets",
            options.max_metric_attribute_sets);
        assign(*otlp, otlp_path, "capture_framework_logs",
            options.capture_framework_logs);
        assign_duration(*otlp, otlp_path, "request_timeout_ms",
            options.request_timeout);
        assign(*otlp, otlp_path, "max_attempts", options.max_attempts);
        assign_duration(*otlp, otlp_path, "initial_retry_delay_ms",
            options.initial_retry_delay);
        assign_duration(*otlp, otlp_path, "max_retry_delay_ms",
            options.max_retry_delay);
    }

    void apply_lifecycle(application_configuration& result,
        const cnetmod::json::document& item)
    {
        constexpr std::string_view path = "lifecycle";
        require_known(item, path,
            {"service_start_timeout_ms", "total_start_timeout_ms",
                "service_stop_timeout_ms", "total_stop_timeout_ms",
                "http_drain_timeout_ms", "telemetry_flush_timeout_ms"});
        auto& lifecycle = result.lifecycle;
        assign_duration(item, path, "service_start_timeout_ms",
            lifecycle.service_start_timeout);
        assign_duration(item, path, "total_start_timeout_ms",
            lifecycle.total_start_timeout);
        assign_duration(item, path, "service_stop_timeout_ms",
            lifecycle.service_stop_timeout);
        assign_duration(item, path, "total_stop_timeout_ms",
            lifecycle.total_stop_timeout);
        assign_duration(item, path, "http_drain_timeout_ms",
            lifecycle.http_drain_timeout);
        assign_duration(item, path, "telemetry_flush_timeout_ms",
            lifecycle.telemetry_flush_timeout);
    }

    void apply_health(application_configuration& result,
        const cnetmod::json::document& item)
    {
        constexpr std::string_view path = "health";
        require_known(item, path, {"interval_ms", "timeout_ms",
                                      "failures_before_down", "successes_before_up"});
        assign_duration(item, path, "interval_ms", result.health.interval);
        assign_duration(item, path, "timeout_ms", result.health.timeout);
        assign(item, path, "failures_before_down",
            result.health.failures_before_down);
        assign(item, path, "successes_before_up",
            result.health.successes_before_up);
    }

    void apply_orm(application_configuration& result,
        const cnetmod::json::document& item)
    {
        constexpr std::string_view path = "orm";
        require_known(item, path, {"sharding", "tenant_scope_required"});
        assign(item, path, "tenant_scope_required",
            result.orm.tenant_scope_required);
        const auto* sharding = cnetmod::json::find(item, "sharding");
        if (sharding == nullptr)
            return;
        constexpr std::string_view sharding_path = "orm.sharding";
        require_known(*sharding, sharding_path, {"enabled", "topologies"});
        assign(*sharding, sharding_path, "enabled", result.orm.sharding.enabled);
        const auto* topologies = cnetmod::json::find(*sharding, "topologies");
        if (topologies == nullptr)
            return;
        require_object(*topologies, "orm.sharding.topologies");
        for (const auto& [topology_name, topology_value] :
            topologies->get_object())
        {
            const auto topology_path =
                std::format("orm.sharding.topologies.{}", topology_name);
            require_known(topology_value, topology_path,
                {"logical_table", "table_count", "databases", "scatter_gather",
                    "distributed_transactions"});
            orm_shard_topology_configuration configured;
            configured.logical_table = topology_name;
            assign(topology_value, topology_path, "logical_table",
                configured.logical_table);
            if (const auto* count = cnetmod::json::find(
                    topology_value, "table_count"))
            {
                const auto count_path = join(topology_path, "table_count");
                if (!count->is_uint64() && !count->is_int64())
                    fail(count_path, "expected a positive integer");
                const auto raw = count->as<std::int64_t>();
                if (raw <= 0 ||
                    static_cast<std::uint64_t>(raw) >
                        std::numeric_limits<std::size_t>::max())
                    fail(count_path, "expected a positive integer");
                configured.table_count = static_cast<std::size_t>(raw);
            }
            assign(topology_value, topology_path, "databases",
                configured.databases);
            assign(topology_value, topology_path, "scatter_gather",
                configured.scatter_gather);
            assign(topology_value, topology_path, "distributed_transactions",
                configured.distributed_transactions);
            result.orm.sharding.topologies.insert_or_assign(
                topology_name, std::move(configured));
        }
    }

    void apply_security(application_configuration& result,
        const cnetmod::json::document& item)
    {
        require_known(item, "security", {"jwt"});
        const auto* jwt = cnetmod::json::find(item, "jwt");
        if (jwt == nullptr)
            return;
        constexpr std::string_view path = "security.jwt";
        require_known(*jwt, path, {"enabled", "issuer", "secret",
                                      "expires_in_seconds", "session_idle_seconds"});
        auto& configured = result.security.jwt;
        assign(*jwt, path, "enabled", configured.enabled);
        assign(*jwt, path, "issuer", configured.issuer);
        assign(*jwt, path, "secret", configured.secret);
        assign(*jwt, path, "expires_in_seconds", configured.expires_in_seconds);
        assign(*jwt, path, "session_idle_seconds",
            configured.session_idle_seconds);
    }

    void apply_services(application_configuration& result,
        const cnetmod::json::document& item)
    {
        require_object(item, "services");
        for (const auto& [name, value] : item.get_object())
        {
            const auto path = join("services", name);
            require_object(value, path);
            configured_service service{.name = name};
            assign(value, path, "type", service.name);
            assign(value, path, "enabled", service.enabled);
            assign(value, path, "instance", service.instance);
            if (const auto* required = cnetmod::json::find(value, "required"))
            {
                if (!required->is_boolean())
                    fail(join(path, "required"), "expected a boolean");
                service.requirement = required->get<bool>()
                    ? service_requirement::required
                    : service_requirement::optional;
            }
            if (const auto* recovery = cnetmod::json::find(value, "recovery"))
            {
                const auto recovery_path = join(path, "recovery");
                require_known(*recovery, recovery_path,
                    {"initial_delay_ms", "maximum_delay_ms", "budget_ms",
                        "multiplier", "jitter"});
                assign_duration(*recovery, recovery_path, "initial_delay_ms",
                    service.recovery.initial_delay);
                assign_duration(*recovery, recovery_path, "maximum_delay_ms",
                    service.recovery.maximum_delay);
                assign_duration(*recovery, recovery_path, "budget_ms",
                    service.recovery.budget);
                assign(*recovery, recovery_path, "multiplier",
                    service.recovery.multiplier);
                assign(*recovery, recovery_path, "jitter",
                    service.recovery.jitter);
            }
            for (const auto& [key, property] : value.get_object())
            {
                if (key != "enabled" && key != "type" && key != "instance" &&
                    key != "required" && key != "recovery")
                    service.properties[key] = property;
            }
            result.services.insert_or_assign(name, std::move(service));
        }
    }

    /**
     * @brief Applies one parsed document. Framework sections are validated
     *        strictly; every other top-level key is captured as an
     *        application section and claimed later by the options registry.
     */
    void apply_document(application_configuration& result,
        const cnetmod::json::document& root)
    {
        require_object(root, {});
        for (const auto& [key, value] : root.get_object())
        {
            if (key == "application")
                apply_application(result, value);
            else if (key == "logging")
                apply_logging(result, value);
            else if (key == "http")
                apply_http(result, value);
            else if (key == "management")
                apply_management(result, value);
            else if (key == "observability")
                apply_observability(result, value);
            else if (key == "crash_dump")
            {
                require_known(value, "crash_dump", {"directory"});
                assign(value, "crash_dump", "directory",
                    result.crash_dump.directory);
            }
            else if (key == "lifecycle")
                apply_lifecycle(result, value);
            else if (key == "health")
                apply_health(result, value);
            else if (key == "orm")
                apply_orm(result, value);
            else if (key == "security")
                apply_security(result, value);
            else if (key == "services")
                apply_services(result, value);
            else
                result.sections.insert_or_assign(key, value);
        }
    }

    template <class Unsigned>
    [[nodiscard]] auto environment_unsigned(std::string_view name,
        Unsigned minimum, Unsigned maximum) -> std::optional<Unsigned>
    {
        auto value = environment(name);
        if (!value)
            return std::nullopt;
        Unsigned parsed{};
        const auto [end, error] = std::from_chars(value->data(),
            value->data() + value->size(), parsed);
        if (error != std::errc{} || end != value->data() + value->size() ||
            parsed < minimum || parsed > maximum)
            fail(std::format("env:{}", name),
                std::format("expected an integer in [{}, {}], got '{}'",
                    minimum, maximum, *value));
        return parsed;
    }

    void apply_process_environment(application_configuration& result)
    {
        if (auto value = environment("CNETMOD_APPLICATION_NAME"))
            result.name = std::move(*value);
        if (auto parsed = environment_unsigned<unsigned>(
                "CNETMOD_CPU_THREADS", 1U, 1024U))
            result.execution.cpu_threads = *parsed;
        if (auto parsed = environment_unsigned<unsigned>(
                "CNETMOD_IO_THREADS", 1U, 1024U))
            result.execution.io_threads = *parsed;
        if (auto value = environment("CNETMOD_HTTP_ADDRESS"))
            result.http.address = std::move(*value);
        if (auto parsed = environment_unsigned<unsigned>(
                "CNETMOD_HTTP_PORT", 1U, 65535U))
            result.http.port = static_cast<std::uint16_t>(*parsed);
        if (auto value = environment("CNETMOD_LOG_LEVEL"))
        {
            const auto level = parse_log_level(*value);
            if (!level)
                fail("env:CNETMOD_LOG_LEVEL",
                    std::format("unknown level '{}'", *value));
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
                fail("env:CNETMOD_OTLP_CAPTURE_FRAMEWORK_LOGS",
                    std::format("expected true/false/1/0, got '{}'", *value));
            result.observability.otlp.capture_framework_logs = *enabled;
        }
    }

    auto nested_property(const cnetmod::json::document& root,
        std::string_view path) -> const cnetmod::json::document*
    {
        if (!root.is_object() || path.empty())
            return nullptr;
        const auto* current = &root;
        for (const auto segment : std::views::split(path, '.'))
        {
            if (!current->is_object())
                return nullptr;
            const auto name = std::string{segment.begin(), segment.end()};
            const auto* found = cnetmod::json::find(*current, name);
            if (found == nullptr)
                return nullptr;
            current = found;
        }
        return current;
    }

    [[nodiscard]] auto positive_duration(std::chrono::milliseconds duration) noexcept
        -> bool
    {
        const auto horizon = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::duration::max() / 2);
        return duration.count() > 0 && duration <= horizon;
    }

    void require_positive(std::chrono::milliseconds value, std::string path)
    {
        if (!positive_duration(value))
            fail(std::move(path), "expected a positive duration");
    }

    [[nodiscard]] auto valid_identifier(std::string_view identifier) noexcept
        -> bool
    {
        if (identifier.empty() || identifier.size() > 63)
            return false;
        const auto first = static_cast<unsigned char>(identifier.front());
        if (!(std::isalpha(first) || first == '_'))
            return false;
        return std::ranges::all_of(identifier, [](const char character)
            {
                const auto byte = static_cast<unsigned char>(character);
                return std::isalnum(byte) || byte == '_';
            });
    }

    /**
     * @brief Throwing validation core shared by load and reload.
     */
    void check_configuration(const application_configuration& value)
    {
        if (value.name.empty())
            fail("application.name", "must not be empty");
        if (value.crash_dump.directory.empty())
            fail("crash_dump.directory", "must not be empty");
        if (value.execution.cpu_threads == 0U || value.execution.cpu_threads > 1024U)
            fail("application.cpu_threads", "expected a value in [1, 1024]");
        if (value.execution.io_threads == 0U || value.execution.io_threads > 1024U)
            fail("application.io_threads", "expected a value in [1, 1024]");
        if (value.http.address.empty())
            fail("http.address", "must not be empty");
        if (value.http.port == 0U)
            fail("http.port", "must be in [1, 65535]");
        if (value.http.request_timeout)
            require_positive(*value.http.request_timeout, "http.request_timeout_ms");
        require_positive(value.http.sse_max_duration, "http.sse.max_duration_ms");
        require_positive(value.http.sse_write_timeout, "http.sse.write_timeout_ms");
        const auto ratio = value.observability.sampling_ratio;
        if (!std::isfinite(ratio) || ratio < 0.0 || ratio > 1.0)
            fail("observability.sampling_ratio", "expected a value in [0, 1]");
        const auto& lifecycle = value.lifecycle;
        require_positive(lifecycle.service_start_timeout,
            "lifecycle.service_start_timeout_ms");
        require_positive(lifecycle.total_start_timeout,
            "lifecycle.total_start_timeout_ms");
        require_positive(lifecycle.service_stop_timeout,
            "lifecycle.service_stop_timeout_ms");
        require_positive(lifecycle.total_stop_timeout,
            "lifecycle.total_stop_timeout_ms");
        require_positive(lifecycle.http_drain_timeout,
            "lifecycle.http_drain_timeout_ms");
        require_positive(lifecycle.telemetry_flush_timeout,
            "lifecycle.telemetry_flush_timeout_ms");
        require_positive(value.health.interval, "health.interval_ms");
        require_positive(value.health.timeout, "health.timeout_ms");
        if (value.health.failures_before_down == 0U)
            fail("health.failures_before_down", "must be positive");
        if (value.health.successes_before_up == 0U)
            fail("health.successes_before_up", "must be positive");
        const auto& otlp = value.observability.otlp;
        if (otlp.queue_capacity < 2U)
            fail("observability.otlp.queue_capacity", "must be at least 2");
        if (otlp.max_metric_instruments == 0U)
            fail("observability.otlp.max_metric_instruments", "must be positive");
        if (otlp.max_batch_size == 0U || otlp.max_batch_size > otlp.queue_capacity)
            fail("observability.otlp.max_batch_size",
                "must be positive and not exceed queue_capacity");
        if (otlp.max_attempts == 0U)
            fail("observability.otlp.max_attempts", "must be positive");
        require_positive(otlp.request_timeout,
            "observability.otlp.request_timeout_ms");
        if (otlp.initial_retry_delay.count() < 0)
            fail("observability.otlp.initial_retry_delay_ms",
                "must not be negative");
        if (otlp.max_retry_delay < otlp.initial_retry_delay)
            fail("observability.otlp.max_retry_delay_ms",
                "must not be shorter than initial_retry_delay_ms");
        if (value.management.enabled && value.management.address.empty())
            fail("management.address", "must not be empty when enabled");
        if (value.management.enabled && value.management.port == 0U)
            fail("management.port", "must be in [1, 65535] when enabled");
        const auto& jwt = value.security.jwt;
        if (jwt.enabled)
        {
            if (jwt.issuer.empty())
                fail("security.jwt.issuer", "must not be empty when enabled");
            if (jwt.secret.size() < 32U)
                fail("security.jwt.secret",
                    "must contain at least 32 bytes when enabled");
            if (jwt.expires_in_seconds <= 0 || jwt.expires_in_seconds > 2592000)
                fail("security.jwt.expires_in_seconds",
                    "expected a value in [1, 2592000]");
            if (jwt.session_idle_seconds <= 0 ||
                jwt.session_idle_seconds > jwt.expires_in_seconds)
                fail("security.jwt.session_idle_seconds",
                    "expected a value in [1, expires_in_seconds]");
        }
        std::unordered_set<service_key, service_key_hash> service_keys;
        for (const auto& [name, service] : value.services)
        {
            const auto path = join("services", name);
            if (name.empty())
                fail("services", "service names must not be empty");
            if (service.name.empty())
                fail(join(path, "type"), "must not be empty");
            if (service.instance.empty())
                fail(join(path, "instance"), "must not be empty");
            if (!valid_recovery_policy(service.recovery) ||
                !positive_duration(service.recovery.budget))
                fail(join(path, "recovery"), "invalid recovery policy");
            if (!service_keys.emplace(service_key{service.name, service.instance})
                     .second)
                fail(path,
                    std::format("duplicate service {}:{}", service.name,
                        service.instance),
                    std::errc::file_exists);
        }
        if (value.orm.sharding.enabled && value.orm.sharding.topologies.empty())
            fail("orm.sharding.topologies", "must not be empty when enabled");
        for (const auto& [name, topology] : value.orm.sharding.topologies)
        {
            const auto path = std::format("orm.sharding.topologies.{}", name);
            if (topology.logical_table.empty() || topology.table_count == 0 ||
                topology.databases.empty())
                fail(path, "logical_table, table_count and databases are required");
            const auto suffix_width = std::max<std::size_t>(2,
                std::to_string(topology.table_count - 1).size());
            if (!valid_identifier(topology.logical_table) ||
                topology.logical_table.size() + 1 + suffix_width > 63)
                fail(join(path, "logical_table"),
                    "must be a SQL identifier short enough for shard suffixes");
            std::unordered_set<std::string> databases;
            for (const auto& database : topology.databases)
            {
                if (database.empty() || !databases.emplace(database).second)
                    fail(join(path, "databases"),
                        "entries must be non-empty and unique");
            }
        }
        for (const auto& [name, unused] : value.sections)
        {
            (void)unused;
            if (std::ranges::find(framework_sections, name) !=
                framework_sections.end())
                fail(name, "reserved framework section name");
        }
    }

    template <class Function>
    auto guarded(Function&& function)
        -> std::expected<std::invoke_result_t<Function>, configuration_error>
    {
        try
        {
            if constexpr (std::is_void_v<std::invoke_result_t<Function>>)
            {
                std::forward<Function>(function)();
                return {};
            }
            else
                return std::forward<Function>(function)();
        }
        catch (configuration_failure& failure)
        {
            // The failure object already owns every diagnostic string. Moving
            // it keeps this handler allocation-free, which matters when the
            // original failure is observed while allocation itself is being
            // fault-injected.
            return std::unexpected(std::move(failure.error));
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(configuration_error{
                .code = std::make_error_code(std::errc::not_enough_memory),
                .message = "out of memory while processing configuration"});
        }
        catch (const std::exception& error)
        {
            return std::unexpected(configuration_error{
                .code = std::make_error_code(std::errc::io_error),
                .message = error.what()});
        }
    }

} // namespace

auto configuration_error::describe() const -> std::string
{
    if (path.empty())
        return message;
    return std::format("{}: {}", path, message);
}

auto expand_environment_references(std::string_view source)
    -> std::expected<std::string, configuration_error>
{
    return guarded([source] { return expand_string(source, {}); });
}

auto configured_service::string_property(std::string_view path) const
    -> std::expected<std::optional<std::string>, std::error_code>
{
    const auto* value = nested_property(properties, path);
    if (value == nullptr)
        return std::optional<std::string>{};
    if (!value->is_string())
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    return std::optional<std::string>{value->get<std::string>()};
}

auto configured_service::integer_property(std::string_view path) const
    -> std::expected<std::optional<std::int64_t>, std::error_code>
{
    const auto* value = nested_property(properties, path);
    if (value == nullptr)
        return std::optional<std::int64_t>{};
    if (!value->is_int64() && !value->is_uint64())
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    return std::optional<std::int64_t>{value->as<std::int64_t>()};
}

auto configured_service::string_array_property(std::string_view path) const
    -> std::expected<std::optional<std::vector<std::string>>,
        std::error_code>
{
    const auto* value = nested_property(properties, path);
    if (value == nullptr)
        return std::optional<std::vector<std::string>>{};
    if (!value->is_array())
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));

    std::vector<std::string> result;
    result.reserve(value->size());
    for (const auto& item : value->get_array())
    {
        if (!item.is_string())
            return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
        result.push_back(item.get<std::string>());
    }
    return std::optional<std::vector<std::string>>{std::move(result)};
}

void configured_service::set_property(std::string name, std::string value)
{
    properties[std::move(name)] = std::move(value);
}

void configured_service::set_property(std::string name, std::int64_t value)
{
    properties[std::move(name)] = value;
}

void configured_service::set_property(std::string name, bool value)
{
    properties[std::move(name)] = value;
}

auto load_configuration(const std::optional<std::filesystem::path>& file)
    -> std::expected<application_configuration, configuration_error>
{
    return guarded([&file]
        {
            application_configuration result;
            if (file)
            {
                auto document = read_document(*file);
                const document_cleanup cleanup{document};
                expand_tree(document, {});
                apply_document(result, document);
            }
            apply_process_environment(result);
            if (result.observability.otlp.service_name.empty() ||
                result.observability.otlp.service_name == "cnetmod")
                result.observability.otlp.service_name = result.name;
            check_configuration(result);
            return result;
        });
}

auto validate_configuration(const application_configuration& value)
    -> std::expected<void, configuration_error>
{
    return guarded([&value] { check_configuration(value); });
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
    for (const auto& [name, section] : candidate.sections)
    {
        const auto found = active.sections.find(name);
        if (found == active.sections.end() ||
            !cnetmod::json::equivalent(found->second, section))
            result.changed_sections.push_back(name);
    }
    for (const auto& [name, unused] : active.sections)
    {
        (void)unused;
        if (!candidate.sections.contains(name))
            result.changed_sections.push_back(name);
    }
    // Section publication is decided by the options registry; the snapshot
    // always tracks the candidate so a later reload compares against it.
    active.sections = candidate.sections;
    result.applied = !result.changed.empty();
    result.restart_required = active.name != candidate.name ||
        active.execution != candidate.execution ||
        active.install_signal_handlers != candidate.install_signal_handlers ||
        active.crash_dump.directory != candidate.crash_dump.directory ||
        active.logging.manage_lifecycle != candidate.logging.manage_lifecycle ||
        active.logging.format != candidate.logging.format ||
        active.http.address != candidate.http.address ||
        active.http.port != candidate.http.port ||
        active.http.max_connections != candidate.http.max_connections ||
        active.http.request_timeout != candidate.http.request_timeout ||
        active.http.sse_max_duration != candidate.http.sse_max_duration ||
        active.http.sse_write_timeout != candidate.http.sse_write_timeout ||
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
        active.orm != candidate.orm ||
        active.security != candidate.security ||
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
                !cnetmod::json::equivalent(
                    service.properties, found->second.properties))
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
    -> std::expected<configuration_reload_result, configuration_error>
{
    return guarded([&active, &candidate]
        {
            check_configuration(candidate);
            auto staged = active;
            auto result = prepare_configuration_reload(staged, candidate);
            static_assert(std::is_nothrow_move_assignable_v<application_configuration>);
            static_assert(std::is_nothrow_move_constructible_v<configuration_reload_result>);
            active = std::move(staged);
            return result;
        });
}

auto redact_configuration(const cnetmod::json::document& value) -> cnetmod::json::document
{
    if (value.is_array())
    {
        auto result = cnetmod::json::array();
        for (const auto& child : value.get_array())
            result.get_array().push_back(redact_configuration(child));
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
    auto result = cnetmod::json::object();
    for (const auto& [key, child] : value.get_object())
    {
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
        result[key] = secret ? cnetmod::json::document("[REDACTED]")
                             : redact_configuration(child);
    }
    return result;
}

} // namespace cnetmod::application
