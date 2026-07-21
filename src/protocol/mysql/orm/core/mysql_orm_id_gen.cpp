module;

#include <cnetmod/config.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

module cnetmod.protocol.mysql;

import :orm_id_gen;

import std;

namespace cnetmod::mysql::orm {

auto uuid::is_nil() const noexcept -> bool {
  for (const auto byte : data) {
    if (byte != 0)
      return false;
  }
  return true;
}

auto uuid::to_string() const -> std::string {
  static constexpr char hex[] = "0123456789abcdef";
  std::string text;
  text.reserve(36);
  for (std::size_t index = 0; index < data.size(); ++index) {
    if (index == 4 || index == 6 || index == 8 || index == 10) {
      text.push_back('-');
    }
    text.push_back(hex[(data[index] >> 4) & 0x0F]);
    text.push_back(hex[data[index] & 0x0F]);
  }
  return text;
}

auto uuid::from_string(const std::string_view text) -> std::optional<uuid> {
  uuid value{};
  std::size_t nibble_index = 0;
  for (const char character : text) {
    if (nibble_index == 32)
      break;
    if (character == '-')
      continue;

    const auto nibble = hex_val(character);
    if (nibble < 0)
      return std::nullopt;
    if ((nibble_index % 2) == 0) {
      value.data[nibble_index / 2] = static_cast<std::uint8_t>(nibble << 4);
    } else {
      value.data[nibble_index / 2] |= static_cast<std::uint8_t>(nibble);
    }
    ++nibble_index;
  }
  return nibble_index == 32 ? std::optional<uuid>{value} : std::nullopt;
}

auto uuid::hex_val(const char character) noexcept -> int {
  if (character >= '0' && character <= '9')
    return character - '0';
  if (character >= 'a' && character <= 'f')
    return 10 + (character - 'a');
  if (character >= 'A' && character <= 'F')
    return 10 + (character - 'A');
  return -1;
}

namespace detail {

void crypto_random_bytes(std::uint8_t *buffer, const std::size_t length) {
#ifdef _WIN32
  (void)BCryptGenRandom(nullptr, buffer, static_cast<ULONG>(length),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
  std::random_device random_device;
  for (std::size_t index = 0; index < length; ++index) {
    buffer[index] = static_cast<std::uint8_t>(random_device() & 0xFF);
  }
#endif
}

} // namespace detail

auto uuid_v4() -> uuid {
  uuid value{};
  detail::crypto_random_bytes(value.data.data(), value.data.size());
  value.data[6] = (value.data[6] & 0x0F) | 0x40;
  value.data[8] = (value.data[8] & 0x3F) | 0x80;
  return value;
}

snowflake_generator::snowflake_generator(
    const std::uint16_t machine_id) noexcept
    : machine_id_(static_cast<std::int64_t>(machine_id) & max_machine) {}

auto snowflake_generator::next_id() -> std::int64_t {
  auto now = current_ms();
  if (now == last_ms_) {
    sequence_ = (sequence_ + 1) & max_sequence;
    while (sequence_ == 0 && now <= last_ms_) {
      now = current_ms();
    }
  } else {
    sequence_ = 0;
  }

  last_ms_ = now;
  return ((now - epoch_ms) << (machine_bits + sequence_bits)) |
         (machine_id_ << sequence_bits) | sequence_;
}

auto snowflake_generator::machine_id() const noexcept -> std::uint16_t {
  return static_cast<std::uint16_t>(machine_id_);
}

auto snowflake_generator::current_ms() -> std::int64_t {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             now.time_since_epoch())
      .count();
}

} // namespace cnetmod::mysql::orm
