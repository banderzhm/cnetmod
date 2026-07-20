/**/ module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.websocket:handshake;
import std; // HTTP upgrade API declarations
import :types;
import cnetmod.protocol.http;
namespace cnetmod::ws {
export auto generate_sec_key() -> std::string;
export auto compute_accept_key(std::string_view sec_key) -> std::string;
export auto build_upgrade_request(std::string_view host, std::string_view path,
                                  std::string_view sec_key,
                                  std::string_view subprotocol = {},
                                  std::string_view origin = {})
    -> http::request;
export auto validate_upgrade_response(const http::response_parser &resp,
                                      std::string_view expected_accept)
    -> std::expected<void, std::error_code>;
export auto validate_upgrade_request(const http::request_parser &req)
    -> std::expected<std::string, std::error_code>;
export auto build_upgrade_response(std::string_view accept_key,
                                   std::string_view subprotocol = {})
    -> http::response;
} // namespace cnetmod::ws
