module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :client_lifecycle;

import std;
import :types;
import :codec;
import :client;
import cnetmod.core.address;
import cnetmod.core.dns;
import cnetmod.coro.task;
import cnetmod.protocol.udp;

namespace cnetmod::coap {

udp_client::udp_client(io_context& ctx, client_config cfg)
    : ctx_(ctx), sock_(ctx), cfg_(cfg)
{
    std::random_device rd;
    rng_.seed((static_cast<std::uint64_t>(rd()) << 32) ^ rd());
    message_id_ = static_cast<std::uint16_t>(rd());
}

auto udp_client::get(const endpoint& remote, std::string path, std::string query)
    -> task<std::expected<message, std::error_code>>
{
    co_return co_await request(remote, make_request(request_options{
        .method_code = method::get,
        .path = std::move(path),
        .query = std::move(query),
    }));
}

auto udp_client::post(const endpoint& remote, std::string path, std::vector<std::byte> payload,
                      content_format format)
    -> task<std::expected<message, std::error_code>>
{
    co_return co_await request(remote, make_request(request_options{
        .method_code = method::post,
        .path = std::move(path),
        .content_type = format,
        .payload = std::move(payload),
    }));
}

auto udp_client::put(const endpoint& remote, std::string path, std::vector<std::byte> payload,
                     content_format format)
    -> task<std::expected<message, std::error_code>>
{
    co_return co_await request(remote, make_request(request_options{
        .method_code = method::put,
        .path = std::move(path),
        .content_type = format,
        .payload = std::move(payload),
    }));
}

auto udp_client::delete_(const endpoint& remote, std::string path)
    -> task<std::expected<message, std::error_code>>
{
    co_return co_await request(remote, make_request(request_options{
        .method_code = method::delete_,
        .path = std::move(path),
    }));
}

auto udp_client::resolve_endpoint(std::string_view host, std::uint16_t port)
    -> task<std::expected<endpoint, std::error_code>>
{
    auto addrs = co_await async_resolve_addresses(ctx_, host, std::to_string(port));
    if (!addrs || addrs->empty()) {
        co_return std::unexpected(std::make_error_code(std::errc::host_unreachable));
    }
    co_return endpoint{addrs->front(), port};
}

void udp_client::close() noexcept {
    sock_.close();
}

} // namespace cnetmod::coap
