/// cnetmod.protocol.coap:codec - RFC 7252 datagram codec interface.

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.coap:codec;

import std;
import :types;

namespace cnetmod::coap {

export auto parse_message(std::span<const std::byte> bytes)
    -> std::expected<message, std::error_code>;

export auto serialize_message(const message& msg)
    -> std::expected<std::vector<std::byte>, std::error_code>;

export auto make_request(request_options opts) -> message;
export auto extract_path(const message& msg) -> std::string;
export auto extract_query(const message& msg) -> std::string;

} // namespace cnetmod::coap
