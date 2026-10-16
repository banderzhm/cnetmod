TEST(configuration_reload_allocation_failure_preserves_active_snapshot)
{
    namespace application = cnetmod::application;
    application::application_configuration original;
    original.services.emplace("service", application::configured_service{.name = "custom"});
    auto candidate = original;
    candidate.observability.sampling_ratio = 0.25;
    candidate.health.interval = std::chrono::milliseconds{1234};
    candidate.services.at("service").recovery.budget = std::chrono::milliseconds{9876};
    candidate.http.port = 9999;
    unsigned failures = 0;
    unsigned successes = 0;
    for (std::size_t allocation = 0; allocation != 64; ++allocation)
    {
        auto active = original;
        fail_after = allocation;
        auto result = application::reload_safe_configuration(active, candidate);
        fail_after.reset();
        if (!result)
        {
            ++failures;
            ASSERT_EQ(result.error().code, std::make_error_code(std::errc::not_enough_memory));
            ASSERT_EQ(active.observability.sampling_ratio, original.observability.sampling_ratio);
            ASSERT_TRUE(active.health.interval == original.health.interval);
            ASSERT_TRUE(active.services.at("service").recovery.budget == original.services.at("service").recovery.budget);
        }
        else
        {
            ++successes;
            ASSERT_TRUE(result->applied);
            ASSERT_TRUE(result->restart_required);
            ASSERT_EQ(active.observability.sampling_ratio, 0.25);
            ASSERT_TRUE(active.health.interval == candidate.health.interval);
            ASSERT_TRUE(active.services.at("service").recovery.budget == candidate.services.at("service").recovery.budget);
        }
        ASSERT_EQ(active.http.port, original.http.port);
    }
    ASSERT_TRUE(failures > 0);
    ASSERT_TRUE(successes > 0);
    auto active = original;
    candidate.observability.sampling_ratio = std::numeric_limits<double>::quiet_NaN();
    auto rejected = application::reload_safe_configuration(active, candidate);
    ASSERT_FALSE(rejected.has_value());
    ASSERT_EQ(rejected.error().code, std::make_error_code(std::errc::invalid_argument));
    ASSERT_EQ(active.observability.sampling_ratio, original.observability.sampling_ratio);
}

TEST(configuration_reload_preserves_recovery_for_replaced_service_identity)
{
    namespace application = cnetmod::application;
    for (const bool change_type : {false, true})
    {
        application::application_configuration active;
        active.services.emplace("binding", application::configured_service{.name = "original"});
        auto candidate = active;
        auto& replacement = candidate.services.at("binding");
        if (change_type)
            replacement.name = "replacement";
        else
            replacement.instance = "replacement";
        const auto original_budget = active.services.at("binding").recovery.budget;
        replacement.recovery.budget = std::chrono::milliseconds{1234};
        const auto result = application::reload_safe_configuration(active, candidate);
        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result->restart_required);
        ASSERT_FALSE(result->applied);
        ASSERT_TRUE(active.services.at("binding").recovery.budget == original_budget);
        ASSERT_EQ(active.services.at("binding").name, std::string{"original"});
    }
}

TEST(recovery_policy_batch_failure_preserves_every_override)
{
    namespace app = cnetmod::application;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io,
        {.export_traces = false, .export_metrics = false, .export_logs = false}};
    app::service_registry services;
    app::task_supervisor supervisor{*io};
    app::health_registry health;
    const app::service_key first{"database", std::string(128, 'a')};
    const app::service_key second{"database", std::string(128, 'b')};
    app::recovery_policy original;
    auto changed = original;
    changed.budget = std::chrono::milliseconds{4567};
    const std::array<std::pair<app::service_key, app::recovery_policy>, 1> initial{{{first, original}}};
    const std::array<std::pair<app::service_key, app::recovery_policy>, 2> updates{{{first, changed}, {second, changed}}};
    unsigned failures = 0;
    unsigned successes = 0;
    for (std::size_t allocation = 0; allocation != 32; ++allocation)
    {
        app::service_lifecycle lifecycle{*io, telemetry, services, supervisor, health};
        ASSERT_TRUE(lifecycle.update_recovery_policies(initial).has_value());
        fail_after = allocation;
        auto result = lifecycle.update_recovery_policies(updates);
        fail_after.reset();
        const auto actual_first = lifecycle.recovery_policy_override(first);
        const auto actual_second = lifecycle.recovery_policy_override(second);
        ASSERT_TRUE(actual_first.has_value());
        if (!result)
        {
            ++failures;
            ASSERT_EQ(result.error(), std::make_error_code(std::errc::not_enough_memory));
            ASSERT_TRUE(actual_first->budget == original.budget);
            ASSERT_FALSE(actual_second.has_value());
        }
        else
        {
            ++successes;
            ASSERT_TRUE(actual_first->budget == changed.budget);
            ASSERT_TRUE(actual_second.has_value());
            ASSERT_TRUE(actual_second->budget == changed.budget);
        }
    }
    ASSERT_TRUE(failures > 0);
    ASSERT_TRUE(successes > 0);
}

TEST(configuration_default_loading_reports_allocation_failure)
{
    unsigned failures = 0;
    unsigned successes = 0;
    for (std::size_t allocation = 0; allocation != 64; ++allocation)
    {
        fail_after = allocation;
        const auto result = cnetmod::application::load_configuration(std::nullopt);
        fail_after.reset();
        if (result)
            ++successes;
        else
        {
            ++failures;
            ASSERT_TRUE(result.error().code == std::errc::not_enough_memory);
        }
    }
    ASSERT_TRUE(failures > 0);
    ASSERT_TRUE(successes > 0);
}

TEST(configuration_invalid_root_allocation_failure_does_not_terminate)
{
    const auto path = std::filesystem::temp_directory_path() /
        std::format("cnetmod-invalid-root-{}.json", std::chrono::steady_clock::now().time_since_epoch().count());
    struct remove_fixture
    {
        std::filesystem::path path;
        ~remove_fixture()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } cleanup{path};
    {
        std::ofstream output{path};
        output << R"([{"nested":[{},[],{"leaf":"value"}]}])";
        ASSERT_TRUE(output.good());
    }
    const std::optional<std::filesystem::path> file{path};
    for (std::size_t allocation = 0; allocation != 128; ++allocation)
    {
        fail_after = allocation;
        const auto result = cnetmod::application::load_configuration(file);
        fail_after.reset();
        ASSERT_FALSE(result.has_value());
    }
}

TEST(host_reload_preparation_failure_preserves_configuration_and_sampling)
{
    namespace app = cnetmod::application;
    const auto path = std::filesystem::temp_directory_path() /
        std::format("cnetmod-reload-{}.json", std::chrono::steady_clock::now().time_since_epoch().count());
    struct remove_fixture
    {
        std::filesystem::path path;
        ~remove_fixture()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } cleanup{path};
    unsigned failures = 0;
    unsigned successes = 0;
    for (std::size_t allocation = 0; allocation != 256; ++allocation)
    {
        {
            std::ofstream output{path};
            output << R"({"observability":{"sampling_ratio":0.75},"health":{"interval_ms":8765},"services":{"dependency":{"type":"custom","enabled":false,"recovery":{"budget_ms":9876}}}})";
            ASSERT_TRUE(output.good());
        }
        auto host = app::application_builder{"reload-transaction"}
            .configuration_file(path)
            .configure([](app::application_configuration& config)
                {
                    config.logging.manage_lifecycle = false;
                    config.management.enabled = false;
                })
            .build();
        ASSERT_TRUE(host.has_value());
        {
            std::ofstream output{path};
            output << R"({"observability":{"sampling_ratio":0.25},"health":{"interval_ms":1234},"services":{"dependency":{"type":"custom","enabled":false,"recovery":{"budget_ms":4567}}}})";
            ASSERT_TRUE(output.good());
        }
        fail_after = allocation;
        const auto result = host->reload_configuration();
        fail_after.reset();
        const auto config = host->configuration();
        if (!result)
        {
            ++failures;
            ASSERT_EQ(config.observability.sampling_ratio, 0.75);
            ASSERT_EQ(host->telemetry().sampling_ratio(), 0.75);
            ASSERT_TRUE(host->health().policy().interval == std::chrono::milliseconds{8765});
            ASSERT_TRUE(config.health.interval == std::chrono::milliseconds{8765});
            ASSERT_TRUE(config.services.at("dependency").recovery.budget == std::chrono::milliseconds{9876});
        }
        else
        {
            ++successes;
            ASSERT_TRUE(result->applied);
            ASSERT_EQ(config.observability.sampling_ratio, 0.25);
            ASSERT_EQ(host->telemetry().sampling_ratio(), 0.25);
            ASSERT_TRUE(host->health().policy().interval == std::chrono::milliseconds{1234});
            ASSERT_TRUE(config.health.interval == std::chrono::milliseconds{1234});
            ASSERT_TRUE(config.services.at("dependency").recovery.budget == std::chrono::milliseconds{4567});
        }
    }
    ASSERT_TRUE(failures > 0);
    ASSERT_TRUE(successes > 0);
}
