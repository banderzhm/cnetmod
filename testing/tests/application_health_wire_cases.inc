TEST(application_health_outcomes_reach_the_http_collector)
{
    namespace app = cnetmod::application;

    struct probe_service final : app::managed_service
    {
        std::error_code error;
        bool stall_stop = false;
        bool stop_cancelled = false;
        bool down = true;
        unsigned starts = 0;
        unsigned stops = 0;
        unsigned probes = 0;

        auto key() const -> app::service_key override
        {
            return {"wire-probe"};
        }

        auto requirement() const noexcept -> app::service_requirement override
        {
            return app::service_requirement::required;
        }

        auto start(app::service_context&) -> cnetmod::task<std::expected<void, std::error_code>> override
        {
            ++starts;
            if (starts == 2)
                co_return std::unexpected(std::make_error_code(std::errc::permission_denied));
            co_return std::expected<void, std::error_code>{};
        }

        auto recovery() const noexcept -> app::recovery_policy override
        {
            return {.initial_delay = std::chrono::milliseconds{1},
                .maximum_delay = std::chrono::milliseconds{1},
                .budget = std::chrono::seconds{2},
                .jitter = 0.0};
        }

        auto stop(app::service_context& context) -> cnetmod::task<std::expected<void, std::error_code>> override
        {
            ++stops;
            if (stall_stop)
            {
                const auto waited = co_await cnetmod::async_timer_wait(context.io,
                    std::chrono::seconds{10}, context.cancellation);
                stop_cancelled = context.cancellation.is_cancelled() && !waited;
                co_return waited;
            }
            co_return std::expected<void, std::error_code>{};
        }

        auto probe(app::service_context&) -> cnetmod::task<app::health_report> override
        {
            ++probes;
            co_return app::health_report{.status = down ? app::service_health::down : app::service_health::up,
                .message = "private-probe-diagnostic",
                .error = error};
        }
    };

    auto run_mode = [&](bool enabled)
    {
        cnetmod::net_init network;
        auto io = cnetmod::make_io_context();
        std::vector<cnetmod::json::document> spans;
        std::vector<cnetmod::json::document> logs;
        unsigned requests = 0;
        std::map<std::pair<std::string, std::string>, double> operation_totals;
        cnetmod::http::router routes;
        routes.post("/v1/:signal", [&](cnetmod::http::request_context& request) -> cnetmod::task<void>
            {
                ++requests;
                const auto body = std::string{co_await request.read_full_body()};
                ASSERT_FALSE(body.contains("private-probe-diagnostic"));
                const auto document = cnetmod::json::document::parse(body);
                if (request.path() == "/v1/traces")
                    for (const auto& resource : document.at("resourceSpans"))
                        for (const auto& scope : resource.at("scopeSpans"))
                            for (const auto& span : scope.at("spans"))
                                spans.push_back(span);
                else if (request.path() == "/v1/logs")
                    for (const auto& resource : document.at("resourceLogs"))
                        for (const auto& scope : resource.at("scopeLogs"))
                            for (const auto& record : scope.at("logRecords"))
                                logs.push_back(record);
                else
                    for (const auto& resource : document.at("resourceMetrics"))
                        for (const auto& scope : resource.at("scopeMetrics"))
                            for (const auto& metric : scope.at("metrics"))
                            {
                                if (metric.at("name") != "application.service.operations")
                                    continue;
                                const auto& sum = metric.at("sum");
                                ASSERT_EQ(sum.at("aggregationTemporality").get<std::string>(), "AGGREGATION_TEMPORALITY_CUMULATIVE");
                                ASSERT_TRUE(sum.at("isMonotonic").get<bool>());
                                for (const auto& point : sum.at("dataPoints"))
                                {
                                    std::map<std::string, std::string> labels;
                                    for (const auto& attribute : point.at("attributes"))
                                        ASSERT_TRUE(labels.emplace(attribute.at("key").get<std::string>(),
                                                              attribute.at("value").at("stringValue").get<std::string>())
                                                .second);
                                    ASSERT_EQ(labels.at("service.name"), "wire-probe");
                                    ASSERT_EQ(labels.at("service.instance"), "default");
                                    const auto value = point.at("asDouble").get<double>();
                                    auto& previous = operation_totals[{labels.at("operation"), labels.at("outcome")}];
                                    ASSERT_TRUE(value >= previous);
                                    previous = value;
                                }
                            }
                request.json(cnetmod::http::status::ok, "{}");
            });
        cnetmod::http::server collector{*io};
        collector.set_router(std::move(routes));
        ASSERT_TRUE(collector.listen("127.0.0.1", 19435).has_value());
        cnetmod::observability::telemetry_hub telemetry{*io,
            {.endpoint = "http://127.0.0.1:19435/v1/traces",
                .export_traces = enabled,
                .export_metrics = enabled,
                .export_logs = enabled}};
        app::task_supervisor supervisor{*io};
        app::health_registry health{{.failures_before_down = 1, .successes_before_up = 1}};
        auto service = std::make_shared<probe_service>();
        ASSERT_TRUE(health.add(service).has_value());
        app::service_registry services;
        ASSERT_TRUE(services.manage(service).has_value());
        services.freeze();
        app::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health,
            {.service_stop_timeout = std::chrono::milliseconds{10}}};
        cnetmod::cancel_token cancellation;
        app::service_context context{*io, telemetry, supervisor, cancellation, {}};
        const std::array errors{std::make_error_code(std::errc::operation_canceled),
            cnetmod::make_error_code(cnetmod::errc::operation_aborted),
            std::make_error_code(std::errc::timed_out),
            std::make_error_code(std::errc::permission_denied), std::error_code{}, std::error_code{}};
        auto accept = collector.run();
        accept.handle().resume();
        auto exercise = [&]() -> cnetmod::task<void>
        {
            for (std::size_t index = 0; index != errors.size(); ++index)
            {
                service->error = errors[index];
                service->down = index != 5;
                co_await health.refresh(context);
            }
            ASSERT_TRUE((co_await lifecycle.start()).has_value());
            ASSERT_TRUE(lifecycle.schedule_recovery(service->key()).has_value());
            ASSERT_TRUE((co_await supervisor.join()).has_value());
            service->stall_stop = true;
            const auto failed_stop = co_await lifecycle.stop();
            ASSERT_FALSE(failed_stop.has_value());
            ASSERT_EQ(failed_stop.error(), std::make_error_code(std::errc::timed_out));
            ASSERT_TRUE(service->stop_cancelled);
            ASSERT_EQ(lifecycle.started_services().size(), 1U);
            service->stall_stop = false;
            ASSERT_TRUE((co_await lifecycle.stop()).has_value());
            ASSERT_TRUE(lifecycle.started_services().empty());
            ASSERT_TRUE((co_await telemetry.shutdown(std::chrono::seconds{2}, std::chrono::milliseconds{500})).has_value());
            collector.stop();
            const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{1};
            while ((!accept.handle().done() || collector.active_connections() != 0) &&
                std::chrono::steady_clock::now() < limit)
                (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
            ASSERT_TRUE(accept.handle().done());
            ASSERT_EQ(collector.active_connections(), 0U);
            io->stop();
        };
        auto work = exercise();
        work.handle().resume();
        io->run();
        ASSERT_TRUE(work.handle().done());
        work.handle().promise().result();
        ASSERT_EQ(service->starts, 3U);
        ASSERT_EQ(service->stops, 2U);
        ASSERT_EQ(service->probes, 7U);
        if (!enabled)
        {
            ASSERT_EQ(requests, 0U);
            ASSERT_TRUE(spans.empty());
            ASSERT_TRUE(logs.empty());
            ASSERT_TRUE(operation_totals.empty());
            ASSERT_EQ(telemetry.statistics().accepted, 0U);
            ASSERT_FALSE(telemetry.metrics().render_openmetrics().contains("application_"));
            return;
        }
        ASSERT_TRUE(requests >= 3U);
        ASSERT_EQ(spans.size(), errors.size() + 5U);
        ASSERT_EQ(logs.size(), 5U);
        ASSERT_EQ(operation_totals.size(), 5U);
        for (const auto& key : {std::pair{std::string{"start"}, std::string{"success"}},
                 std::pair{std::string{"recover"}, std::string{"success"}},
                 std::pair{std::string{"recover"}, std::string{"failure"}},
                 std::pair{std::string{"stop"}, std::string{"failure"}},
                 std::pair{std::string{"stop"}, std::string{"success"}}})
            ASSERT_EQ(operation_totals.at(key), 1.0);
        const std::array expected{"cancelled", "cancelled", "timeout", "error", "error", "success"};
        for (std::size_t index = 0; index < std::min(spans.size(), errors.size()); ++index)
        {
            const auto& span = spans[index];
            ASSERT_EQ(span.at("name").get<std::string>(), "application.health.probe");
            ASSERT_EQ(span.at("kind").get<std::string>(), "SPAN_KIND_INTERNAL");
            std::map<std::string, std::string> attributes;
            for (const auto& attribute : span.at("attributes"))
                attributes.emplace(attribute.at("key").get<std::string>(), attribute.at("value").at("stringValue").get<std::string>());
            ASSERT_EQ(attributes.at("cnetmod.operation.status"), expected[index]);
            if (errors[index])
                ASSERT_EQ(attributes.at("cnetmod.error.code"), std::to_string(errors[index].value()));
            if (index >= 2 && index <= 4)
                ASSERT_EQ(span.at("status").at("code").get<std::string>(), "STATUS_CODE_ERROR");
            else
                ASSERT_FALSE(span.contains("status"));
        }
        const std::array phases{"start", "recover", "recover", "stop", "stop"};
        const std::array outcomes{"success", "error", "success", "timeout", "success"};
        for (std::size_t index = 0; index < phases.size() && errors.size() + index < spans.size(); ++index)
        {
            const auto& span = spans[errors.size() + index];
            ASSERT_EQ(span.at("name").get<std::string>(), std::format("application.service.{}", phases[index]));
            ASSERT_EQ(span.at("kind").get<std::string>(), "SPAN_KIND_INTERNAL");
            if (index == 1 || index == 3)
                ASSERT_EQ(span.at("status").at("code").get<std::string>(), "STATUS_CODE_ERROR");
            else
                ASSERT_FALSE(span.contains("status"));
            std::map<std::string, std::string> attributes;
            for (const auto& attribute : span.at("attributes"))
                attributes.emplace(attribute.at("key").get<std::string>(), attribute.at("value").at("stringValue").get<std::string>());
            ASSERT_EQ(attributes.at("service.name"), "wire-probe");
            ASSERT_EQ(attributes.at("service.instance"), "default");
            ASSERT_EQ(attributes.at("cnetmod.operation.status"), outcomes[index]);
            if (index == 1 || index == 3)
                ASSERT_EQ(attributes.at("cnetmod.error.code"), std::to_string(std::make_error_code(index == 1 ? std::errc::permission_denied : std::errc::timed_out).value()));
            if (index < logs.size())
            {
                ASSERT_EQ(logs[index].at("traceId"), span.at("traceId"));
                ASSERT_EQ(logs[index].at("spanId"), span.at("spanId"));
                ASSERT_EQ(logs[index].at("severityText").get<std::string>(), index == 1 || index == 3 ? "ERROR" : "INFO");
            }
        }
    };
    run_mode(true);
    run_mode(false);
}
