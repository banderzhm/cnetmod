TEST(application_validates_connector_ports_before_narrowing)
{
    std::vector<std::string> services;
#ifdef CNETMOD_HAS_PROTOCOL_REDIS
    services.emplace_back("redis");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
    services.emplace_back("mysql");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
    services.emplace_back("postgresql");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
    services.emplace_back("mongodb");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
    services.emplace_back("kafka");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MQTT
    services.emplace_back("mqtt");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
    services.emplace_back("amqp091");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
    services.emplace_back("amqp10");
#endif
    const std::vector<cnetmod::json::document> invalid{
        -1, 0, 65536, 65537, 1.5, true, nullptr, "3306",
        std::numeric_limits<std::uint64_t>::max()};
    for (const auto& name : services)
    {
        const auto build = [&](const cnetmod::json::document& port)
        {
            return application::application_builder{"port-validation"}
                .enable_auto_configuration()
                .configure([&](application::application_configuration& value)
                    {
                        value.logging.manage_lifecycle = false;
                        value.management.enabled = false;
                        application::configured_service service{.name = name, .enabled = true};
                        service.properties["port"] = port;
                        if (name == "mysql")
                        {
                            service.properties["username"] = "test";
                            service.properties["database"] = "test";
                        }
                        value.services.emplace("connector", std::move(service));
                    })
                .build();
        };
        for (const auto& port : invalid)
        {
            auto host = build(port);
            ASSERT_FALSE(host.has_value());
            if (!host)
                ASSERT_EQ(host.error(), std::make_error_code(std::errc::invalid_argument));
        }
        for (const auto port : {1, 65535})
            ASSERT_TRUE(build(port).has_value());
    }
}
