module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.websocket:base64;

import std;

namespace cnetmod::ws::detail {

// =============================================================================
// Base64 Encoding/Decoding
// =============================================================================

namespace {

constexpr char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr bool is_base64(unsigned char c) noexcept {
    return (c == '+' || (c >= '/' && c <= '9') ||
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

constexpr auto find_base64_index(unsigned char c) noexcept -> unsigned char {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return 0;
}

} // anonymous namespace

/// Base64 encoding
export auto base64_encode(const void* input, std::size_t len) -> std::string {
    std::string ret;
    ret.reserve(((len + 2) / 3) * 4);

    auto src = static_cast<const unsigned char*>(input);
    int i = 0;
    unsigned char c3[3], c4[4];

    while (len--) {
        c3[i++] = *(src++);
        if (i == 3) {
            c4[0] = (c3[0] & 0xfc) >> 2;
            c4[1] = ((c3[0] & 0x03) << 4) | ((c3[1] & 0xf0) >> 4);
            c4[2] = ((c3[1] & 0x0f) << 2) | ((c3[2] & 0xc0) >> 6);
            c4[3] = c3[2] & 0x3f;
            for (i = 0; i < 4; ++i)
                ret += base64_chars[c4[i]];
            i = 0;
        }
    }

    if (i) {
        for (int j = i; j < 3; ++j) c3[j] = '\0';
        c4[0] = (c3[0] & 0xfc) >> 2;
        c4[1] = ((c3[0] & 0x03) << 4) | ((c3[1] & 0xf0) >> 4);
        c4[2] = ((c3[1] & 0x0f) << 2) | ((c3[2] & 0xc0) >> 6);
        c4[3] = c3[2] & 0x3f;
        for (int j = 0; j < i + 1; ++j)
            ret += base64_chars[c4[j]];
        while (i++ < 3)
            ret += '=';
    }
    return ret;
}

/// Base64 encoding (span version)
export auto base64_encode(std::span<const std::byte> input) -> std::string {
    return base64_encode(input.data(), input.size());
}

/// Base64 decoding
export auto base64_decode(std::string_view input) -> std::vector<std::byte> {
    auto in_len = input.size();
    int i = 0;
    std::size_t in_pos = 0;
    unsigned char c4[4], c3[3];
    std::vector<std::byte> ret;
    ret.reserve((in_len * 3) / 4);

    while (in_len-- && input[in_pos] != '=' && is_base64(static_cast<unsigned char>(input[in_pos]))) {
        c4[i++] = static_cast<unsigned char>(input[in_pos++]);
        if (i == 4) {
            for (i = 0; i < 4; ++i)
                c4[i] = find_base64_index(c4[i]);
            c3[0] = (c4[0] << 2) | ((c4[1] & 0x30) >> 4);
            c3[1] = ((c4[1] & 0xf) << 4) | ((c4[2] & 0x3c) >> 2);
            c3[2] = ((c4[2] & 0x3) << 6) | c4[3];
            for (i = 0; i < 3; ++i)
                ret.push_back(static_cast<std::byte>(c3[i]));
            i = 0;
        }
    }

    if (i) {
        for (int j = i; j < 4; ++j) c4[j] = 0;
        for (int j = 0; j < 4; ++j)
            c4[j] = find_base64_index(c4[j]);
        c3[0] = (c4[0] << 2) | ((c4[1] & 0x30) >> 4);
        c3[1] = ((c4[1] & 0xf) << 4) | ((c4[2] & 0x3c) >> 2);
        c3[2] = ((c4[2] & 0x3) << 6) | c4[3];
        for (int j = 0; j < i - 1; ++j)
            ret.push_back(static_cast<std::byte>(c3[j]));
    }
    return ret;
}

} // namespace cnetmod::ws::detail
