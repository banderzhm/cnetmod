module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mysql:diagnostics;

import std;

namespace cnetmod::mysql {

// =============================================================================
// character_set — Character set description (reference: Boost.MySQL character_set)
// =============================================================================

export struct character_set {
    /// Character set name (NULL-terminated) — Should match name in MySQL SET NAMES
    const char* name = nullptr;

    /// Get byte count of first character, returns 0 for invalid sequence
    std::size_t (*next_char)(std::span<const unsigned char>) = nullptr;
};

// UTF-8 (1-4 bytes) — Default character set
namespace detail {
inline std::size_t next_char_utf8mb4(std::span<const unsigned char> s) noexcept {
    if (s.empty()) return 0;
    unsigned char c0 = s[0];
    if (c0 < 0x80)                          return 1;
    if ((c0 & 0xE0) == 0xC0 && s.size() >= 2) return 2;
    if ((c0 & 0xF0) == 0xE0 && s.size() >= 3) return 3;
    if ((c0 & 0xF8) == 0xF0 && s.size() >= 4) return 4;
    return 0;  // invalid
}
inline std::size_t next_char_ascii(std::span<const unsigned char> s) noexcept {
    if (s.empty()) return 0;
    return s[0] < 0x80 ? 1 : 0;
}
} // namespace detail

export inline constexpr character_set utf8mb4_charset{"utf8mb4", detail::next_char_utf8mb4};
export inline constexpr character_set ascii_charset{"ascii", detail::next_char_ascii};

// =============================================================================
// diagnostics — Error diagnostic information (reference: Boost.MySQL diagnostics)
// =============================================================================

export class diagnostics {
public:
    diagnostics() noexcept = default;

    /// Server-returned error message (encoded by character_set_results)
    auto server_message() const noexcept -> std::string_view {
        return is_server_ ? std::string_view(msg_) : std::string_view{};
    }

    /// Client-generated error message (always ASCII)
    auto client_message() const noexcept -> std::string_view {
        return is_server_ ? std::string_view{} : std::string_view(msg_);
    }

    void assign_server(std::string msg) { msg_ = std::move(msg); is_server_ = true; }
    void assign_client(std::string msg) { msg_ = std::move(msg); is_server_ = false; }

    void clear() noexcept { msg_.clear(); is_server_ = false; }

    auto empty() const noexcept -> bool { return msg_.empty(); }

    friend auto operator==(const diagnostics& a, const diagnostics& b) noexcept -> bool {
        return a.is_server_ == b.is_server_ && a.msg_ == b.msg_;
    }
    friend auto operator!=(const diagnostics& a, const diagnostics& b) noexcept -> bool {
        return !(a == b);
    }

private:
    bool is_server_ = false;
    std::string msg_;
};

// =============================================================================
// ssl_mode — TLS negotiation mode (reference: Boost.MySQL ssl_mode)
// =============================================================================

export enum class ssl_mode {
    disable,   // Never use TLS
    enable,    // Use if server supports (default)
    require,   // Must use TLS, otherwise abort
};

// =============================================================================
// quoting_context — String escaping context
// =============================================================================

export enum class quoting_context : char {
    double_quote = '"',
    single_quote = '\'',
    backtick     = '`',
};

// =============================================================================
// format_options — SQL formatting / escaping options
// =============================================================================

export struct format_options {
    character_set charset = utf8mb4_charset;   // Connection's current character set
    bool backslash_escapes = true;             // true = backslash is escape character (default)
};

// =============================================================================
// escape_string — String escaping (reference: Boost.MySQL escape_string)
// =============================================================================
//
// Replaces characters that need escaping in input string with escape sequences, writes result to output.
// Escaping rules determined by quoting_context:
//   single_quote: escape '
//   double_quote: escape "
//   backtick:     escape `
// If backslash_escapes == true, also escape \, \0, \n, \r, \x1a
//
// Returns true for success, false for encoding error (simplified version always succeeds)

export inline auto escape_string(
    std::string_view input,
    const format_options& opts,
    quoting_context ctx,
    std::string& output
) -> bool
{
    output.clear();
    output.reserve(input.size() + input.size() / 8);

    char quote_char = static_cast<char>(ctx);

    for (std::size_t i = 0; i < input.size(); ++i) {
        char ch = input[i];

        // Quote character: escape by doubling
        if (ch == quote_char) {
            output.push_back(quote_char);
            output.push_back(quote_char);
            continue;
        }

        // Backtick context only needs to handle backtick itself
        if (ctx == quoting_context::backtick) {
            output.push_back(ch);
            continue;
        }

        // Special characters in backslash_escapes mode
        if (opts.backslash_escapes) {
            switch (ch) {
            case '\\': output.append("\\\\"); continue;
            case '\0': output.append("\\0");  continue;
            case '\n': output.append("\\n");  continue;
            case '\r': output.append("\\r");  continue;
            case '\x1a': output.append("\\Z"); continue;
            default: break;
            }
        }

        output.push_back(ch);
    }

    return true;
}

/// Convenience overload: returns escaped string
export inline auto escape_string(
    std::string_view input,
    const format_options& opts,
    quoting_context ctx = quoting_context::single_quote
) -> std::string
{
    std::string result;
    escape_string(input, opts, ctx, result);
    return result;
}

} // namespace cnetmod::mysql
