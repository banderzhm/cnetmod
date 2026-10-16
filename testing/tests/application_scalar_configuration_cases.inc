TEST(application_builder_rejects_nonfinite_sampling)
{
    for (const auto ratio : {std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -0.1, 1.1})
    {
        auto host = application::application_builder{"sampling-validation"}
            .configure([&](application::application_configuration& value)
                {
                    value.logging.manage_lifecycle = false;
                    value.observability.sampling_ratio = ratio;
                })
            .build();
        ASSERT_FALSE(host.has_value());
        if (!host)
            ASSERT_EQ(host.error().code, std::make_error_code(std::errc::invalid_argument));
    }
    for (const auto ratio : {0.0, 0.5, 1.0})
    {
        application::application_configuration value;
        value.observability.sampling_ratio = ratio;
        ASSERT_TRUE(application::validate_configuration(value).has_value());
    }
}

TEST(application_validates_explicit_http_and_lifecycle_duration_ranges)
{
    using duration = std::chrono::milliseconds;
    const auto invalid = {duration{-1}, duration{0}, duration::max()};
    for (const auto timeout : invalid)
    {
        application::application_configuration value;
        value.http.request_timeout = timeout;
        ASSERT_FALSE(application::validate_configuration(value).has_value());
        value.http.request_timeout.reset();
        value.lifecycle.service_start_timeout = timeout;
        ASSERT_FALSE(application::validate_configuration(value).has_value());
    }
    application::application_configuration value;
    ASSERT_TRUE(application::validate_configuration(value).has_value());
    value.http.request_timeout = duration{1};
    ASSERT_TRUE(application::validate_configuration(value).has_value());
}
