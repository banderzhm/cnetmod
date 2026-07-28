module;

export module cnetmod.protocol.socks5:types;

import std;

namespace cnetmod::socks5 {

// =============================================================================
// SOCKS5 Protocol Constants
// =============================================================================

export constexpr std::uint8_t SOCKS_VERSION = 0x05;
export constexpr std::uint8_t GSSAPI_VERSION = 0x01;

// Authentication methods
export enum class auth_method : std::uint8_t {
  no_auth = 0x00,           // No authentication required
  gssapi = 0x01,            // GSSAPI
  username_password = 0x02, // Username/Password
  no_acceptable = 0xFF      // No acceptable methods
};

// RFC 1961 GSS-API subnegotiation message types.
export enum class gssapi_message_type : std::uint8_t {
  authentication = 0x01,
  protection_level = 0x02,
  encapsulated_data = 0x03,
  abort = 0xFF
};

// RFC 1961 section 4.2 protection levels.
export enum class gssapi_protection_level : std::uint8_t {
  integrity = 0x01,
  confidentiality = 0x02,
  selective = 0x03
};

export struct gssapi_message {
  std::uint8_t version = GSSAPI_VERSION;
  gssapi_message_type type = gssapi_message_type::authentication;
  std::vector<std::byte> token;

  [[nodiscard]] auto serialize() const
      -> std::expected<std::vector<std::byte>, std::error_code>;
  [[nodiscard]] static auto parse(const std::byte *data, std::size_t len)
      -> std::optional<gssapi_message>;
};

// Adapter surface for a native GSS implementation (Kerberos, SPNEGO/SSPI,
// Heimdal, ...). cnetmod owns the RFC 1961 wire protocol while the application
// owns native credential and security-context lifetime.
export struct gssapi_step_result {
  std::vector<std::byte> output_token;
  bool complete = false;
};

export struct gssapi_unwrap_result {
  std::vector<std::byte> payload;
  bool confidential = false;
};

export using gssapi_step_handler = std::function<
    std::expected<gssapi_step_result, std::error_code>(
        std::span<const std::byte> input_token)>;
export using gssapi_wrap_handler = std::function<
    std::expected<std::vector<std::byte>, std::error_code>(
        std::span<const std::byte> payload, bool confidential)>;
export using gssapi_unwrap_handler = std::function<
    std::expected<gssapi_unwrap_result, std::error_code>(
        std::span<const std::byte> token)>;

export struct gssapi_context {
  gssapi_step_handler step;
  gssapi_wrap_handler wrap;
  gssapi_unwrap_handler unwrap;

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(step) && static_cast<bool>(wrap) &&
           static_cast<bool>(unwrap);
  }
};

export using gssapi_context_factory =
    std::function<std::expected<gssapi_context, std::error_code>()>;

export struct gssapi_session {
  gssapi_context context;
  gssapi_protection_level protection =
      gssapi_protection_level::integrity;
};

// SOCKS5 commands
export enum class command : std::uint8_t {
  connect = 0x01,      // CONNECT
  bind = 0x02,         // BIND
  udp_associate = 0x03 // UDP ASSOCIATE
};

// Address types
export enum class address_type : std::uint8_t {
  ipv4 = 0x01,        // IPv4 address
  domain_name = 0x03, // Domain name
  ipv6 = 0x04         // IPv6 address
};

// Reply codes
export enum class reply : std::uint8_t {
  succeeded = 0x00,                 // Succeeded
  general_failure = 0x01,           // General SOCKS server failure
  connection_not_allowed = 0x02,    // Connection not allowed by ruleset
  network_unreachable = 0x03,       // Network unreachable
  host_unreachable = 0x04,          // Host unreachable
  connection_refused = 0x05,        // Connection refused
  ttl_expired = 0x06,               // TTL expired
  command_not_supported = 0x07,     // Command not supported
  address_type_not_supported = 0x08 // Address type not supported
};

// =============================================================================
// SOCKS5 Request/Response Structures
// =============================================================================

export struct socks5_address {
  address_type type;
  std::string host; // IPv4/IPv6 address or domain name
  std::uint16_t port;

  [[nodiscard]] auto serialize() const -> std::vector<std::byte>;
  [[nodiscard]] static auto parse(const std::byte *data, std::size_t len)
      -> std::optional<std::pair<socks5_address, std::size_t>>;
};

export struct auth_request {
  std::uint8_t version = SOCKS_VERSION;
  std::vector<auth_method> methods;

  [[nodiscard]] auto serialize() const -> std::vector<std::byte>;
  [[nodiscard]] static auto parse(const std::byte *data, std::size_t len)
      -> std::optional<auth_request>;
};

export struct auth_response {
  std::uint8_t version = SOCKS_VERSION;
  auth_method method;

  [[nodiscard]] auto serialize() const -> std::vector<std::byte>;
  [[nodiscard]] static auto parse(const std::byte *data, std::size_t len)
      -> std::optional<auth_response>;
};

export struct username_password_request {
  std::uint8_t version = 0x01;
  std::string username;
  std::string password;

  [[nodiscard]] auto serialize() const -> std::vector<std::byte>;
  [[nodiscard]] static auto parse(const std::byte *data, std::size_t len)
      -> std::optional<username_password_request>;
};

export struct username_password_response {
  std::uint8_t version = 0x01;
  std::uint8_t status; // 0x00 = success, other = failure

  [[nodiscard]] auto serialize() const -> std::vector<std::byte>;
  [[nodiscard]] static auto parse(const std::byte *data, std::size_t len)
      -> std::optional<username_password_response>;
};

export struct socks5_request {
  std::uint8_t version = SOCKS_VERSION;
  command cmd;
  std::uint8_t reserved = 0x00;
  socks5_address address;

  [[nodiscard]] auto serialize() const -> std::vector<std::byte>;
  [[nodiscard]] static auto parse(const std::byte *data, std::size_t len)
      -> std::optional<socks5_request>;
};

export struct socks5_response {
  std::uint8_t version = SOCKS_VERSION;
  reply rep;
  std::uint8_t reserved = 0x00;
  socks5_address bind_address;

  [[nodiscard]] auto serialize() const -> std::vector<std::byte>;
  [[nodiscard]] static auto parse(const std::byte *data, std::size_t len)
      -> std::optional<socks5_response>;
};

export struct udp_datagram {
  std::uint16_t reserved = 0x0000;
  std::uint8_t fragment = 0x00;
  socks5_address address;
  std::vector<std::byte> payload;

  [[nodiscard]] auto serialize() const -> std::vector<std::byte>;
  [[nodiscard]] static auto parse(const std::byte *data, std::size_t len)
      -> std::optional<udp_datagram>;
};

} // namespace cnetmod::socks5
