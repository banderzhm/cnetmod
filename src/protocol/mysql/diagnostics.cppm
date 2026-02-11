module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mysql:diagnostics;

import std;

namespace cnetmod::mysql {

// =============================================================================
// character_set — 字符集描述（参考 Boost.MySQL character_set）
// =============================================================================

export struct character_set {
    /// 字符集名称 (NULL 终止)  — 应与 MySQL SET NAMES 中的名称一致
    const char* name = nullptr;

    /// 获取第一个字符的字节数，返回 0 表示无效序列
    std::size_t (*next_char)(std::span<const unsigned char>) = nullptr;
};

// UTF-8 (1-4 字节) — 默认字符集
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
// diagnostics — 错误诊断信息（参考 Boost.MySQL diagnostics）
// =============================================================================

export class diagnostics {
public:
    diagnostics() noexcept = default;

    /// 服务端返回的错误消息（按 character_set_results 编码）
    auto server_message() const noexcept -> std::string_view {
        return is_server_ ? std::string_view(msg_) : std::string_view{};
    }

    /// 客户端产生的错误消息（始终 ASCII）
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
// ssl_mode — TLS 协商模式（参考 Boost.MySQL ssl_mode）
// =============================================================================

export enum class ssl_mode {
    disable,   // 从不使用 TLS
    enable,    // 服务器支持则使用（默认）
    require,   // 必须使用 TLS，否则中止
};

// =============================================================================
// quoting_context — 字符串转义上下文
// =============================================================================

export enum class quoting_context : char {
    double_quote = '"',
    single_quote = '\'',
    backtick     = '`',
};

// =============================================================================
// format_options — SQL 格式化 / 转义选项
// =============================================================================

export struct format_options {
    character_set charset = utf8mb4_charset;   // 连接当前字符集
    bool backslash_escapes = true;             // true = 反斜杠是转义字符（默认）
};

// =============================================================================
// escape_string — 字符串转义（参考 Boost.MySQL escape_string）
// =============================================================================
//
// 将输入字符串中需要转义的字符替换为转义序列，结果写入 output。
// 根据 quoting_context 决定转义规则：
//   single_quote: 转义 '
//   double_quote: 转义 "
//   backtick:     转义 `
// 如果 backslash_escapes == true，还需转义 \, \0, \n, \r, \x1a
//
// 返回 true 表示成功，false 表示编码错误（简化版始终成功）

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

        // 引号字符：双写转义
        if (ch == quote_char) {
            output.push_back(quote_char);
            output.push_back(quote_char);
            continue;
        }

        // 反引号上下文仅需处理反引号本身
        if (ctx == quoting_context::backtick) {
            output.push_back(ch);
            continue;
        }

        // backslash_escapes 模式下的特殊字符
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

/// 便捷重载：返回转义后的字符串
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
