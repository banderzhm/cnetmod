/// cnetmod unit tests — HTTP IP filter IPv4/IPv6 matching

#include "test_framework.hpp"

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.core.socket;
import cnetmod.core.log;
import cnetmod.protocol.http;
import cnetmod.protocol.http.middleware.ip_filter;

using namespace cnetmod;
using namespace cnetmod::http;

static auto run_filter(std::string client_ip, ip_filter_options opts)
    -> std::pair<bool, response>
{
    auto io = make_io_context();
    socket sock;
    response resp;
    header_map headers{{"X-Forwarded-For", std::move(client_ip)}};
    request_context ctx(*io, sock, "GET", "/secure", headers, "", resp, {});

    logger::set_level(logger::level::error);
    bool next_called = false;
    auto mw = ip_filter(std::move(opts));
    sync_wait(mw(ctx, [&next_called]() -> task<void> {
        next_called = true;
        co_return;
    }));

    return {next_called, std::move(resp)};
}

TEST(ip_filter_ipv6_exact_allow) {
    auto [next_called, resp] = run_filter("2001:db8::10", {
        .allow_list = {"2001:db8::10"},
    });

    ASSERT_TRUE(next_called);
    ASSERT_EQ(resp.status_code(), status::ok);
}

TEST(ip_filter_ipv6_cidr_allow) {
    auto [next_called, resp] = run_filter("2001:db8:abcd::99", {
        .allow_list = {"2001:db8:abcd::/48"},
    });

    ASSERT_TRUE(next_called);
    ASSERT_EQ(resp.status_code(), status::ok);
}

TEST(ip_filter_ipv6_cidr_deny) {
    auto [next_called, resp] = run_filter("2001:db8:ffff::1", {
        .allow_list = {"2001:db8:abcd::/48"},
    });

    ASSERT_FALSE(next_called);
    ASSERT_EQ(resp.status_code(), status::forbidden);
}

TEST(ip_filter_ipv6_bracketed_exact_deny) {
    auto [next_called, resp] = run_filter("[::1]", {
        .deny_list = {"::1"},
    });

    ASSERT_FALSE(next_called);
    ASSERT_EQ(resp.status_code(), status::forbidden);
}

TEST(ip_filter_ipv4_still_supports_cidr) {
    auto [next_called, resp] = run_filter("10.12.34.56", {
        .allow_list = {"10.0.0.0/8"},
    });

    ASSERT_TRUE(next_called);
    ASSERT_EQ(resp.status_code(), status::ok);
}

RUN_TESTS()
