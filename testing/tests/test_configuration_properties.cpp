#include "test_framework.hpp"

import std;
import cnetmod.application.configuration;
import cnetmod.json;

TEST(configured_service_reads_nested_properties_without_exposing_json)
{
    cnetmod::application::configured_service service;
    service.properties = cnetmod::json::document{
        {"security", cnetmod::json::document{{"issuer", "issuer-a"}, {"ttl", 3600}, {"origins", cnetmod::json::array({"a", "b"})}}}};

    const auto issuer = service.string_property("security.issuer");
    ASSERT_TRUE(issuer.has_value());
    ASSERT_TRUE(issuer->has_value());
    ASSERT_EQ(**issuer, "issuer-a");

    const auto ttl = service.integer_property("security.ttl");
    ASSERT_TRUE(ttl.has_value());
    ASSERT_TRUE(ttl->has_value());
    ASSERT_EQ(**ttl, 3600);

    const auto origins = service.string_array_property("security.origins");
    ASSERT_TRUE(origins.has_value());
    ASSERT_TRUE(origins->has_value());
    ASSERT_EQ((**origins).size(), std::size_t{2});
}

TEST(configured_service_distinguishes_missing_and_invalid_properties)
{
    cnetmod::application::configured_service service;
    service.properties = cnetmod::json::document{{"port", "not-an-integer"}};

    const auto missing = service.string_property("missing");
    ASSERT_TRUE(missing.has_value());
    ASSERT_FALSE(missing->has_value());

    const auto invalid = service.integer_property("port");
    ASSERT_FALSE(invalid.has_value());
    ASSERT_TRUE(invalid.error() == std::errc::invalid_argument);
}

TEST(configured_service_sets_typed_properties)
{
    cnetmod::application::configured_service service;
    service.set_property("host", std::string{"localhost"});
    service.set_property("port", std::int64_t{3306});
    service.set_property("tls", true);

    const auto host = service.string_property("host");
    const auto port = service.integer_property("port");
    ASSERT_TRUE(host.has_value() && host->has_value());
    ASSERT_TRUE(port.has_value() && port->has_value());
    ASSERT_EQ(**host, "localhost");
    ASSERT_EQ(**port, 3306);
    ASSERT_TRUE(service.properties.at("tls").is_boolean());
    ASSERT_TRUE(service.properties.at("tls").get<bool>());
}

RUN_TESTS()
