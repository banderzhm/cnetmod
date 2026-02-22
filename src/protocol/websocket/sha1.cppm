module;

#include <cnetmod/config.hpp>
#include <cstring>

export module cnetmod.protocol.websocket:sha1;

import std;

namespace cnetmod::ws::detail {

// =============================================================================
// SHA-1 Implementation (based on smallsha1, BSD license)
// =============================================================================

namespace {

inline auto rol(std::uint32_t value, unsigned steps) noexcept -> std::uint32_t {
    return (value << steps) | (value >> (32 - steps));
}

inline void clear_w(std::uint32_t* w) noexcept {
    for (int i = 15; i >= 0; --i) w[i] = 0;
}

inline void inner_hash(std::uint32_t* result, std::uint32_t* w) noexcept {
    auto a = result[0];
    auto b = result[1];
    auto c = result[2];
    auto d = result[3];
    auto e = result[4];

    int round = 0;

    auto sha1_round = [&](std::uint32_t func_val, std::uint32_t k) {
        auto t = rol(a, 5) + func_val + e + k + w[round];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    };

    while (round < 16) { sha1_round((b & c) | (~b & d), 0x5a827999); ++round; }
    while (round < 20) {
        w[round] = rol(w[round-3] ^ w[round-8] ^ w[round-14] ^ w[round-16], 1);
        sha1_round((b & c) | (~b & d), 0x5a827999); ++round;
    }
    while (round < 40) {
        w[round] = rol(w[round-3] ^ w[round-8] ^ w[round-14] ^ w[round-16], 1);
        sha1_round(b ^ c ^ d, 0x6ed9eba1); ++round;
    }
    while (round < 60) {
        w[round] = rol(w[round-3] ^ w[round-8] ^ w[round-14] ^ w[round-16], 1);
        sha1_round((b & c) | (b & d) | (c & d), 0x8f1bbcdc); ++round;
    }
    while (round < 80) {
        w[round] = rol(w[round-3] ^ w[round-8] ^ w[round-14] ^ w[round-16], 1);
        sha1_round(b ^ c ^ d, 0xca62c1d6); ++round;
    }

    result[0] += a; result[1] += b; result[2] += c;
    result[3] += d; result[4] += e;
}

} // anonymous namespace

/// Compute SHA-1 hash
export auto sha1(const void* src, std::size_t len) noexcept
    -> std::array<std::byte, 20>
{
    std::uint32_t result[5] = {
        0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0
    };
    auto sarray = static_cast<const unsigned char*>(src);
    std::uint32_t w[80];

    std::size_t current_block = 0;
    if (len >= 64) {
        auto end_of_full = len - 64;
        while (current_block <= end_of_full) {
            auto end_current = current_block + 64;
            for (int rp = 0; current_block < end_current; current_block += 4) {
                w[rp++] = static_cast<std::uint32_t>(sarray[current_block + 3])
                    | (static_cast<std::uint32_t>(sarray[current_block + 2]) << 8)
                    | (static_cast<std::uint32_t>(sarray[current_block + 1]) << 16)
                    | (static_cast<std::uint32_t>(sarray[current_block])     << 24);
            }
            inner_hash(result, w);
        }
    }

    auto end_current = len - current_block;
    clear_w(w);
    std::size_t last = 0;
    for (; last < end_current; ++last) {
        w[last >> 2] |= static_cast<std::uint32_t>(sarray[last + current_block])
            << ((3 - (last & 3)) << 3);
    }
    w[last >> 2] |= 0x80u << ((3 - (last & 3)) << 3);
    if (end_current >= 56) {
        inner_hash(result, w);
        clear_w(w);
    }
    w[15] = static_cast<std::uint32_t>(len << 3);
    inner_hash(result, w);

    std::array<std::byte, 20> hash{};
    for (int i = 19; i >= 0; --i) {
        hash[static_cast<std::size_t>(i)] = static_cast<std::byte>(
            (result[i >> 2] >> (((3 - i) & 0x3) << 3)) & 0xff);
    }
    return hash;
}

/// Compute SHA-1 hash (span version)
export auto sha1(std::span<const std::byte> input) noexcept
    -> std::array<std::byte, 20>
{
    return sha1(input.data(), input.size());
}

/// Compute SHA-1 hash (string_view version)
export auto sha1(std::string_view input) noexcept
    -> std::array<std::byte, 20>
{
    return sha1(input.data(), input.size());
}

} // namespace cnetmod::ws::detail
