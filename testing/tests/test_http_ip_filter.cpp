/// cnetmod unit tests — HTTP IP filter matching and trusted-proxy client resolution

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

TEST(ip_matches_ipv4_ipv6_and_cidr) {
    ASSERT_TRUE(ip_matches("2001:db8::10", "2001:db8::10"));
    ASSERT_TRUE(ip_matches("2001:db8:abcd::99", "2001:db8:abcd::/48"));
    ASSERT_FALSE(ip_matches("2001:db8:ffff::1", "2001:db8:abcd::/48"));
    ASSERT_TRUE(ip_matches("[::1]", "::1"));
    ASSERT_TRUE(ip_matches("10.12.34.56", "10.0.0.0/8"));
    ASSERT_FALSE(ip_matches("11.0.0.1", "10.0.0.0/8"));
}

TEST(forwarding_headers_are_ignored_from_untrusted_peers) {
    const std::vector<std::string> none;
    ASSERT_EQ(resolve_forwarded_client_ip("198.51.100.7", "1.2.3.4", "5.6.7.8", none),
        std::string{"198.51.100.7"});
    const std::vector<std::string> proxies{"10.0.0.0/8"};
    ASSERT_EQ(resolve_forwarded_client_ip("198.51.100.7", "1.2.3.4", "", proxies),
        std::string{"198.51.100.7"});
    ASSERT_EQ(resolve_forwarded_client_ip("", "1.2.3.4", "", proxies),
        std::string{"unknown"});
}

TEST(forwarded_client_is_the_first_untrusted_hop_from_the_right) {
    const std::vector<std::string> proxies{"10.0.0.0/8", "192.0.2.1"};
    // Client-supplied spoof (9.9.9.9) left of the real client is ignored.
    ASSERT_EQ(resolve_forwarded_client_ip("10.0.0.2",
                  "9.9.9.9, 203.0.113.5, 192.0.2.1", "", proxies),
        std::string{"203.0.113.5"});
    ASSERT_EQ(resolve_forwarded_client_ip("10.0.0.2", "", "203.0.113.9", proxies),
        std::string{"203.0.113.9"});
    ASSERT_EQ(resolve_forwarded_client_ip("10.0.0.2", "10.1.1.1", "", proxies),
        std::string{"10.0.0.2"});
    ASSERT_EQ(resolve_forwarded_client_ip("10.0.0.2", "[2001:db8::7]", "", proxies),
        std::string{"2001:db8::7"});
}

TEST(ip_filter_does_not_let_clients_choose_their_address) {
    // The unconnected test socket has no peer; a spoofed allow-listed
    // X-Forwarded-For must not grant access.
    auto [next_called, resp] = run_filter("10.12.34.56", {
        .allow_list = {"10.0.0.0/8"},
    });
    ASSERT_FALSE(next_called);
    ASSERT_EQ(resp.status_code(), status::forbidden);
}

RUN_TESTS()
