namespace {
auto build_mongodb_uri_application(cnetmod::json::document properties)
{
    return cnetmod::application::application_builder{"mongodb-uri-configuration"}
        .enable_auto_configuration()
        .configure([properties = std::move(properties)](cnetmod::application::application_configuration& value)
            {
                value.logging.manage_lifecycle = false;
                value.management.enabled = false;
                cnetmod::application::configured_service service{.name = "mongodb", .enabled = true};
                service.properties = properties;
                value.services.emplace("mongodb", std::move(service));
            })
        .build();
}
} // namespace

TEST(mongodb_application_uri_registration_is_lazy_and_named)
{
    auto host = build_mongodb_uri_application(cnetmod::json::object({{"uri", "mongodb+srv://cluster.example.com/?authMechanism=MONGODB-AWS&compressors=snappy"}}));
    ASSERT_TRUE(host.has_value());
    auto service = host->services().find<cnetmod::application::mongodb_service>();
    ASSERT_TRUE(service != nullptr);
    ASSERT_TRUE(service->topology_pool() == nullptr);
    bool rejected = false;
    try
    {
        (void)service->pool();
    }
    catch (const std::logic_error&)
    {
        rejected = true;
    }
    ASSERT_TRUE(rejected);
}

TEST(mongodb_application_uri_rejects_ambiguous_and_invalid_configuration)
{
    const auto cases = std::array{
        cnetmod::json::object({{"uri", "mongodb://localhost"}, {"host", "ignored"}}),
        cnetmod::json::object({{"uri", 17}}),
        cnetmod::json::object({{"uri", "mongodb://localhost"}, {"heartbeat_frequency_ms", 1}}),
        cnetmod::json::object({{"uri", "mongodb://localhost"}, {"tls_ca_file", false}}),
        cnetmod::json::object({{"uri", "mongodb://localhost/?minPoolSize=4"}, {"maximum_size", 2}}),
        cnetmod::json::object({{"uri", "mongodb://localhost/?compressors=unknown"}})};
    for (const auto& properties : cases)
        ASSERT_FALSE(build_mongodb_uri_application(properties).has_value());
}

TEST(mongodb_application_srv_failure_does_not_admit_monitoring_and_can_restart)
{
    namespace app = cnetmod::application;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io};
    app::task_supervisor supervisor{*io};
    cnetmod::cancel_token token;
    unsigned lookups{};
    app::mongodb_service_options options;
    options.uri = "mongodb+srv://cluster.example.com";
    options.srv_lookup = [&](std::string, std::string) -> cnetmod::task<cnetmod::mongodb::result<cnetmod::mongodb::srv_dns_records>>
    {
        ++lookups;
        co_return std::unexpected(cnetmod::mongodb::make_error(cnetmod::mongodb::error_code::server_selection_failed, "private DNS diagnostic"));
    };
    app::mongodb_service service{std::move(options), *io, "dns", app::service_requirement::required, {}};
    bool completed{};
    auto work = [&]() -> cnetmod::task<void>
    {
        app::service_context context{*io, telemetry, supervisor, token, cnetmod::deadline::after(std::chrono::seconds{3})};
        for (unsigned attempt{}; attempt < 2; ++attempt)
        {
            auto started = co_await service.start(context);
            ASSERT_FALSE(started.has_value());
            ASSERT_EQ(std::string_view(started.error().category().name()), std::string_view("cnetmod.mongodb"));
            ASSERT_FALSE(service.cleanup_required());
            ASSERT_TRUE(service.topology_pool() == nullptr);
            ASSERT_FALSE(supervisor.state("mongodb-maintenance:dns").has_value());
        }
        token.cancel();
        ASSERT_FALSE((co_await service.start(context)).has_value());
        ASSERT_EQ(lookups, 2U);
        ASSERT_FALSE((co_await service.command("admin", {{"ping", 1}})).has_value());
        completed = true;
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
    ASSERT_TRUE(completed);
}

TEST(mongodb_application_srv_txt_reaches_typed_cloud_and_compression_configuration)
{
    namespace app = cnetmod::application;
    namespace mg = cnetmod::mongodb;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io};
    app::task_supervisor supervisor{*io};
    cnetmod::cancel_token cancellation;
    bool configured{};
    unsigned lookups{};
    app::mongodb_service_options options;
    options.uri = "mongodb+srv://cluster.example.com/?tls=false&authMechanism=MONGODB-OIDC&authMechanismProperties=ENVIRONMENT:gcp,TOKEN_RESOURCE:audience&compressors=snappy";
    options.srv_lookup = [&](std::string hostname, std::string service) -> cnetmod::task<mg::result<mg::srv_dns_records>>
    {
        ASSERT_EQ(hostname, std::string("cluster.example.com"));
        ASSERT_EQ(service, std::string("mongodb"));
        ++lookups;
        co_return mg::srv_dns_records{{{{"node.example.com", 27017}, std::chrono::seconds{90}}}, {"authSource=%24external"}};
    };
    options.configure_connection = [&](mg::connection_options& connection) -> mg::result<void>
    {
        configured = true;
        ASSERT_TRUE(connection.authentication == mg::authentication_mechanism::oidc);
        ASSERT_EQ(connection.authentication_database, std::string("$external"));
        ASSERT_TRUE(connection.oidc_application_environment == mg::oidc_environment::gcp);
        ASSERT_EQ(connection.oidc_token_resource, std::string("audience"));
        ASSERT_TRUE(connection.enable_snappy_compression);
        ASSERT_TRUE(connection.compression_preference == std::vector<std::uint8_t>{mg::compressor_snappy});
        // Stop before network access: the assertions inspect the exact resolved
        // plan passed to the production cloud-provider composition path.
        return std::unexpected(mg::make_error(mg::error_code::authentication_failed, "injected pre-connect rejection"));
    };
    app::mongodb_service service{std::move(options), *io, "srv", app::service_requirement::required, {}};
    bool complete{};
    auto work = [&]() -> cnetmod::task<void>
    {
        app::service_context context{*io, telemetry, supervisor, cancellation, cnetmod::deadline::after(std::chrono::seconds{3})};
        auto started = co_await service.start(context);
        ASSERT_FALSE(started.has_value());
        ASSERT_TRUE(started.error() == std::errc::permission_denied);
        ASSERT_TRUE(configured);
        ASSERT_EQ(lookups, 1U);
        ASSERT_FALSE(service.cleanup_required());
        ASSERT_TRUE(service.topology_pool() == nullptr);
        complete = true;
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
    ASSERT_TRUE(complete);
}

TEST(mongodb_application_srv_late_resolution_honors_startup_deadline)
{
    namespace app = cnetmod::application;
    namespace mg = cnetmod::mongodb;
    cnetmod::net_init network;
    auto io = cnetmod::make_io_context();
    cnetmod::observability::telemetry_hub telemetry{*io};
    app::task_supervisor supervisor{*io};
    cnetmod::cancel_token cancellation;
    bool configured{}, completed{};
    app::mongodb_service_options options;
    options.uri = "mongodb+srv://cluster.example.com";
    options.srv_lookup = [&](std::string, std::string) -> cnetmod::task<mg::result<mg::srv_dns_records>>
    {
        (void)co_await cnetmod::async_timer_wait(*io, std::chrono::milliseconds{20});
        co_return mg::srv_dns_records{{{{"node.example.com", 27017}, std::chrono::seconds{60}}}, {}};
    };
    options.configure_connection = [&](mg::connection_options&) -> mg::result<void>
    {
        configured = true;
        return {};
    };
    app::mongodb_service service{std::move(options), *io, "deadline", app::service_requirement::required, {}};
    auto work = [&]() -> cnetmod::task<void>
    {
        app::service_context context{*io, telemetry, supervisor, cancellation, cnetmod::deadline::after(std::chrono::milliseconds{1})};
        auto started = co_await service.start(context);
        ASSERT_FALSE(started.has_value());
        ASSERT_TRUE(started.error() == std::errc::timed_out);
        ASSERT_FALSE(configured);
        ASSERT_FALSE(service.cleanup_required());
        ASSERT_TRUE(service.topology_pool() == nullptr);
        completed = true;
        io->stop();
    };
    cnetmod::spawn(*io, work());
    io->run();
    ASSERT_TRUE(completed);
}
