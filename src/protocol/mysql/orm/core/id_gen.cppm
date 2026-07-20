export module cnetmod.protocol.mysql:orm_id_gen;

import std;

namespace cnetmod::mysql::orm {

// =============================================================================
// id_strategy — Primary key generation strategy
// =============================================================================

export enum class id_strategy : std::uint8_t {
  none = 0,           ///< No strategy (manual assignment or auto_increment)
  auto_increment = 1, ///< MySQL AUTO_INCREMENT
  uuid = 2,           ///< UUID v4 (stored as CHAR(36))
  snowflake = 3,      ///< Snowflake algorithm (stored as BIGINT)
};

// =============================================================================
// uuid — 128-bit universally unique identifier
// =============================================================================

export struct uuid {
  std::array<std::uint8_t, 16> data{};

  /// Whether all zeros (nil UUID)
  [[nodiscard]] auto is_nil() const noexcept -> bool;

  /// Convert to standard string "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
  [[nodiscard]] auto to_string() const -> std::string;

  /// Parse UUID from string (with or without '-')
  [[nodiscard]] static auto from_string(std::string_view sv)
      -> std::optional<uuid>;

  auto operator==(const uuid &) const noexcept -> bool = default;
  auto operator<=>(const uuid &) const noexcept = default;

private:
  static auto hex_val(char c) noexcept -> int;
};

// =============================================================================
// uuid_v4() — Generate UUID v4 (random)
// =============================================================================

export auto uuid_v4() -> uuid;

// =============================================================================
// snowflake_generator — Snowflake algorithm ID generator
// =============================================================================
//
// Structure (64 bit):
//   0            | 1..41          | 42..51      | 52..63
//   Sign bit(0)  | Millisecond timestamp(41) | machine(10) | sequence(12)
//
// Epoch: 2020-01-01 00:00:00 UTC = 1577836800000 ms
// Theoretical capacity: 4096 IDs per millisecond, per machine

export class snowflake_generator {
public:
  /// epoch: 2020-01-01 00:00:00 UTC (milliseconds)
  static constexpr std::int64_t epoch_ms = 1577836800000LL;
  static constexpr int machine_bits = 10;
  static constexpr int sequence_bits = 12;
  static constexpr std::int64_t max_machine = (1LL << machine_bits) - 1;
  static constexpr std::int64_t max_sequence = (1LL << sequence_bits) - 1;

  explicit snowflake_generator(std::uint16_t machine_id = 0) noexcept;

  /// Generate next snowflake ID (not thread-safe, user must lock)
  auto next_id() -> std::int64_t;

  /// Get machine_id
  [[nodiscard]] auto machine_id() const noexcept -> std::uint16_t;

private:
  std::int64_t machine_id_ = 0;
  std::int64_t last_ms_ = 0;
  std::int64_t sequence_ = 0;

  static auto current_ms() -> std::int64_t;
};

} // namespace cnetmod::mysql::orm
