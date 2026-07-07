/// cnetmod.protocol.coap:facade - concise public CoAP entry points.

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.coap:facade;

import std;
import :types;
import :client;
import :server;
import :multicast;

namespace cnetmod::coap {

export using client = udp_client;
export using server = udp_server;
export using route = request_handler;
export using resource = resource_description;
export using subscription = observe_subscription;
export using multicast = multicast_client;

export auto to_bytes(std::string_view text) -> std::vector<std::byte>;
export auto payload_text(const message& msg) -> std::string;
export void set_payload(message& msg, std::span<const std::byte> body);
export void set_text_payload(message& msg, std::string_view body,
                             content_format format = content_format::text_plain);
export auto text_response(const message& req, std::string_view body,
                          response_code code = response_code::content) -> message;
export auto json_response(const message& req, std::string_view body,
                          response_code code = response_code::content) -> message;

} // namespace cnetmod::coap
