module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :facade;

import std;
import :types;

namespace cnetmod::coap {

auto to_bytes(std::string_view text) -> std::vector<std::byte> {
    return {
        reinterpret_cast<const std::byte*>(text.data()),
        reinterpret_cast<const std::byte*>(text.data() + text.size()),
    };
}

auto payload_text(const message& msg) -> std::string {
    return {
        reinterpret_cast<const char*>(msg.payload.data()),
        msg.payload.size(),
    };
}

void set_payload(message& msg, std::span<const std::byte> body) {
    msg.payload.assign(body.begin(), body.end());
}

void set_text_payload(message& msg, std::string_view body, content_format format) {
    msg.add_uint_option(option_number::content_format, static_cast<std::uint16_t>(format));
    msg.payload = to_bytes(body);
}

auto text_response(const message& req, std::string_view body, response_code code) -> message {
    auto resp = make_response(req, code);
    set_text_payload(resp, body);
    return resp;
}

auto json_response(const message& req, std::string_view body, response_code code) -> message {
    auto resp = make_response(req, code);
    set_text_payload(resp, body, content_format::json);
    return resp;
}

} // namespace cnetmod::coap
