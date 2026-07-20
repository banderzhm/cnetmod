module cnetmod.protocol.websocket; // implementation unit
import :handshake;
import :sha1;
import :base64;

namespace cnetmod::ws {
namespace {
constexpr std::string_view ws_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

auto ascii_lower(std::string_view value) -> std::string {
  std::string out;
  out.reserve(value.size());
  for (auto c : value)
    out += static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
  return out;
}
} // namespace

auto generate_sec_key() -> std::string {
  std::array<std::byte, 16> raw{};
  static thread_local std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<unsigned> dist(0, 255);
  for (auto &byte : raw)
    byte = static_cast<std::byte>(dist(rng));
  return detail::base64_encode(raw);
}

auto compute_accept_key(std::string_view sec_key) -> std::string {
  std::string input;
  input.reserve(sec_key.size() + ws_guid.size());
  input += sec_key;
  input += ws_guid;
  return detail::base64_encode(detail::sha1(input));
}

auto build_upgrade_request(std::string_view host, std::string_view path,
                           std::string_view sec_key,
                           std::string_view subprotocol,
                           std::string_view origin) -> http::request {
  http::request req(http::http_method::GET, path);
  req.set_header("Host", host);
  req.set_header("Upgrade", "websocket");
  req.set_header("Connection", "Upgrade");
  req.set_header("Sec-WebSocket-Key", sec_key);
  req.set_header("Sec-WebSocket-Version", "13");
  if (!subprotocol.empty())
    req.set_header("Sec-WebSocket-Protocol", subprotocol);
  if (!origin.empty())
    req.set_header("Origin", origin);
  return req;
}

auto validate_upgrade_response(const http::response_parser &resp,
                               std::string_view expected_accept)
    -> std::expected<void, std::error_code> {
  if (resp.status_code() != 101 ||
      ascii_lower(resp.get_header("Upgrade")) != "websocket" ||
      ascii_lower(resp.get_header("Connection")).find("upgrade") ==
          std::string::npos ||
      resp.get_header("Sec-WebSocket-Accept") != expected_accept)
    return std::unexpected(make_error_code(ws_errc::handshake_failed));
  return {};
}

auto validate_upgrade_request(const http::request_parser &req)
    -> std::expected<std::string, std::error_code> {
  if (req.method() != "GET" || req.version() != http::http_version::http_1_1 ||
      ascii_lower(req.get_header("Upgrade")) != "websocket" ||
      ascii_lower(req.get_header("Connection")).find("upgrade") ==
          std::string::npos ||
      req.get_header("Sec-WebSocket-Key").empty() ||
      req.get_header("Sec-WebSocket-Version") != "13")
    return std::unexpected(make_error_code(ws_errc::handshake_failed));
  return compute_accept_key(req.get_header("Sec-WebSocket-Key"));
}

auto build_upgrade_response(std::string_view accept_key,
                            std::string_view subprotocol) -> http::response {
  http::response response(http::status::switching_protocols);
  response.set_header("Upgrade", "websocket");
  response.set_header("Connection", "Upgrade");
  response.set_header("Sec-WebSocket-Accept", accept_key);
  if (!subprotocol.empty())
    response.set_header("Sec-WebSocket-Protocol", subprotocol);
  return response;
}
} // namespace cnetmod::ws
