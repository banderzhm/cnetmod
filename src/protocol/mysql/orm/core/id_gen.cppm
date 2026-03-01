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

export module cnetmod.protocol.mysql:orm_id_gen;

import std;

namespace cnetmod::mysql::orm {

// =============================================================================
// id_strategy — Primary key generation strategy
// =============================================================================

export enum class id_strategy : std::uint8_t {
    none           = 0,   ///< No strategy (manual assignment or auto_increment)
    auto_increment = 1,   ///< MySQL AUTO_INCREMENT
    uuid           = 2,   ///< UUID v4 (stored as CHAR(36))
    snowflake      = 3,   ///< Snowflake algorithm (stored as BIGINT)
};

// =============================================================================
// uuid — 128-bit universally unique identifier
// =============================================================================

export struct uuid {
    std::array<std::uint8_t, 16> data{};

    /// Whether all zeros (nil UUID)
    [[nodiscard]] auto is_nil() const noexcept -> bool {
        for (auto b : data)
            if (b != 0) return false;
        return true;
    }

    /// Convert to standard string "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
    [[nodiscard]] auto to_string() const -> std::string {
        static constexpr char hex[] = "0123456789abcdef";
        std::string s;
        s.reserve(36);
        for (int i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10)
                s.push_back('-');
            s.push_back(hex[(data[i] >> 4) & 0x0F]);
            s.push_back(hex[data[i] & 0x0F]);
        }
        return s;
    }

    /// Parse UUID from string (with or without '-')
    [[nodiscard]] static auto from_string(std::string_view sv) -> std::optional<uuid> {
        uuid u{};
        std::size_t j = 0;
        for (std::size_t i = 0; i < sv.size() && j < 32; ++i) {
            char c = sv[i];
            if (c == '-') continue;
            int nibble = hex_val(c);
            if (nibble < 0) return std::nullopt;
            if (j % 2 == 0)
                u.data[j / 2] = static_cast<std::uint8_t>(nibble << 4);
            else
                u.data[j / 2] |= static_cast<std::uint8_t>(nibble);
            ++j;
        }
        if (j != 32) return std::nullopt;
        return u;
    }

    auto operator==(const uuid&) const noexcept -> bool = default;
    auto operator<=>(const uuid&) const noexcept = default;

private:
    static constexpr auto hex_val(char c) noexcept -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    }
};

// =============================================================================
// uuid_v4() — Generate UUID v4 (random)
// =============================================================================

namespace detail {

inline void crypto_random_bytes(std::uint8_t* buf, std::size_t len) {
#ifdef _WIN32
    // Windows: BCryptGenRandom (CSPRNG)
    (void)BCryptGenRandom(nullptr, buf, static_cast<ULONG>(len),
                          BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    // POSIX fallback: std::random_device
    std::random_device rd;
    for (std::size_t i = 0; i < len; ++i)
        buf[i] = static_cast<std::uint8_t>(rd() & 0xFF);
#endif
}

} // namespace detail

export inline auto uuid_v4() -> uuid {
    uuid u{};
    detail::crypto_random_bytes(u.data.data(), 16);

    // RFC 4122: version 4 (random)
    u.data[6] = (u.data[6] & 0x0F) | 0x40;   // version = 4
    u.data[8] = (u.data[8] & 0x3F) | 0x80;   // variant = 10xx (RFC4122)
    return u;
}

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
    static constexpr int machine_bits  = 10;
    static constexpr int sequence_bits = 12;
    static constexpr std::int64_t max_machine  = (1LL << machine_bits) - 1;
    static constexpr std::int64_t max_sequence = (1LL << sequence_bits) - 1;

    explicit snowflake_generator(std::uint16_t machine_id = 0) noexcept
        : machine_id_(static_cast<std::int64_t>(machine_id) & max_machine)
    {}

    /// Generate next snowflake ID (not thread-safe, user must lock)
    auto next_id() -> std::int64_t {
        auto now = current_ms();

        if (now == last_ms_) {
            sequence_ = (sequence_ + 1) & max_sequence;
            if (sequence_ == 0) {
                // Sequence exhausted for this millisecond, wait for next millisecond
                while (now <= last_ms_)
                    now = current_ms();
            }
        } else {
            sequence_ = 0;
        }

        last_ms_ = now;

        return ((now - epoch_ms) << (machine_bits + sequence_bits))
             | (machine_id_ << sequence_bits)
             | sequence_;
    }

    /// Get machine_id
    [[nodiscard]] auto machine_id() const noexcept -> std::uint16_t {
        return static_cast<std::uint16_t>(machine_id_);
    }

private:
    std::int64_t machine_id_ = 0;
    std::int64_t last_ms_    = 0;
    std::int64_t sequence_   = 0;

    static auto current_ms() -> std::int64_t {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
    }
};

} // namespace cnetmod::mysql::orm
