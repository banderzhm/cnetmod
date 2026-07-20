#include "test_framework.hpp"

import std;
import cnetmod.protocol.http.middleware.metrics;

using namespace cnetmod::metrics;

TEST(openmetrics_counter_gauge_histogram_render) {
    registry reg;
    reg.counter_add("requests_total", 1, {{"method", "GET"}}, "request count");
    reg.gauge_set("connections", 3);
    reg.histogram_observe("request_seconds", 0.02, {0.01, 0.05, 0.1});

    auto text = reg.render_openmetrics();
    ASSERT_TRUE(text.find("# TYPE requests_total counter") != std::string::npos);
    ASSERT_TRUE(text.find("requests_total{method=\"GET\"} 1") != std::string::npos);
    ASSERT_TRUE(text.find("# TYPE connections gauge") != std::string::npos);
    ASSERT_TRUE(text.find("request_seconds_bucket{le=\"0.05\"}") != std::string::npos);
    ASSERT_TRUE(text.ends_with("# EOF\n"));
}

RUN_TESTS()
