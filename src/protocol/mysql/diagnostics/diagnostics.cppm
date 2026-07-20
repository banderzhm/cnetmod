module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mysql:diagnostics;

import std;

namespace cnetmod::mysql {
// =============================================================================
// character_set — Character set description (reference: Boost.MySQL
// character_set)
// =============================================================================

export struct character_set {
  /// Character set name (NULL-terminated) — Should match name in MySQL SET
  /// NAMES
  const char *name = nullptr;

  /// Get byte count of first character, returns 0 for invalid sequence
  std::size_t (*next_char)(std::span<const unsigned char>) = nullptr;
};

/// UTF-8 (1-4 bytes) — default connection character set.
export extern const character_set utf8mb4_charset;

/// Seven-bit ASCII character set.
export extern const character_set ascii_charset;

// =============================================================================
// diagnostics — Error diagnostic information (reference: Boost.MySQL
// diagnostics)
// =============================================================================

export class diagnostics {
public:
  diagnostics() noexcept;

  /// Server-returned error message (encoded by character_set_results)
  auto server_message() const noexcept -> std::string_view;

  /// Client-generated error message (always ASCII)
  auto client_message() const noexcept -> std::string_view;

  void assign_server(std::string msg);
  void assign_client(std::string msg);

  void clear() noexcept;

  auto empty() const noexcept -> bool;

  friend auto operator==(const diagnostics &a, const diagnostics &b) noexcept
      -> bool;
  friend auto operator!=(const diagnostics &a, const diagnostics &b) noexcept
      -> bool;

private:
  bool is_server_ = false;
  std::string msg_;
};

// =============================================================================
// ssl_mode — TLS negotiation mode (reference: Boost.MySQL ssl_mode)
// =============================================================================

export enum class ssl_mode {
  disable, // Never use TLS
  enable,  // Use if server supports (default)
  require, // Must use TLS, otherwise abort
};

// =============================================================================
// quoting_context — String escaping context
// =============================================================================

export enum class quoting_context : char {
  double_quote = '"',
  single_quote = '\'',
  backtick = '`',
};

// =============================================================================
// format_options — SQL formatting / escaping options
// =============================================================================

export struct format_options {
  character_set charset = utf8mb4_charset; // Connection's current character set
  bool backslash_escapes =
      true; // true = backslash is escape character (default)
};

// =============================================================================
// escape_string — String escaping (reference: Boost.MySQL escape_string)
// =============================================================================
//
// Replaces characters that need escaping in input string with escape sequences,
// writes result to output. Escaping rules determined by quoting_context:
//   single_quote: escape '
//   double_quote: escape "
//   backtick:     escape `
// If backslash_escapes == true, also escape \, \0, \n, \r, \x1a
//
// Returns true for success, false for encoding error (simplified version always
// succeeds)

export auto escape_string(std::string_view input, const format_options &opts,
                          quoting_context ctx, std::string &output) -> bool;

/// Convenience overload: returns escaped string
export auto escape_string(std::string_view input, const format_options &opts,
                          quoting_context ctx = quoting_context::single_quote)
    -> std::string;
} // namespace cnetmod::mysql