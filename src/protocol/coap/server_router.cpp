module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :server_router;

import std;
import :types;
import :server;

namespace cnetmod::coap {

void resource_router::add(method m, std::string path, request_handler handler) {
    routes_[route_key{m, normalize_path(std::move(path))}] = std::move(handler);
}

auto resource_router::dispatch(const inbound_request& req, const endpoint& peer) const
    -> task<message>
{
    const auto key = route_key{req.request.method_code(), normalize_path(req.path)};
    auto it = routes_.find(key);
    if (it == routes_.end()) {
        co_return make_response(req.request,
            has_path(key.path) ? response_code::method_not_allowed : response_code::not_found);
    }
    co_return co_await it->second(req, peer);
}

auto resource_router::route_key::operator==(const route_key& rhs) const noexcept -> bool {
    return m == rhs.m && path == rhs.path;
}

auto resource_router::route_hash::operator()(const route_key& key) const noexcept -> std::size_t {
    return (std::hash<int>{}(static_cast<int>(key.m)) << 1) ^
           std::hash<std::string>{}(key.path);
}

auto resource_router::normalize_path(std::string path) -> std::string {
    if (path.empty()) {
        return "/";
    }
    if (path.front() != '/') {
        path.insert(path.begin(), '/');
    }
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

auto resource_router::has_path(std::string_view path) const -> bool {
    return std::ranges::any_of(routes_, [&](const auto& item) {
        return item.first.path == path;
    });
}

} // namespace cnetmod::coap
