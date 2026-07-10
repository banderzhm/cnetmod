module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :server_discovery;

import std;
import :types;
import :server;

namespace cnetmod::coap {

auto udp_server::make_discovery_response(const inbound_request& req) -> std::optional<message> {
    if (!cfg_.enable_resource_discovery || req.request.method_code() != method::get ||
        req.path != "/.well-known/core") {
        return std::nullopt;
    }

    std::vector<resource_description> resources;
    {
        std::scoped_lock lock(resources_mutex_);
        resources = resources_;
    }

    std::string body;
    for (const auto& res : resources) {
        if (!body.empty()) {
            body.push_back(',');
        }
        body += "<";
        body += res.path;
        body += ">";
        if (!res.rt.empty()) {
            body += ";rt=\"";
            body += res.rt;
            body += "\"";
        }
        if (!res.if_.empty()) {
            body += ";if=\"";
            body += res.if_;
            body += "\"";
        }
        if (res.observable) {
            body += ";obs";
        }
    }

    auto resp = make_response(req.request, response_code::content);
    resp.add_uint_option(option_number::content_format,
        static_cast<std::uint16_t>(content_format::link_format));
    resp.payload.assign(reinterpret_cast<const std::byte*>(body.data()),
        reinterpret_cast<const std::byte*>(body.data() + body.size()));
    return resp;
}

} // namespace cnetmod::coap
