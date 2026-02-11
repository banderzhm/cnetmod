module;

#include <cnetmod/config.hpp>
#include <cstring>

export module cnetmod.protocol.http:multipart;

import std;
import :types;

namespace cnetmod::http {

// =============================================================================
// URL 编码 / 解码
// =============================================================================

namespace detail {

inline auto hex_digit(char c) noexcept -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

inline constexpr char hex_chars[] = "0123456789ABCDEF";

/// RFC 3986 非保留字符
inline auto is_unreserved(char c) noexcept -> bool {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == '.' || c == '~';
}

} // namespace detail

/// 百分号解码。`+` → 空格 (form 模式)。无效序列原样保留。
export auto url_decode(std::string_view input, bool plus_as_space = true)
    -> std::string
{
    std::string out;
    out.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            auto hi = detail::hex_digit(input[i + 1]);
            auto lo = detail::hex_digit(input[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
            // 无效序列 — 原样保留
        }
        if (plus_as_space && input[i] == '+') {
            out += ' ';
        } else {
            out += input[i];
        }
    }
    return out;
}

/// RFC 3986 百分号编码
export auto url_encode(std::string_view input) -> std::string {
    std::string out;
    out.reserve(input.size() * 3 / 2);

    for (auto c : input) {
        if (detail::is_unreserved(c)) {
            out += c;
        } else {
            out += '%';
            out += detail::hex_chars[static_cast<unsigned char>(c) >> 4];
            out += detail::hex_chars[static_cast<unsigned char>(c) & 0x0F];
        }
    }
    return out;
}

// =============================================================================
// Content-Type 解析
// =============================================================================

export struct content_type {
    std::string mime;
    std::unordered_map<std::string, std::string> params;

    [[nodiscard]] auto param(std::string_view key) const -> std::string_view {
        // 参数名大小写不敏感查找
        for (auto& [k, v] : params) {
            if (k.size() != key.size()) continue;
            bool eq = true;
            for (std::size_t i = 0; i < k.size(); ++i) {
                auto a = (k[i] >= 'A' && k[i] <= 'Z') ? k[i] + 32 : k[i];
                auto b = (key[i] >= 'A' && key[i] <= 'Z') ? key[i] + 32 : key[i];
                if (a != b) { eq = false; break; }
            }
            if (eq) return v;
        }
        return {};
    }
};

namespace detail {

/// 跳过 OWS (可选空白: SP / HTAB)
inline auto skip_ows(std::string_view s) noexcept -> std::string_view {
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t'))
        s.remove_prefix(1);
    return s;
}

/// 解析引号字符串 (含 \" \\ 转义)，返回解引号后的值和剩余串
inline auto parse_quoted_string(std::string_view s)
    -> std::pair<std::string, std::string_view>
{
    // s 以 '"' 开始
    if (s.empty() || s[0] != '"') return {{}, s};
    s.remove_prefix(1);

    std::string val;
    while (!s.empty()) {
        if (s[0] == '"') {
            s.remove_prefix(1);
            return {val, s};
        }
        if (s[0] == '\\' && s.size() > 1) {
            val += s[1];
            s.remove_prefix(2);
        } else {
            val += s[0];
            s.remove_prefix(1);
        }
    }
    return {val, s}; // 缺少闭合引号 — 尽力而为
}

/// 解析 token (RFC 7230: 非分隔符可见 ASCII)
inline auto parse_token(std::string_view s)
    -> std::pair<std::string, std::string_view>
{
    std::size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        // tchar = "!" / "#" / "$" / "%" / "&" / "'" / "*" / "+" / "-" / "." /
        //         "^" / "_" / "`" / "|" / "~" / DIGIT / ALPHA
        if ((c >= '!' && c <= '~') && c != '"' && c != '(' && c != ')' &&
            c != ',' && c != '/' && c != ':' && c != ';' && c != '<' &&
            c != '=' && c != '>' && c != '?' && c != '@' && c != '[' &&
            c != ']' && c != '{' && c != '}' && c != '\\') {
            ++i;
        } else {
            break;
        }
    }
    return {std::string(s.substr(0, i)), s.substr(i)};
}

/// 参数名转小写
inline auto to_lower(std::string s) -> std::string {
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') c += 32;
    }
    return s;
}

} // namespace detail

/// 解析 Content-Type header value
/// e.g. "multipart/form-data; boundary=----abc; charset=utf-8"
export auto parse_content_type(std::string_view header) -> content_type {
    content_type ct;
    header = detail::skip_ows(header);

    // mime type: token "/" token
    auto [type_part, rest] = detail::parse_token(header);
    if (!rest.empty() && rest[0] == '/') {
        rest.remove_prefix(1);
        auto [sub, rest2] = detail::parse_token(rest);
        ct.mime = detail::to_lower(type_part) + "/" + detail::to_lower(sub);
        rest = rest2;
    } else {
        ct.mime = detail::to_lower(type_part);
    }

    // 参数: (; name=value)*
    while (!rest.empty()) {
        rest = detail::skip_ows(rest);
        if (rest.empty() || rest[0] != ';') break;
        rest.remove_prefix(1);
        rest = detail::skip_ows(rest);

        auto [pname, rest2] = detail::parse_token(rest);
        rest = rest2;
        if (pname.empty()) break;

        if (rest.empty() || rest[0] != '=') {
            ct.params[detail::to_lower(pname)] = "";
            continue;
        }
        rest.remove_prefix(1); // skip '='

        std::string pval;
        if (!rest.empty() && rest[0] == '"') {
            auto [qval, rest3] = detail::parse_quoted_string(rest);
            pval = std::move(qval);
            rest = rest3;
        } else {
            auto [tval, rest3] = detail::parse_token(rest);
            pval = std::move(tval);
            rest = rest3;
        }
        ct.params[detail::to_lower(pname)] = std::move(pval);
    }

    return ct;
}

// =============================================================================
// Content-Disposition 解析
// =============================================================================

export struct content_disposition {
    std::string type;            // "form-data", "attachment" 等
    std::string name;            // 字段名
    std::string filename;        // 原始文件名 (可空)
    std::string filename_star;   // RFC 5987 扩展文件名 (可空)

    /// 返回有效文件名：优先 filename_star，其次 filename
    [[nodiscard]] auto effective_filename() const noexcept -> std::string_view {
        if (!filename_star.empty()) return filename_star;
        return filename;
    }

    [[nodiscard]] auto has_filename() const noexcept -> bool {
        return !filename.empty() || !filename_star.empty();
    }
};

namespace detail {

/// 解析 RFC 5987 ext-value: charset'language'value-chars
/// e.g. "UTF-8''my%20file.txt" → "my file.txt"
inline auto decode_ext_value(std::string_view input) -> std::string {
    // charset'[language]'percent-encoded
    auto tick1 = input.find('\'');
    if (tick1 == std::string_view::npos) return std::string(input);
    auto tick2 = input.find('\'', tick1 + 1);
    if (tick2 == std::string_view::npos) return std::string(input);

    // charset = input.substr(0, tick1); // 暂不做 charset 转换
    auto encoded = input.substr(tick2 + 1);
    return url_decode(encoded, /*plus_as_space=*/false);
}

} // namespace detail

/// 解析 Content-Disposition header value
/// e.g. "form-data; name=\"field1\"; filename=\"my file.txt\""
export auto parse_content_disposition(std::string_view header)
    -> content_disposition
{
    content_disposition cd;
    header = detail::skip_ows(header);

    // disposition type
    auto [dtype, rest] = detail::parse_token(header);
    cd.type = detail::to_lower(dtype);

    // 参数
    while (!rest.empty()) {
        rest = detail::skip_ows(rest);
        if (rest.empty() || rest[0] != ';') break;
        rest.remove_prefix(1);
        rest = detail::skip_ows(rest);

        auto [pname, rest2] = detail::parse_token(rest);
        rest = rest2;
        if (pname.empty()) break;

        auto pname_lower = detail::to_lower(pname);

        if (rest.empty() || rest[0] != '=') continue;
        rest.remove_prefix(1);

        // filename* 使用 ext-value 语法 (不加引号)
        if (pname_lower == "filename*") {
            // ext-value 到 ; 或末尾
            auto end = rest.find(';');
            auto raw = (end != std::string_view::npos)
                ? rest.substr(0, end) : rest;
            // trim trailing OWS
            while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t'))
                raw.remove_suffix(1);
            cd.filename_star = detail::decode_ext_value(raw);
            if (end != std::string_view::npos) {
                rest = rest.substr(end);
            } else {
                rest = {};
            }
            continue;
        }

        std::string pval;
        if (!rest.empty() && rest[0] == '"') {
            auto [qval, rest3] = detail::parse_quoted_string(rest);
            pval = std::move(qval);
            rest = rest3;
        } else {
            auto [tval, rest3] = detail::parse_token(rest);
            pval = std::move(tval);
            rest = rest3;
        }

        if (pname_lower == "name") {
            cd.name = std::move(pval);
        } else if (pname_lower == "filename") {
            cd.filename = std::move(pval);
        }
    }

    return cd;
}

// =============================================================================
// Base64 解码 (用于 Content-Transfer-Encoding: base64)
// =============================================================================

namespace detail {

inline constexpr std::int8_t b64_table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 0-15
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 16-31
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63, // 32-47
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1, // 48-63  ('=' = -2)
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14, // 64-79
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1, // 80-95
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40, // 96-111
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1, // 112-127
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, // 128+
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

/// Base64 解码，忽略空白字符 (CR/LF/SP/TAB)，处理 padding
inline auto base64_decode(std::string_view input)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    std::vector<std::byte> out;
    out.reserve(input.size() * 3 / 4);

    std::uint32_t accum = 0;
    int bits = 0;

    for (auto c : input) {
        // 跳过空白
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        // padding 或结束
        if (c == '=') break;

        auto val = b64_table[static_cast<unsigned char>(c)];
        if (val < 0) {
            return std::unexpected(make_error_code(http_errc::unsupported_encoding));
        }

        accum = (accum << 6) | static_cast<std::uint32_t>(val);
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::byte>((accum >> bits) & 0xFF));
        }
    }

    return out;
}

/// Quoted-Printable 解码 (RFC 2045)
inline auto quoted_printable_decode(std::string_view input)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    std::vector<std::byte> out;
    out.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '=') {
            // soft line break: =\r\n 或 =\n
            if (i + 1 < input.size() && input[i + 1] == '\n') {
                ++i; // skip \n
                continue;
            }
            if (i + 2 < input.size() && input[i + 1] == '\r' && input[i + 2] == '\n') {
                i += 2; // skip \r\n
                continue;
            }
            // hex pair
            if (i + 2 < input.size()) {
                auto hi = hex_digit(input[i + 1]);
                auto lo = hex_digit(input[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    out.push_back(static_cast<std::byte>((hi << 4) | lo));
                    i += 2;
                    continue;
                }
            }
            // 无效 — 原样保留
            out.push_back(static_cast<std::byte>('='));
        } else {
            out.push_back(static_cast<std::byte>(input[i]));
        }
    }

    return out;
}

} // namespace detail

// =============================================================================
// form_field / form_file / form_data
// =============================================================================

export struct form_field {
    std::string name;
    std::string value;
};

export struct form_file {
    std::string field_name;
    std::string filename;
    std::string content_type;     // 默认 "application/octet-stream"
    header_map  headers;          // 该 part 的完整 headers
    std::vector<std::byte> data;

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return data.size();
    }

    [[nodiscard]] auto data_as_string() const noexcept -> std::string_view {
        return {reinterpret_cast<const char*>(data.data()), data.size()};
    }
};

export class form_data {
public:
    // --- 字段访问 ---

    [[nodiscard]] auto field(std::string_view name) const
        -> std::optional<std::string_view>
    {
        for (auto& f : fields_) {
            if (f.name == name) return f.value;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto fields(std::string_view name) const
        -> std::vector<std::string_view>
    {
        std::vector<std::string_view> result;
        for (auto& f : fields_) {
            if (f.name == name) result.push_back(f.value);
        }
        return result;
    }

    [[nodiscard]] auto has_field(std::string_view name) const noexcept -> bool {
        for (auto& f : fields_) {
            if (f.name == name) return true;
        }
        return false;
    }

    // --- 文件访问 ---

    [[nodiscard]] auto file(std::string_view name) const -> const form_file* {
        for (auto& f : files_) {
            if (f.field_name == name) return &f;
        }
        return nullptr;
    }

    [[nodiscard]] auto files(std::string_view name) const
        -> std::vector<const form_file*>
    {
        std::vector<const form_file*> result;
        for (auto& f : files_) {
            if (f.field_name == name) result.push_back(&f);
        }
        return result;
    }

    [[nodiscard]] auto has_file(std::string_view name) const noexcept -> bool {
        for (auto& f : files_) {
            if (f.field_name == name) return true;
        }
        return false;
    }

    // --- 全部 ---

    [[nodiscard]] auto all_fields() const noexcept
        -> const std::vector<form_field>& { return fields_; }

    [[nodiscard]] auto all_files() const noexcept
        -> const std::vector<form_file>& { return files_; }

    [[nodiscard]] auto field_count() const noexcept -> std::size_t {
        return fields_.size();
    }

    [[nodiscard]] auto file_count() const noexcept -> std::size_t {
        return files_.size();
    }

    // --- 修改 ---

    void add_field(form_field f) { fields_.push_back(std::move(f)); }
    void add_file(form_file f)   { files_.push_back(std::move(f)); }

private:
    std::vector<form_field> fields_;
    std::vector<form_file>  files_;
};

// =============================================================================
// multipart_parser — 完整 multipart/form-data 解析器
// =============================================================================

export class multipart_parser {
public:
    explicit multipart_parser(std::string boundary)
        : boundary_(std::move(boundary))
        , delimiter_("--" + boundary_)
        , close_delimiter_("--" + boundary_ + "--")
    {}

    /// 解析完整的 multipart body，返回 form_data
    [[nodiscard]] auto parse(std::string_view body)
        -> std::expected<form_data, std::error_code>
    {
        form_data result;

        // 1. 找到第一个 delimiter (跳过 preamble)
        auto first = find_in(body, delimiter_, 0);
        if (first == std::string_view::npos) {
            return std::unexpected(make_error_code(http_errc::invalid_multipart));
        }

        auto pos = first + delimiter_.size();

        // 跳过 delimiter 后的 CRLF (或 -- 表示结束)
        if (!skip_transport_padding(body, pos)) {
            return std::unexpected(make_error_code(http_errc::invalid_multipart));
        }

        // 2. 循环解析 parts
        while (pos < body.size()) {
            // 检查是否是 close delimiter
            // 已在上一轮的 skip_transport_padding 或 boundary 后处理

            // 查找下一个 boundary
            // part body 从 pos 开始，到 \r\n + delimiter 之前结束
            auto next_delim = find_in(body, "\r\n" + delimiter_, pos);

            std::string_view part_data;
            bool is_last = false;

            if (next_delim == std::string_view::npos) {
                // 没有更多 boundary — 可能格式错误或缺少 close delimiter
                // 尝试查找 close delimiter without leading CRLF (容错)
                auto close_pos = find_in(body, "\r\n" + close_delimiter_, pos);
                if (close_pos != std::string_view::npos) {
                    part_data = body.substr(pos, close_pos - pos);
                    is_last = true;
                } else {
                    // 取剩余所有内容作为最后一个 part (容错)
                    part_data = body.substr(pos);
                    is_last = true;
                }
            } else {
                part_data = body.substr(pos, next_delim - pos);
            }

            // 解析这个 part
            auto r = parse_part(part_data, result);
            if (!r) return std::unexpected(r.error());

            if (is_last) break;

            // 跳过 \r\n + delimiter
            pos = next_delim + 2 + delimiter_.size();

            // 检查是否是 close delimiter (--)
            if (pos + 1 < body.size() && body[pos] == '-' && body[pos + 1] == '-') {
                break; // 结束
            }

            // 跳过 CRLF
            if (!skip_transport_padding(body, pos)) break;
        }

        return result;
    }

private:
    /// 在 data 中从 offset 开始查找 needle
    static auto find_in(std::string_view data, std::string_view needle,
                        std::size_t offset) noexcept -> std::size_t
    {
        if (offset >= data.size() || needle.empty()) return std::string_view::npos;

        auto haystack = data.substr(offset);
        auto it = std::search(haystack.begin(), haystack.end(),
                              needle.begin(), needle.end());
        if (it == haystack.end()) return std::string_view::npos;
        return offset + static_cast<std::size_t>(it - haystack.begin());
    }

    /// 跳过 transport-padding 和 CRLF
    auto skip_transport_padding(std::string_view body, std::size_t& pos) const
        -> bool
    {
        // 跳过 SP/HTAB (transport-padding)
        while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t'))
            ++pos;
        // 跳过 CRLF
        if (pos + 1 < body.size() && body[pos] == '\r' && body[pos + 1] == '\n') {
            pos += 2;
            return true;
        }
        // 也接受单独 LF (容错)
        if (pos < body.size() && body[pos] == '\n') {
            ++pos;
            return true;
        }
        // 可能是 close delimiter
        if (pos + 1 < body.size() && body[pos] == '-' && body[pos + 1] == '-') {
            return true; // caller 会检查
        }
        return pos >= body.size(); // EOF 也算结束
    }

    /// 解析单个 part: headers 和 body，分类为 field 或 file
    auto parse_part(std::string_view part_data, form_data& result)
        -> std::expected<void, std::error_code>
    {
        // 空行 (\r\n\r\n) 分隔 headers 和 body
        auto header_end = find_in(part_data, "\r\n\r\n", 0);
        std::string_view header_block;
        std::string_view body_block;

        if (header_end == std::string_view::npos) {
            // 也尝试 \n\n (容错)
            header_end = find_in(part_data, "\n\n", 0);
            if (header_end == std::string_view::npos) {
                // 无 headers，整体作为 body (极端容错)
                body_block = part_data;
            } else {
                header_block = part_data.substr(0, header_end);
                body_block = part_data.substr(header_end + 2);
            }
        } else {
            header_block = part_data.substr(0, header_end);
            body_block = part_data.substr(header_end + 4);
        }

        // 解析 headers
        auto headers_r = parse_part_headers(header_block);
        if (!headers_r) return std::unexpected(headers_r.error());
        auto& headers = *headers_r;

        // 解析 Content-Disposition
        auto cd_it = headers.find("Content-Disposition");
        if (cd_it == headers.end()) {
            // 缺少 Content-Disposition — 跳过该 part
            return {};
        }

        auto cd = parse_content_disposition(cd_it->second);
        if (cd.name.empty() && cd.type != "form-data") {
            // 非 form-data 且无 name — 跳过
            return {};
        }

        // Content-Transfer-Encoding
        auto cte_it = headers.find("Content-Transfer-Encoding");
        std::string_view encoding;
        if (cte_it != headers.end()) encoding = cte_it->second;

        // 解码 body
        auto decoded = decode_body(body_block, encoding);
        if (!decoded) return std::unexpected(decoded.error());

        // 分类
        if (cd.has_filename()) {
            // 文件
            form_file ff;
            ff.field_name = std::move(cd.name);
            ff.filename = std::string(cd.effective_filename());
            ff.headers = std::move(headers);

            auto ct_it = ff.headers.find("Content-Type");
            if (ct_it != ff.headers.end()) {
                ff.content_type = ct_it->second;
            } else {
                ff.content_type = "application/octet-stream";
            }

            ff.data = std::move(*decoded);
            result.add_file(std::move(ff));
        } else {
            // 普通字段
            form_field field;
            field.name = std::move(cd.name);
            field.value = std::string(
                reinterpret_cast<const char*>(decoded->data()),
                decoded->size());
            result.add_field(std::move(field));
        }

        return {};
    }

    /// 解析 part 的 header block (每行 "Key: Value\r\n")
    static auto parse_part_headers(std::string_view block)
        -> std::expected<header_map, std::error_code>
    {
        header_map headers;
        if (block.empty()) return headers;

        // 支持 continuation lines (以 SP/HTAB 开始的行拼接到上一行)
        std::string current_key;
        std::string current_value;

        auto flush = [&]() {
            if (!current_key.empty()) {
                auto it = headers.find(current_key);
                if (it != headers.end()) {
                    it->second += ", ";
                    it->second += current_value;
                } else {
                    headers.emplace(std::move(current_key), std::move(current_value));
                }
                current_key.clear();
                current_value.clear();
            }
        };

        while (!block.empty()) {
            // 找行结束
            std::size_t eol = std::string_view::npos;
            bool crlf = false;
            for (std::size_t i = 0; i < block.size(); ++i) {
                if (block[i] == '\r' && i + 1 < block.size() && block[i + 1] == '\n') {
                    eol = i; crlf = true; break;
                }
                if (block[i] == '\n') {
                    eol = i; break;
                }
            }

            std::string_view line;
            if (eol == std::string_view::npos) {
                line = block;
                block = {};
            } else {
                line = block.substr(0, eol);
                block.remove_prefix(eol + (crlf ? 2 : 1));
            }

            if (line.empty()) continue;

            // continuation line?
            if (line[0] == ' ' || line[0] == '\t') {
                if (!current_key.empty()) {
                    current_value += ' ';
                    auto trimmed = detail::skip_ows(line);
                    current_value += trimmed;
                }
                continue;
            }

            // 先 flush 前一个 header
            flush();

            // 解析 "Key: Value"
            auto colon = line.find(':');
            if (colon == std::string_view::npos) continue; // 无效行 — 跳过

            auto key = line.substr(0, colon);
            // trim key 尾空白
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
                key.remove_suffix(1);

            auto val = detail::skip_ows(line.substr(colon + 1));
            // trim val 尾空白
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t'))
                val.remove_suffix(1);

            current_key = std::string(key);
            current_value = std::string(val);
        }

        flush();
        return headers;
    }

    /// 根据 Content-Transfer-Encoding 解码 body
    static auto decode_body(std::string_view body, std::string_view encoding)
        -> std::expected<std::vector<std::byte>, std::error_code>
    {
        // 空 encoding 或 7bit/8bit/binary — 直接复制
        if (encoding.empty()) {
            return raw_copy(body);
        }

        // 转小写比较
        std::string enc_lower;
        enc_lower.reserve(encoding.size());
        for (auto c : encoding) {
            enc_lower += (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
        }

        if (enc_lower == "7bit" || enc_lower == "8bit" || enc_lower == "binary") {
            return raw_copy(body);
        }

        if (enc_lower == "base64") {
            return detail::base64_decode(body);
        }

        if (enc_lower == "quoted-printable") {
            return detail::quoted_printable_decode(body);
        }

        return std::unexpected(make_error_code(http_errc::unsupported_encoding));
    }

    static auto raw_copy(std::string_view body) -> std::vector<std::byte> {
        std::vector<std::byte> out(body.size());
        std::memcpy(out.data(), body.data(), body.size());
        return out;
    }

    std::string boundary_;
    std::string delimiter_;
    std::string close_delimiter_;
};

// =============================================================================
// URL-encoded 表单解析
// =============================================================================

/// 解析 application/x-www-form-urlencoded body
export auto parse_form_urlencoded(std::string_view body) -> form_data {
    form_data result;

    while (!body.empty()) {
        // 按 & 分割
        auto amp = body.find('&');
        auto pair = (amp != std::string_view::npos)
            ? body.substr(0, amp) : body;

        if (!pair.empty()) {
            auto eq = pair.find('=');
            std::string key, value;
            if (eq != std::string_view::npos) {
                key = url_decode(pair.substr(0, eq));
                value = url_decode(pair.substr(eq + 1));
            } else {
                key = url_decode(pair);
            }
            result.add_field({std::move(key), std::move(value)});
        }

        if (amp == std::string_view::npos) break;
        body.remove_prefix(amp + 1);
    }

    return result;
}

// =============================================================================
// 统一入口
// =============================================================================

/// 根据 Content-Type 自动选择解析方式
export auto parse_form(std::string_view content_type_header,
                       std::string_view body)
    -> std::expected<form_data, std::error_code>
{
    auto ct = parse_content_type(content_type_header);

    if (ct.mime == "multipart/form-data") {
        auto boundary = ct.param("boundary");
        if (boundary.empty()) {
            return std::unexpected(make_error_code(http_errc::missing_boundary));
        }
        multipart_parser mp{std::string(boundary)};
        return mp.parse(body);
    }

    if (ct.mime == "application/x-www-form-urlencoded") {
        return parse_form_urlencoded(body);
    }

    return std::unexpected(make_error_code(http_errc::invalid_multipart));
}

// =============================================================================
// multipart_builder — 客户端构建 multipart 请求
// =============================================================================

export class multipart_builder {
public:
    multipart_builder()
        : boundary_(generate_boundary())
    {}

    explicit multipart_builder(std::string boundary)
        : boundary_(std::move(boundary))
    {}

    /// 添加普通字段
    auto add_field(std::string_view name, std::string_view value)
        -> multipart_builder&
    {
        part p;
        p.disposition = std::format("form-data; name=\"{}\"", name);
        p.body.assign(
            reinterpret_cast<const std::byte*>(value.data()),
            reinterpret_cast<const std::byte*>(value.data() + value.size()));
        parts_.push_back(std::move(p));
        return *this;
    }

    /// 添加文件 (字节数据)
    auto add_file(std::string_view field_name, std::string_view filename,
                  std::string_view content_type,
                  std::span<const std::byte> data)
        -> multipart_builder&
    {
        part p;
        p.disposition = std::format(
            "form-data; name=\"{}\"; filename=\"{}\"", field_name, filename);
        p.content_type = std::string(content_type);
        p.body.assign(data.begin(), data.end());
        parts_.push_back(std::move(p));
        return *this;
    }

    /// 添加文件 (字符串数据)
    auto add_file(std::string_view field_name, std::string_view filename,
                  std::string_view content_type, std::string_view data)
        -> multipart_builder&
    {
        part p;
        p.disposition = std::format(
            "form-data; name=\"{}\"; filename=\"{}\"", field_name, filename);
        p.content_type = std::string(content_type);
        p.body.assign(
            reinterpret_cast<const std::byte*>(data.data()),
            reinterpret_cast<const std::byte*>(data.data() + data.size()));
        parts_.push_back(std::move(p));
        return *this;
    }

    /// 返回 Content-Type header value (含 boundary)
    [[nodiscard]] auto content_type() const -> std::string {
        return std::format("multipart/form-data; boundary={}", boundary_);
    }

    /// 返回 boundary
    [[nodiscard]] auto boundary() const noexcept -> std::string_view {
        return boundary_;
    }

    /// 构建完整的 multipart body
    [[nodiscard]] auto build() const -> std::string {
        std::string out;

        for (auto& p : parts_) {
            out += "--";
            out += boundary_;
            out += "\r\n";

            // Content-Disposition
            out += "Content-Disposition: ";
            out += p.disposition;
            out += "\r\n";

            // Content-Type (仅文件)
            if (!p.content_type.empty()) {
                out += "Content-Type: ";
                out += p.content_type;
                out += "\r\n";
            }

            // 空行
            out += "\r\n";

            // Body
            out.append(reinterpret_cast<const char*>(p.body.data()),
                       p.body.size());
            out += "\r\n";
        }

        // 结束 delimiter
        out += "--";
        out += boundary_;
        out += "--\r\n";

        return out;
    }

private:
    struct part {
        std::string disposition;
        std::string content_type;
        std::vector<std::byte> body;
    };

    /// 生成随机 boundary
    static auto generate_boundary() -> std::string {
        static constexpr char chars[] =
            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        // 使用 steady_clock + 简单混合生成伪随机 boundary
        auto seed = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        // xorshift64
        auto next = [&seed]() -> std::uint64_t {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            return seed;
        };

        std::string boundary = "----cnetmod";
        for (int i = 0; i < 16; ++i) {
            boundary += chars[next() % (sizeof(chars) - 1)];
        }
        return boundary;
    }

    std::string boundary_;
    std::vector<part> parts_;
};

} // namespace cnetmod::http
