TEST(application_rejects_invalid_pool_capacity_without_normalization)
{
    std::vector<std::string> names;
#ifdef CNETMOD_HAS_PROTOCOL_REDIS
    names.emplace_back("redis");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
    names.emplace_back("mysql");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_POSTGRESQL
    names.emplace_back("postgresql");
#endif
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
    names.emplace_back("mongodb");
#endif
    const std::vector<cnetmod::json::document> invalid_values{
        -1, 1.5, true, nullptr, "2", std::numeric_limits<std::uint64_t>::max()};
    for (const auto& name : names)
    {
        const auto build = [&](cnetmod::json::document properties)
        {
            return application::application_builder{"pool-capacity"}
                .enable_auto_configuration()
                .configure([&](application::application_configuration& value)
                    {
                        value.logging.manage_lifecycle = false;
                        value.management.enabled = false;
                        if (name == "mysql")
                        {
                            properties["username"] = "test";
                            properties["database"] = "test";
                        }
                        application::configured_service service{.name = name, .enabled = true};
                        service.properties = std::move(properties);
                        value.services.emplace("pool", std::move(service));
                    })
                .build();
        };
        const auto reject = [&](cnetmod::json::document properties)
        {
            auto result = build(std::move(properties));
            ASSERT_FALSE(result.has_value());
            if (!result)
                ASSERT_EQ(result.error(), std::make_error_code(std::errc::invalid_argument));
        };
        for (const auto* field : {"minimum_size", "maximum_size"})
            for (const auto& invalid : invalid_values)
                reject(cnetmod::json::document{{field, invalid}});
        reject({{"maximum_size", 0}});
        reject({{"minimum_size", 2}, {"maximum_size", 1}});
        reject({{"minimum_size", 1000}});
        ASSERT_TRUE(build({{"minimum_size", 0}, {"maximum_size", 1}}).has_value());
        ASSERT_TRUE(build({{"minimum_size", 2}, {"maximum_size", 2}}).has_value());
        ASSERT_TRUE(build(cnetmod::json::document::object()).has_value());
    }
}
