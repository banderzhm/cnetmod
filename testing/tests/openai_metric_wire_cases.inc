TEST(application_openai_metrics_reach_otlp_and_local_registry_once)
{
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    constexpr std::uint16_t port = 19439;
    std::vector<std::string> payloads;
    cnetmod::http::router routes;
    routes.post("/v1/metrics", [&](cnetmod::http::request_context& request) -> cnetmod::task<void>
        {
            payloads.emplace_back(co_await request.read_full_body());
            request.json(cnetmod::http::status::ok, "{}");
        });
    cnetmod::http::server collector{*io};
    collector.set_router(std::move(routes));
    const auto listening = collector.listen("127.0.0.1", port);
    ASSERT_TRUE(listening.has_value());
    if (!listening)
        return;
    auto accept_loop = collector.run();
    accept_loop.handle().resume();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.metrics_endpoint = std::format("http://127.0.0.1:{}/v1/metrics", port),
            .export_traces = false,
            .export_metrics = true,
            .export_logs = false}};
    cnetmod::application::openai_service service{*io, telemetry, {}, "default",
        cnetmod::application::service_requirement::required, {}};
    auto configuration = service.run_configuration({.run_id = "private-run"});
    ASSERT_EQ(configuration.listeners.size(), std::size_t{1});
    auto run = [&]() -> cnetmod::task<void>
    {
        configuration.notify({.type = cnetmod::openai::run_event_type::model_start,
            .name = "model",
            .detail = "private-prompt"});
        configuration.notify({.type = cnetmod::openai::run_event_type::model_end,
            .name = "model",
            .detail = "private-response",
            .attributes = {{"input_tokens", 100}, {"output_tokens", 25}}});
        ASSERT_EQ(telemetry.statistics().accepted_metrics, std::uint64_t{7});
        ASSERT_EQ(telemetry.statistics().accepted_spans, std::uint64_t{0});
        const auto flushed = co_await telemetry.flush(std::chrono::seconds{2});
        ASSERT_TRUE(flushed.has_value());
        const auto settled = co_await telemetry.shutdown(std::chrono::seconds{1}, std::chrono::seconds{1});
        ASSERT_TRUE(settled.has_value());
        collector.stop();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        while ((!accept_loop.handle().done() || collector.active_connections() != 0) &&
            std::chrono::steady_clock::now() < deadline)
            (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{1});
        ASSERT_TRUE(accept_loop.handle().done());
        ASSERT_EQ(collector.active_connections(), 0U);
        io->stop();
    };
    cnetmod::spawn(*io, run());
    io->run();
    ASSERT_EQ(payloads.size(), std::size_t{1});
    if (payloads.empty())
        return;
    const auto parsed = cnetmod::json::parse_document(payloads.front()).value();
    double tokens = 0;
    unsigned durations = 0;
    for (const auto& metric : parsed["resourceMetrics"][0]["scopeMetrics"][0]["metrics"].get_array())
    {
        if (metric["name"].get<std::string>() == "gen_ai_client_tokens")
            for (const auto& point : metric["sum"]["dataPoints"].get_array())
                tokens += point["asDouble"].as<double>();
        if (metric["name"].get<std::string>() == "gen_ai_client_operation_duration")
        {
            ++durations;
            ASSERT_EQ(metric["histogram"]["dataPoints"][0]["count"].get<std::string>(), "1");
        }
    }
    ASSERT_EQ(tokens, 125.0);
    ASSERT_EQ(durations, 1U);
    ASSERT_FALSE(payloads.front().contains("private-run"));
    ASSERT_FALSE(payloads.front().contains("private-prompt"));
    ASSERT_FALSE(payloads.front().contains("private-response"));
    const auto local = telemetry.metrics().render_openmetrics();
    ASSERT_TRUE(local.contains("gen_ai_client_tokens_total{operation=\"chat\",token_type=\"input\"} 100"));
    ASSERT_TRUE(local.contains("gen_ai_client_tokens_total{operation=\"chat\",token_type=\"output\"} 25"));
}
