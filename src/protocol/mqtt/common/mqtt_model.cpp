module;

#include <cstring>

module cnetmod.protocol.mqtt;

import std;
import :types;

namespace cnetmod::mqtt {

auto to_string(protocol_version value) noexcept -> std::string_view {
  switch (value) {
  case protocol_version::v3_1_1:
    return "v3.1.1";
  case protocol_version::v5:
    return "v5.0";
  default:
    return "unknown";
  }
}

auto to_string(qos value) noexcept -> std::string_view {
  switch (value) {
  case qos::at_most_once:
    return "at_most_once";
  case qos::at_least_once:
    return "at_least_once";
  case qos::exactly_once:
    return "exactly_once";
  default:
    return "invalid_qos";
  }
}

auto get_packet_type(std::uint8_t value) noexcept -> control_packet_type {
  return static_cast<control_packet_type>(value & 0xF0);
}

auto get_packet_flags(std::uint8_t value) noexcept -> std::uint8_t {
  return value & 0x0F;
}

auto to_string(control_packet_type value) noexcept -> std::string_view {
  switch (value) {
  case control_packet_type::connect:
    return "CONNECT";
  case control_packet_type::connack:
    return "CONNACK";
  case control_packet_type::publish:
    return "PUBLISH";
  case control_packet_type::puback:
    return "PUBACK";
  case control_packet_type::pubrec:
    return "PUBREC";
  case control_packet_type::pubrel:
    return "PUBREL";
  case control_packet_type::pubcomp:
    return "PUBCOMP";
  case control_packet_type::subscribe:
    return "SUBSCRIBE";
  case control_packet_type::suback:
    return "SUBACK";
  case control_packet_type::unsubscribe:
    return "UNSUBSCRIBE";
  case control_packet_type::unsuback:
    return "UNSUBACK";
  case control_packet_type::pingreq:
    return "PINGREQ";
  case control_packet_type::pingresp:
    return "PINGRESP";
  case control_packet_type::disconnect:
    return "DISCONNECT";
  case control_packet_type::auth:
    return "AUTH";
  default:
    return "UNKNOWN";
  }
}

auto to_string(connect_return_code value) noexcept -> std::string_view {
  switch (value) {
  case connect_return_code::accepted:
    return "accepted";
  case connect_return_code::unacceptable_protocol_version:
    return "unacceptable_protocol_version";
  case connect_return_code::identifier_rejected:
    return "identifier_rejected";
  case connect_return_code::server_unavailable:
    return "server_unavailable";
  case connect_return_code::bad_user_name_or_password:
    return "bad_user_name_or_password";
  case connect_return_code::not_authorized:
    return "not_authorized";
  default:
    return "unknown";
  }
}

namespace v5 {
auto is_error(connect_reason_code value) noexcept -> bool {
  return static_cast<std::uint8_t>(value) >= 0x80;
}
auto is_error(suback_reason_code value) noexcept -> bool {
  return static_cast<std::uint8_t>(value) >= 0x80;
}
auto is_error(puback_reason_code value) noexcept -> bool {
  return static_cast<std::uint8_t>(value) >= 0x80;
}
auto is_error(pubrec_reason_code value) noexcept -> bool {
  return static_cast<std::uint8_t>(value) >= 0x80;
}
} // namespace v5

auto mqtt_property::byte_prop(property_id id, std::uint8_t value)
    -> mqtt_property {
  return {id, value};
}
auto mqtt_property::u16_prop(property_id id, std::uint16_t value)
    -> mqtt_property {
  return {id, value};
}
auto mqtt_property::u32_prop(property_id id, std::uint32_t value)
    -> mqtt_property {
  return {id, value};
}
auto mqtt_property::string_prop(property_id id, std::string value)
    -> mqtt_property {
  return {id, std::move(value)};
}
auto mqtt_property::binary_prop(property_id id, std::string value)
    -> mqtt_property {
  return {id, std::move(value)};
}
auto mqtt_property::string_pair_prop(property_id id, std::string key,
                                     std::string value) -> mqtt_property {
  return {id, std::pair{std::move(key), std::move(value)}};
}

auto subscribe_entry::encode_options() const noexcept -> std::uint8_t {
  auto options = static_cast<std::uint8_t>(max_qos);
  if (no_local)
    options |= 0x04;
  if (retain_as_published)
    options |= 0x08;
  return options | (static_cast<std::uint8_t>(rh) << 4);
}

namespace detail {

auto decode_variable_length(std::string_view data) noexcept
    -> std::pair<std::size_t, std::size_t> {
  std::size_t value = 0;
  std::size_t multiplier = 1;
  std::size_t consumed = 0;
  for (std::size_t i = 0; i < data.size() && i < 4; ++i) {
    const auto byte = static_cast<std::uint8_t>(data[i]);
    value += (byte & 0x7F) * multiplier;
    multiplier *= 128;
    ++consumed;
    if ((byte & 0x80) == 0)
      return {value, consumed};
  }
  return {0, 0};
}

auto read_u16(std::string_view data) noexcept -> std::uint16_t {
  return static_cast<std::uint16_t>((static_cast<std::uint8_t>(data[0]) << 8) |
                                    static_cast<std::uint8_t>(data[1]));
}

auto read_u32(std::string_view data) noexcept -> std::uint32_t {
  return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[0]))
          << 24) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[1]))
          << 16) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[2])) << 8) |
         static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[3]));
}

class mqtt_error_category_impl final : public std::error_category {
public:
  auto name() const noexcept -> const char * override { return "mqtt"; }
  auto message(int ev) const -> std::string override {
    switch (static_cast<mqtt_errc>(ev)) {
    case mqtt_errc::success:
      return "success";
    case mqtt_errc::malformed_packet:
      return "malformed packet";
    case mqtt_errc::protocol_error:
      return "protocol error";
    case mqtt_errc::invalid_remaining_length:
      return "invalid remaining length";
    case mqtt_errc::invalid_packet_type:
      return "invalid packet type";
    case mqtt_errc::invalid_qos:
      return "invalid qos";
    case mqtt_errc::packet_too_large:
      return "packet too large";
    case mqtt_errc::not_connected:
      return "not connected";
    case mqtt_errc::connect_refused:
      return "connection refused";
    case mqtt_errc::connect_timeout:
      return "connect timeout";
    case mqtt_errc::keep_alive_timeout:
      return "keep alive timeout";
    case mqtt_errc::unexpected_disconnect:
      return "unexpected disconnect";
    case mqtt_errc::unknown_error:
      return "unknown error";
    default:
      return "unrecognized mqtt error";
    }
  }
};

auto mqtt_category_instance() -> const std::error_category & {
  static const mqtt_error_category_impl instance;
  return instance;
}

} // namespace detail

auto make_error_code(mqtt_errc e) noexcept -> std::error_code {
  return {static_cast<int>(e), detail::mqtt_category_instance()};
}

auto validate_utf8(std::string_view s) noexcept -> bool {
  std::size_t i = 0;
  while (i < s.size()) {
    const auto b0 = static_cast<std::uint8_t>(s[i]);
    std::uint32_t cp = 0;
    std::size_t len = 0;
    if (b0 <= 0x7F) {
      cp = b0;
      len = 1;
    } else if ((b0 & 0xE0) == 0xC0) {
      if (i + 1 >= s.size())
        return false;
      const auto b1 = static_cast<std::uint8_t>(s[i + 1]);
      if ((b1 & 0xC0) != 0x80)
        return false;
      cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
      if (cp < 0x80)
        return false;
      len = 2;
    } else if ((b0 & 0xF0) == 0xE0) {
      if (i + 2 >= s.size())
        return false;
      const auto b1 = static_cast<std::uint8_t>(s[i + 1]);
      const auto b2 = static_cast<std::uint8_t>(s[i + 2]);
      if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80)
        return false;
      cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
      if (cp < 0x800)
        return false;
      len = 3;
    } else if ((b0 & 0xF8) == 0xF0) {
      if (i + 3 >= s.size())
        return false;
      const auto b1 = static_cast<std::uint8_t>(s[i + 1]);
      const auto b2 = static_cast<std::uint8_t>(s[i + 2]);
      const auto b3 = static_cast<std::uint8_t>(s[i + 3]);
      if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80)
        return false;
      cp = ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) |
           (b3 & 0x3F);
      if (cp < 0x10000 || cp > 0x10FFFF)
        return false;
      len = 4;
    } else {
      return false;
    }
    if (cp == 0 || (cp >= 1 && cp <= 0x1F && cp != 0x0A && cp != 0x0D) ||
        (cp >= 0x7F && cp <= 0x9F) || (cp >= 0xD800 && cp <= 0xDFFF) ||
        cp == 0xFFFE || cp == 0xFFFF)
      return false;
    i += len;
  }
  return true;
}

namespace detail {

void encode_variable_length(std::string &buf, std::size_t value) {
  if (value > 0x0FFFFFFF)
    return;
  do {
    auto byte = static_cast<char>(value & 0x7F);
    value >>= 7;
    if (value != 0)
      byte |= static_cast<char>(0x80);
    buf.push_back(byte);
  } while (value != 0);
}

void write_u16(std::string &buf, std::uint16_t value) {
  buf.push_back(static_cast<char>((value >> 8) & 0xFF));
  buf.push_back(static_cast<char>(value & 0xFF));
}

void write_u32(std::string &buf, std::uint32_t value) {
  buf.push_back(static_cast<char>((value >> 24) & 0xFF));
  buf.push_back(static_cast<char>((value >> 16) & 0xFF));
  buf.push_back(static_cast<char>((value >> 8) & 0xFF));
  buf.push_back(static_cast<char>(value & 0xFF));
}

void write_utf8_string(std::string &buf, std::string_view value) {
  write_u16(buf, static_cast<std::uint16_t>(value.size()));
  buf.append(value);
}

void write_binary(std::string &buf, std::string_view value) {
  write_u16(buf, static_cast<std::uint16_t>(value.size()));
  buf.append(value);
}

} // namespace detail

binary_data::binary_data(std::string_view data) { assign(data); }

binary_data::binary_data(const char *data) {
  assign(data == nullptr ? std::string_view{} : std::string_view{data});
}

binary_data::binary_data(const std::string &data) {
  assign(std::string_view{data});
}
binary_data::binary_data(const binary_data &other) { copy_from(other); }
binary_data::binary_data(binary_data &&other) noexcept {
  move_from(std::move(other));
}
binary_data::~binary_data() { destroy_shared(); }

auto binary_data::operator=(const binary_data &other) -> binary_data & {
  if (this != &other) {
    destroy_shared();
    size_ = 0;
    shared_ = false;
    copy_from(other);
  }
  return *this;
}

auto binary_data::operator=(binary_data &&other) noexcept -> binary_data & {
  if (this != &other) {
    destroy_shared();
    size_ = 0;
    shared_ = false;
    move_from(std::move(other));
  }
  return *this;
}

auto binary_data::operator=(std::string_view data) -> binary_data & {
  assign(data);
  return *this;
}

auto binary_data::operator=(const std::string &data) -> binary_data & {
  assign(std::string_view{data});
  return *this;
}

auto binary_data::operator=(const char *data) -> binary_data & {
  assign(data == nullptr ? std::string_view{} : std::string_view{data});
  return *this;
}

auto binary_data::data() const noexcept -> const char * {
  if (size_ == 0)
    return "";
  if (shared_)
    return reinterpret_cast<const char *>(storage_.bytes->data());
  return reinterpret_cast<const char *>(storage_.inline_bytes);
}

auto binary_data::size() const noexcept -> std::size_t { return size_; }
auto binary_data::empty() const noexcept -> bool { return size() == 0; }
auto binary_data::view() const noexcept -> std::string_view {
  return {data(), size()};
}
auto binary_data::str() const -> std::string { return std::string(view()); }
binary_data::operator std::string_view() const noexcept { return view(); }

void binary_data::assign(std::string_view data) {
  if (data.empty()) {
    destroy_shared();
    size_ = 0;
    shared_ = false;
    return;
  }
  if (data.size() <= inline_capacity) {
    destroy_shared();
    size_ = static_cast<std::uint32_t>(data.size());
    shared_ = false;
    std::memcpy(storage_.inline_bytes, data.data(), data.size());
    return;
  }
  auto next = std::make_shared<byte_storage>(data.size());
  std::memcpy(next->data(), data.data(), data.size());
  if (shared_) {
    storage_.bytes = std::move(next);
  } else {
    std::construct_at(&storage_.bytes, std::move(next));
  }
  size_ = static_cast<std::uint32_t>(data.size());
  shared_ = true;
}

void binary_data::copy_from(const binary_data &other) {
  size_ = other.size_;
  shared_ = other.shared_;
  if (other.shared_) {
    std::construct_at(&storage_.bytes, other.storage_.bytes);
  } else if (other.size_ > 0) {
    std::memcpy(storage_.inline_bytes, other.storage_.inline_bytes,
                other.size_);
  }
}

void binary_data::move_from(binary_data &&other) noexcept {
  size_ = other.size_;
  shared_ = other.shared_;
  if (other.shared_) {
    std::construct_at(&storage_.bytes, std::move(other.storage_.bytes));
  } else if (other.size_ > 0) {
    std::memcpy(storage_.inline_bytes, other.storage_.inline_bytes,
                other.size_);
  }
}

void binary_data::destroy_shared() noexcept {
  if (shared_)
    std::destroy_at(&storage_.bytes);
}

auto operator==(const binary_data &lhs, std::string_view rhs) noexcept -> bool {
  return lhs.view() == rhs;
}
auto operator==(std::string_view lhs, const binary_data &rhs) noexcept -> bool {
  return lhs == rhs.view();
}
auto operator==(const binary_data &lhs, const std::string &rhs) noexcept
    -> bool {
  return lhs.view() == std::string_view{rhs};
}
auto operator==(const std::string &lhs, const binary_data &rhs) noexcept
    -> bool {
  return std::string_view{lhs} == rhs.view();
}
auto operator<<(std::ostream &output, const binary_data &value)
    -> std::ostream & {
  return output << value.view();
}

} // namespace cnetmod::mqtt
