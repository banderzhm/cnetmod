static void verify_redis_host_cleanup(bool observation_enabled)
{
    struct fixture final : application::managed_service
    {
        std::uint16_t port{};
        std::unique_ptr<application::redis_service> service;
        std::optional<cnetmod::redis::pooled_connection> retained;
        unsigned starts{};

        auto key() const -> application::service_key override
        {
            return {"retained-redis"};
        }

        auto requirement() const noexcept -> application::service_requirement override
        {
            return application::service_requirement::required;
        }

        auto start(application::service_context& context)
            -> cnetmod::task<std::expected<void, std::error_code>> override
        {
            ++starts;
            service = std::make_unique<application::redis_service>(context.io,
                cnetmod::redis::pool_params{.host = "127.0.0.1", .port = port, .resp3 = false, .initial_size = 1, .max_size = 1, .ping_interval = std::chrono::hours{1}},
                "fixture", application::service_requirement::required,
                application::recovery_policy{});
            auto started = co_await service->start(context);
            if (!started)
                co_return started;
            auto lease = co_await service->pool().async_get_connection(context.cancellation);
            if (!lease)
                co_return std::unexpected(lease.error());
            retained.emplace(std::move(*lease));
            co_return std::expected<void, std::error_code>{};
        }

        auto stop(application::service_context& context)
            -> cnetmod::task<std::expected<void, std::error_code>> override
        {
            co_return co_await service->stop(context);
        }

        auto probe(application::service_context& context)
            -> cnetmod::task<application::health_report> override
        {
            co_return co_await service->probe(context);
        }
    };

    cnetmod::net_init network;
    auto peer = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    auto occupied = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(peer.has_value() && occupied.has_value());
    ASSERT_TRUE(peer->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(peer->listen().has_value());
    ASSERT_TRUE(occupied->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    ASSERT_TRUE(occupied->listen().has_value());
    auto probe_io = cnetmod::make_io_context();
    cnetmod::http::server bind_probe{*probe_io};
    const auto bind_result = bind_probe.listen("127.0.0.1", occupied->local_endpoint()->port());
    ASSERT_FALSE(bind_result.has_value());
    auto collector = cnetmod::socket::create(cnetmod::address_family::ipv4, cnetmod::socket_type::stream);
    ASSERT_TRUE(collector.has_value());
    ASSERT_TRUE(collector->bind({cnetmod::ipv4_address::loopback(), 0}).has_value());
    auto held = std::make_shared<fixture>();
    held->port = peer->local_endpoint()->port();
    auto host = application::application_builder{"redis-cleanup"}
                    .configure([&](application::application_configuration& value)
                        {
                            value.http.address = "127.0.0.1";
                            value.http.port = occupied->local_endpoint()->port();
                            value.management.enabled = false;
                            value.logging.manage_lifecycle = false;
                            value.install_signal_handlers = false;
                            value.observability.tracing = observation_enabled;
                            value.observability.metrics = observation_enabled;
                            value.observability.logs = observation_enabled;
                            if (observation_enabled)
                            {
                                value.observability.otlp.endpoint = std::format(
                                    "http://127.0.0.1:{}/v1/traces", collector->local_endpoint()->port());
                                value.observability.otlp.request_timeout = std::chrono::milliseconds{20};
                                value.observability.otlp.max_attempts = 1;
                            }
                            value.lifecycle.total_stop_timeout = std::chrono::milliseconds{100};
                        })
                    .service(held)
                    .build();
    ASSERT_TRUE(host.has_value());
    if (!host)
        return;
    const auto result = host->run();
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), bind_result.error());
    ASSERT_TRUE(host->state() == application::application_state::cleanup_failed);
    ASSERT_TRUE(held->retained.has_value());
    ASSERT_EQ(held->service->pool().checked_out_count(), 1U);
    held->retained.reset();
    ASSERT_TRUE(host->retry_cleanup(std::chrono::seconds{1}).has_value());
    ASSERT_TRUE(host->state() == application::application_state::stopped);
    ASSERT_EQ(held->service->pool().checked_out_count(), 0U);
    ASSERT_EQ(held->service->pool().pending_maintenance(), 0);
    ASSERT_EQ(held->starts, 1U);
    ASSERT_TRUE(host->retry_cleanup(std::chrono::seconds{1}).has_value());
    ASSERT_EQ(held->starts, 1U);
    const auto statistics = host->telemetry().statistics();
    if (observation_enabled)
    {
        ASSERT_TRUE(statistics.accepted_spans > 0);
        ASSERT_TRUE(statistics.accepted_metrics > 0);
        ASSERT_TRUE(statistics.accepted_logs > 0);
        ASSERT_TRUE(statistics.failed_batches > 0);
        ASSERT_EQ(statistics.exported, 0U);
    }
    else
        ASSERT_EQ(statistics.accepted, 0U);
}

TEST(application_redis_host_retries_cleanup_after_lease_return)
{
    verify_redis_host_cleanup(false);
}

TEST(application_redis_host_collector_failure_preserves_cleanup_and_business_error)
{
    verify_redis_host_cleanup(true);
}
