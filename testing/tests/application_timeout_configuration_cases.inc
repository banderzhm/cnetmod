TEST(application_rejects_invalid_client_timeouts_before_registration)
{
    const auto verify = [](std::string service_name, std::string property)
    {
        const std::vector<cnetmod::json::document> invalid{
            -1, 0, 1.5, true, nullptr, "1000", std::numeric_limits<std::uint64_t>::max()};
        for (const auto& timeout : invalid)
        {
            auto host = application::application_builder{"invalid-timeout"}
                .enable_auto_configuration()
                .configure([&](application::application_configuration& value)
                    {
                        value.logging.manage_lifecycle = false;
                        value.management.enabled = false;
                        application::configured_service service{
                            .name = service_name, .enabled = true};
                        service.properties[property] = timeout;
                        if (service_name == "openai")
                            service.properties["api_key"] = "test";
                        value.services.emplace("client", std::move(service));
                    })
                .build();
            ASSERT_FALSE(host.has_value());
            if (!host)
                ASSERT_EQ(host.error().code, std::make_error_code(std::errc::invalid_argument));
        }
        auto host = application::application_builder{"valid-timeout"}
            .enable_auto_configuration()
            .configure([&](application::application_configuration& value)
                {
                    value.logging.manage_lifecycle = false;
                    value.management.enabled = false;
                    application::configured_service service{
                        .name = service_name, .enabled = true};
                    service.properties[property] = 1;
                    if (service_name == "openai")
                        service.properties["api_key"] = "test";
                    value.services.emplace("client", std::move(service));
                })
            .build();
        ASSERT_TRUE(host.has_value());
    };
    verify("http_client", "connect_timeout_ms");
    verify("http_client", "request_timeout_ms");
#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
    verify("openai", "timeout_seconds");
#endif
}
