module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.proto;

import std;
import cnetmod.core.error;
import cnetmod.protocol.grpc.types;

namespace cnetmod::grpc::proto {

export enum class wire_type : std::uint8_t {
    varint = 0,
    fixed64 = 1,
    length_delimited = 2,
    fixed32 = 5,
};

export struct field {
    std::uint32_t number = 0;
    wire_type type = wire_type::varint;
    std::uint64_t varint_value = 0;
    std::uint64_t fixed64_value = 0;
    byte_buffer bytes;
    std::uint32_t fixed32_value = 0;
};

export struct field_def {
    std::string label;
    std::string type;
    std::string name;
    std::uint32_t number = 0;
};

export struct message_def {
    std::string name;
    std::vector<field_def> fields;
};

export struct rpc_def {
    std::string name;
    std::string request_type;
    std::string response_type;
    bool client_streaming = false;
    bool server_streaming = false;
};

export struct service_def {
    std::string name;
    std::vector<rpc_def> rpcs;
};

export struct file_def {
    std::string syntax = "proto3";
    std::string package;
    std::vector<message_def> messages;
    std::vector<service_def> services;
};

namespace detail {

inline auto is_ident_start(char ch) noexcept -> bool {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

inline auto is_ident_char(char ch) noexcept -> bool {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.';
}

inline auto strip_comments(std::string_view text) -> std::string {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) ++i;
            i = std::min(i + 2, text.size());
            continue;
        }
        out.push_back(text[i++]);
    }
    return out;
}

class parser {
public:
    explicit parser(std::string text) : text_(std::move(text)) {}

    auto parse_file() -> std::expected<file_def, std::error_code> {
        file_def out;
        while (!eof()) {
            skip_ws();
            if (eof()) break;
            if (consume("syntax")) {
                if (!consume("=")) return fail();
                auto s = parse_string();
                if (!s) return fail();
                out.syntax = std::move(*s);
                (void)consume(";");
            } else if (consume("package")) {
                auto id = parse_ident();
                if (!id) return fail();
                out.package = std::move(*id);
                (void)consume(";");
            } else if (consume("message")) {
                auto msg = parse_message();
                if (!msg) return fail();
                out.messages.push_back(std::move(*msg));
            } else if (consume("service")) {
                auto svc = parse_service();
                if (!svc) return fail();
                out.services.push_back(std::move(*svc));
            } else {
                skip_statement_or_block();
            }
        }
        return out;
    }

private:
    [[nodiscard]] auto eof() const noexcept -> bool { return pos_ >= text_.size(); }

    void skip_ws() noexcept {
        while (!eof() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
    }

    auto consume(std::string_view token) -> bool {
        skip_ws();
        if (text_.substr(pos_, token.size()) != token) return false;
        if (!token.empty() && detail::is_ident_char(token.back())) {
            auto next = pos_ + token.size();
            if (next < text_.size() && detail::is_ident_char(text_[next])) return false;
        }
        pos_ += token.size();
        return true;
    }

    auto parse_ident() -> std::optional<std::string> {
        skip_ws();
        if (eof() || !detail::is_ident_start(text_[pos_])) return std::nullopt;
        auto start = pos_++;
        while (!eof() && detail::is_ident_char(text_[pos_])) ++pos_;
        return text_.substr(start, pos_ - start);
    }

    auto parse_string() -> std::optional<std::string> {
        skip_ws();
        if (eof() || text_[pos_] != '"') return std::nullopt;
        ++pos_;
        std::string out;
        while (!eof() && text_[pos_] != '"') {
            if (text_[pos_] == '\\' && pos_ + 1 < text_.size()) ++pos_;
            out.push_back(text_[pos_++]);
        }
        if (eof()) return std::nullopt;
        ++pos_;
        return out;
    }

    auto parse_u32() -> std::optional<std::uint32_t> {
        skip_ws();
        auto start = pos_;
        while (!eof() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        if (start == pos_) return std::nullopt;
        std::uint32_t value = 0;
        auto view = std::string_view(text_).substr(start, pos_ - start);
        auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), value);
        if (ec != std::errc{}) return std::nullopt;
        return value;
    }

    auto parse_message() -> std::optional<message_def> {
        auto name = parse_ident();
        if (!name || !consume("{")) return std::nullopt;
        message_def msg{.name = std::move(*name)};
        while (!eof() && !consume("}")) {
            if (consume("option") || consume("reserved") || consume("extensions")) {
                skip_statement_or_block();
                continue;
            }
            if (consume("message") || consume("enum") || consume("oneof")) {
                skip_block();
                continue;
            }
            field_def fd;
            auto first = parse_ident();
            if (!first) {
                skip_statement_or_block();
                continue;
            }
            if (*first == "repeated" || *first == "optional" || *first == "required") {
                fd.label = std::move(*first);
                auto type = parse_ident();
                if (!type) return std::nullopt;
                fd.type = std::move(*type);
            } else {
                fd.type = std::move(*first);
            }
            auto fname = parse_ident();
            if (!fname || !consume("=")) return std::nullopt;
            auto number = parse_u32();
            if (!number) return std::nullopt;
            fd.name = std::move(*fname);
            fd.number = *number;
            while (!eof() && text_[pos_] != ';') ++pos_;
            (void)consume(";");
            msg.fields.push_back(std::move(fd));
        }
        return msg;
    }

    auto parse_service() -> std::optional<service_def> {
        auto name = parse_ident();
        if (!name || !consume("{")) return std::nullopt;
        service_def svc{.name = std::move(*name)};
        while (!eof() && !consume("}")) {
            if (!consume("rpc")) {
                skip_statement_or_block();
                continue;
            }
            auto rpc_name = parse_ident();
            if (!rpc_name || !consume("(")) return std::nullopt;
            rpc_def rpc{.name = std::move(*rpc_name)};
            rpc.client_streaming = consume("stream");
            auto req = parse_ident();
            if (!req || !consume(")") || !consume("returns") || !consume("(")) return std::nullopt;
            rpc.request_type = std::move(*req);
            rpc.server_streaming = consume("stream");
            auto resp = parse_ident();
            if (!resp || !consume(")")) return std::nullopt;
            rpc.response_type = std::move(*resp);
            if (consume("{")) skip_block_body();
            else (void)consume(";");
            svc.rpcs.push_back(std::move(rpc));
        }
        return svc;
    }

    void skip_statement_or_block() {
        skip_ws();
        if (!eof() && text_[pos_] == '{') {
            skip_block();
            return;
        }
        while (!eof() && text_[pos_] != ';' && text_[pos_] != '}') ++pos_;
        if (!eof() && text_[pos_] == ';') ++pos_;
    }

    void skip_block() {
        skip_ws();
        if (!eof() && text_[pos_] == '{') ++pos_;
        skip_block_body();
    }

    void skip_block_body() {
        int depth = 1;
        while (!eof() && depth > 0) {
            if (text_[pos_] == '{') ++depth;
            else if (text_[pos_] == '}') --depth;
            ++pos_;
        }
    }

    auto fail() const -> std::expected<file_def, std::error_code> {
        return std::unexpected(make_error_code(std::errc::invalid_argument));
    }

    std::string text_;
    std::size_t pos_ = 0;
};

inline auto put_varint(byte_buffer& out, std::uint64_t value) -> void {
    while (value >= 0x80) {
        out.push_back(static_cast<std::byte>((value & 0x7f) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<std::byte>(value));
}

inline auto read_varint(std::span<const std::byte> data, std::size_t& pos)
    -> std::optional<std::uint64_t>
{
    std::uint64_t value = 0;
    unsigned shift = 0;
    while (pos < data.size() && shift <= 63) {
        auto b = std::to_integer<std::uint8_t>(data[pos++]);
        value |= static_cast<std::uint64_t>(b & 0x7f) << shift;
        if ((b & 0x80) == 0) return value;
        shift += 7;
    }
    return std::nullopt;
}

} // namespace detail

export auto parse_schema(std::string_view proto_text)
    -> std::expected<file_def, std::error_code>
{
    detail::parser p(detail::strip_comments(proto_text));
    return p.parse_file();
}

export auto encode_varint(std::uint64_t value) -> byte_buffer {
    byte_buffer out;
    detail::put_varint(out, value);
    return out;
}

export auto decode_varint(std::span<const std::byte> data, std::size_t& pos)
    -> std::optional<std::uint64_t>
{
    return detail::read_varint(data, pos);
}

export auto zigzag_encode(std::int64_t value) noexcept -> std::uint64_t {
    return (static_cast<std::uint64_t>(value) << 1) ^
           static_cast<std::uint64_t>(value >> 63);
}

export auto zigzag_decode(std::uint64_t value) noexcept -> std::int64_t {
    return static_cast<std::int64_t>((value >> 1) ^ (~(value & 1) + 1));
}

export void append_key(byte_buffer& out, std::uint32_t number, wire_type type) {
    detail::put_varint(out, (static_cast<std::uint64_t>(number) << 3) |
                            static_cast<std::uint8_t>(type));
}

export void append_uint64(byte_buffer& out, std::uint32_t number, std::uint64_t value) {
    append_key(out, number, wire_type::varint);
    detail::put_varint(out, value);
}

export void append_int64(byte_buffer& out, std::uint32_t number, std::int64_t value) {
    append_uint64(out, number, static_cast<std::uint64_t>(value));
}

export void append_sint64(byte_buffer& out, std::uint32_t number, std::int64_t value) {
    append_uint64(out, number, zigzag_encode(value));
}

export void append_bool(byte_buffer& out, std::uint32_t number, bool value) {
    append_uint64(out, number, value ? 1 : 0);
}

export void append_bytes(byte_buffer& out, std::uint32_t number, std::span<const std::byte> value) {
    append_key(out, number, wire_type::length_delimited);
    detail::put_varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

export void append_string(byte_buffer& out, std::uint32_t number, std::string_view value) {
    append_bytes(out, number, std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(value.data()), value.size()});
}

export auto decode_message(std::span<const std::byte> data)
    -> std::expected<std::vector<field>, std::error_code>
{
    std::vector<field> fields;
    std::size_t pos = 0;
    while (pos < data.size()) {
        auto key = detail::read_varint(data, pos);
        if (!key) return std::unexpected(make_error_code(std::errc::protocol_error));
        field f{
            .number = static_cast<std::uint32_t>(*key >> 3),
            .type = static_cast<wire_type>(*key & 0x07),
        };
        if (f.number == 0) return std::unexpected(make_error_code(std::errc::protocol_error));
        switch (f.type) {
        case wire_type::varint: {
            auto value = detail::read_varint(data, pos);
            if (!value) return std::unexpected(make_error_code(std::errc::protocol_error));
            f.varint_value = *value;
            break;
        }
        case wire_type::fixed64:
            if (pos + 8 > data.size()) return std::unexpected(make_error_code(std::errc::protocol_error));
            for (unsigned i = 0; i < 8; ++i) {
                f.fixed64_value |= static_cast<std::uint64_t>(
                    std::to_integer<std::uint8_t>(data[pos + i])) << (8 * i);
            }
            pos += 8;
            break;
        case wire_type::length_delimited: {
            auto len = detail::read_varint(data, pos);
            if (!len || pos + *len > data.size()) {
                return std::unexpected(make_error_code(std::errc::protocol_error));
            }
            f.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(pos),
                           data.begin() + static_cast<std::ptrdiff_t>(pos + *len));
            pos += static_cast<std::size_t>(*len);
            break;
        }
        case wire_type::fixed32:
            if (pos + 4 > data.size()) return std::unexpected(make_error_code(std::errc::protocol_error));
            for (unsigned i = 0; i < 4; ++i) {
                f.fixed32_value |= static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(data[pos + i])) << (8 * i);
            }
            pos += 4;
            break;
        default:
            return std::unexpected(make_error_code(std::errc::protocol_error));
        }
        fields.push_back(std::move(f));
    }
    return fields;
}

export auto find_first(std::span<const field> fields, std::uint32_t number)
    -> const field*
{
    auto it = std::ranges::find_if(fields, [number](const field& f) {
        return f.number == number;
    });
    return it == fields.end() ? nullptr : &*it;
}

export auto field_string(const field& f) -> std::optional<std::string> {
    if (f.type != wire_type::length_delimited) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(f.bytes.data()), f.bytes.size());
}

export auto field_bytes(const field& f) -> std::optional<byte_buffer> {
    if (f.type != wire_type::length_delimited) return std::nullopt;
    return f.bytes;
}

export auto field_uint64(const field& f) -> std::optional<std::uint64_t> {
    if (f.type != wire_type::varint) return std::nullopt;
    return f.varint_value;
}

export auto field_bool(const field& f) -> std::optional<bool> {
    if (f.type != wire_type::varint) return std::nullopt;
    return f.varint_value != 0;
}

export auto find_message(const file_def& file, std::string_view name)
    -> const message_def*
{
    auto it = std::ranges::find_if(file.messages, [name](const message_def& msg) {
        return msg.name == name;
    });
    return it == file.messages.end() ? nullptr : &*it;
}

export auto find_service(const file_def& file, std::string_view name)
    -> const service_def*
{
    auto it = std::ranges::find_if(file.services, [name](const service_def& svc) {
        return svc.name == name;
    });
    return it == file.services.end() ? nullptr : &*it;
}

export auto find_rpc(const service_def& service, std::string_view name)
    -> const rpc_def*
{
    auto it = std::ranges::find_if(service.rpcs, [name](const rpc_def& rpc) {
        return rpc.name == name;
    });
    return it == service.rpcs.end() ? nullptr : &*it;
}

export auto rpc_path(const file_def& file, const service_def& service, const rpc_def& rpc)
    -> std::string
{
    std::string full_service;
    if (!file.package.empty()) {
        full_service += file.package;
        full_service.push_back('.');
    }
    full_service += service.name;
    return cnetmod::grpc::service_path(full_service, rpc.name);
}

export auto rpc_kind(const rpc_def& rpc) noexcept -> cnetmod::grpc::call_kind {
    if (rpc.client_streaming && rpc.server_streaming) return cnetmod::grpc::call_kind::bidi_streaming;
    if (rpc.client_streaming) return cnetmod::grpc::call_kind::client_streaming;
    if (rpc.server_streaming) return cnetmod::grpc::call_kind::server_streaming;
    return cnetmod::grpc::call_kind::unary;
}

} // namespace cnetmod::grpc::proto
