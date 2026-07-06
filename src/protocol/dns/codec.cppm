module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.dns.codec;

import std;
import cnetmod.core.error;
import cnetmod.core.address;
import cnetmod.protocol.dns.types;

namespace cnetmod::dns {

namespace detail {

inline auto read_u16(std::span<const std::byte> data, std::size_t pos)
    -> std::optional<std::uint16_t>
{
    if (pos + 2 > data.size()) return std::nullopt;
    return static_cast<std::uint16_t>(
        (std::to_integer<std::uint16_t>(data[pos]) << 8) |
        std::to_integer<std::uint16_t>(data[pos + 1]));
}

inline auto read_u32(std::span<const std::byte> data, std::size_t pos)
    -> std::optional<std::uint32_t>
{
    if (pos + 4 > data.size()) return std::nullopt;
    return (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[pos])) << 24) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[pos + 1])) << 16) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[pos + 2])) << 8) |
           static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[pos + 3]));
}

export inline void put_u16(std::vector<std::byte>& out, std::uint16_t value) {
    out.push_back(static_cast<std::byte>((value >> 8) & 0xff));
    out.push_back(static_cast<std::byte>(value & 0xff));
}

inline void put_u32(std::vector<std::byte>& out, std::uint32_t value) {
    out.push_back(static_cast<std::byte>((value >> 24) & 0xff));
    out.push_back(static_cast<std::byte>((value >> 16) & 0xff));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xff));
    out.push_back(static_cast<std::byte>(value & 0xff));
}

inline auto parse_name(std::span<const std::byte> data, std::size_t& pos,
                       int depth = 0) -> std::optional<std::string>
{
    if (depth > 16) return std::nullopt;
    std::string name;
    std::size_t cursor = pos;
    bool jumped = false;

    while (cursor < data.size()) {
        const auto len = std::to_integer<unsigned char>(data[cursor++]);
        if ((len & 0xc0) == 0xc0) {
            if (cursor >= data.size()) return std::nullopt;
            const auto next = std::to_integer<unsigned char>(data[cursor++]);
            const auto offset = static_cast<std::size_t>(((len & 0x3f) << 8) | next);
            if (offset >= data.size()) return std::nullopt;
            if (!jumped) {
                pos = cursor;
                jumped = true;
            }
            auto jumped_pos = offset;
            auto suffix = parse_name(data, jumped_pos, depth + 1);
            if (!suffix) return std::nullopt;
            if (!name.empty() && !suffix->empty()) name.push_back('.');
            name += *suffix;
            return name;
        }
        if ((len & 0xc0) != 0) return std::nullopt;
        if (len == 0) {
            if (!jumped) pos = cursor;
            return name;
        }
        if (cursor + len > data.size()) return std::nullopt;
        if (!name.empty()) name.push_back('.');
        name.append(reinterpret_cast<const char*>(data.data() + cursor), len);
        cursor += len;
    }
    return std::nullopt;
}

inline auto write_name(std::vector<std::byte>& out, std::string_view name) -> bool {
    if (name.empty() || name == ".") {
        out.push_back(std::byte{0});
        return true;
    }
    while (!name.empty()) {
        auto dot = name.find('.');
        auto label = dot == std::string_view::npos ? name : name.substr(0, dot);
        if (label.size() > 63) return false;
        out.push_back(static_cast<std::byte>(label.size()));
        out.insert(out.end(),
            reinterpret_cast<const std::byte*>(label.data()),
            reinterpret_cast<const std::byte*>(label.data() + label.size()));
        if (dot == std::string_view::npos) break;
        name.remove_prefix(dot + 1);
    }
    out.push_back(std::byte{0});
    return true;
}

inline auto parse_question(std::span<const std::byte> data, std::size_t& pos)
    -> std::optional<question>
{
    auto name = parse_name(data, pos);
    if (!name) return std::nullopt;
    auto type = read_u16(data, pos);
    auto cls = read_u16(data, pos + 2);
    if (!type || !cls) return std::nullopt;
    pos += 4;
    return question{
        .name = std::move(*name),
        .type = static_cast<record_type>(*type),
        .cls = static_cast<record_class>(*cls),
    };
}

inline auto parse_rr(std::span<const std::byte> data, std::size_t& pos)
    -> std::optional<resource_record>
{
    auto name = parse_name(data, pos);
    if (!name) return std::nullopt;
    auto type = read_u16(data, pos);
    auto cls = read_u16(data, pos + 2);
    auto ttl = read_u32(data, pos + 4);
    auto len = read_u16(data, pos + 8);
    if (!type || !cls || !ttl || !len) return std::nullopt;
    pos += 10;
    if (pos + *len > data.size()) return std::nullopt;
    resource_record rr{
        .name = std::move(*name),
        .type = static_cast<record_type>(*type),
        .cls = static_cast<record_class>(*cls),
        .ttl = *ttl,
    };
    rr.data.assign(data.begin() + static_cast<std::ptrdiff_t>(pos),
                   data.begin() + static_cast<std::ptrdiff_t>(pos + *len));
    pos += *len;
    return rr;
}

inline auto write_question(std::vector<std::byte>& out, const question& q) -> bool {
    if (!write_name(out, q.name)) return false;
    put_u16(out, static_cast<std::uint16_t>(q.type));
    put_u16(out, static_cast<std::uint16_t>(q.cls));
    return true;
}

inline auto write_rr(std::vector<std::byte>& out, const resource_record& rr) -> bool {
    if (!write_name(out, rr.name)) return false;
    put_u16(out, static_cast<std::uint16_t>(rr.type));
    put_u16(out, static_cast<std::uint16_t>(rr.cls));
    put_u32(out, rr.ttl);
    if (rr.data.size() > std::numeric_limits<std::uint16_t>::max()) return false;
    put_u16(out, static_cast<std::uint16_t>(rr.data.size()));
    out.insert(out.end(), rr.data.begin(), rr.data.end());
    return true;
}

} // namespace detail

export auto parse_message(std::span<const std::byte> data)
    -> std::expected<message, std::error_code>
{
    if (data.size() < 12) {
        return std::unexpected(make_error_code(std::errc::protocol_error));
    }

    auto id = detail::read_u16(data, 0);
    auto flags = detail::read_u16(data, 2);
    auto qd = detail::read_u16(data, 4);
    auto an = detail::read_u16(data, 6);
    auto ns = detail::read_u16(data, 8);
    auto ar = detail::read_u16(data, 10);
    if (!id || !flags || !qd || !an || !ns || !ar) {
        return std::unexpected(make_error_code(std::errc::protocol_error));
    }

    message msg{
        .id = *id,
        .query = ((*flags & 0x8000) == 0),
        .authoritative = (*flags & 0x0400) != 0,
        .truncated = (*flags & 0x0200) != 0,
        .recursion_desired = (*flags & 0x0100) != 0,
        .recursion_available = (*flags & 0x0080) != 0,
        .rcode = static_cast<response_code>(*flags & 0x000f),
    };

    std::size_t pos = 12;
    for (std::uint16_t i = 0; i < *qd; ++i) {
        auto q = detail::parse_question(data, pos);
        if (!q) return std::unexpected(make_error_code(std::errc::protocol_error));
        msg.questions.push_back(std::move(*q));
    }
    for (std::uint16_t i = 0; i < *an; ++i) {
        auto rr = detail::parse_rr(data, pos);
        if (!rr) return std::unexpected(make_error_code(std::errc::protocol_error));
        msg.answers.push_back(std::move(*rr));
    }
    for (std::uint16_t i = 0; i < *ns; ++i) {
        auto rr = detail::parse_rr(data, pos);
        if (!rr) return std::unexpected(make_error_code(std::errc::protocol_error));
        msg.authorities.push_back(std::move(*rr));
    }
    for (std::uint16_t i = 0; i < *ar; ++i) {
        auto rr = detail::parse_rr(data, pos);
        if (!rr) return std::unexpected(make_error_code(std::errc::protocol_error));
        msg.additionals.push_back(std::move(*rr));
    }

    return msg;
}

export auto serialize_message(const message& msg)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    if (msg.questions.size() > 65535 || msg.answers.size() > 65535 ||
        msg.authorities.size() > 65535 || msg.additionals.size() > 65535) {
        return std::unexpected(make_error_code(std::errc::message_size));
    }

    std::vector<std::byte> out;
    out.reserve(512);
    detail::put_u16(out, msg.id);
    std::uint16_t flags = 0;
    if (!msg.query) flags |= 0x8000;
    if (msg.authoritative) flags |= 0x0400;
    if (msg.truncated) flags |= 0x0200;
    if (msg.recursion_desired) flags |= 0x0100;
    if (msg.recursion_available) flags |= 0x0080;
    flags |= static_cast<std::uint16_t>(msg.rcode) & 0x000f;
    detail::put_u16(out, flags);
    detail::put_u16(out, static_cast<std::uint16_t>(msg.questions.size()));
    detail::put_u16(out, static_cast<std::uint16_t>(msg.answers.size()));
    detail::put_u16(out, static_cast<std::uint16_t>(msg.authorities.size()));
    detail::put_u16(out, static_cast<std::uint16_t>(msg.additionals.size()));

    for (const auto& q : msg.questions)
        if (!detail::write_question(out, q))
            return std::unexpected(make_error_code(std::errc::invalid_argument));
    for (const auto& rr : msg.answers)
        if (!detail::write_rr(out, rr))
            return std::unexpected(make_error_code(std::errc::invalid_argument));
    for (const auto& rr : msg.authorities)
        if (!detail::write_rr(out, rr))
            return std::unexpected(make_error_code(std::errc::invalid_argument));
    for (const auto& rr : msg.additionals)
        if (!detail::write_rr(out, rr))
            return std::unexpected(make_error_code(std::errc::invalid_argument));
    return out;
}

export auto make_query(std::string_view name, record_type type,
                       std::uint16_t id = 0) -> message
{
    if (id == 0) {
        id = static_cast<std::uint16_t>(
            std::chrono::steady_clock::now().time_since_epoch().count() & 0xffff);
    }
    return message{
        .id = id,
        .query = true,
        .recursion_desired = true,
        .questions = {question{.name = std::string(name), .type = type}},
    };
}

export auto a_record(std::string_view name, const ipv4_address& addr,
                     std::uint32_t ttl = 60) -> resource_record
{
    auto native = addr.native();
    auto* raw = reinterpret_cast<const std::byte*>(&native);
    return resource_record{
        .name = std::string(name),
        .type = record_type::A,
        .cls = record_class::IN,
        .ttl = ttl,
        .data = std::vector<std::byte>(raw, raw + 4),
    };
}

export auto aaaa_record(std::string_view name, const ipv6_address& addr,
                        std::uint32_t ttl = 60) -> resource_record
{
    auto native = addr.native();
    auto* raw = reinterpret_cast<const std::byte*>(&native);
    return resource_record{
        .name = std::string(name),
        .type = record_type::AAAA,
        .cls = record_class::IN,
        .ttl = ttl,
        .data = std::vector<std::byte>(raw, raw + 16),
    };
}

export auto txt_record(std::string_view name, std::string_view text,
                       std::uint32_t ttl = 60) -> std::expected<resource_record, std::error_code>
{
    if (text.size() > 255) {
        return std::unexpected(make_error_code(std::errc::message_size));
    }
    std::vector<std::byte> data;
    data.push_back(static_cast<std::byte>(text.size()));
    data.insert(data.end(),
        reinterpret_cast<const std::byte*>(text.data()),
        reinterpret_cast<const std::byte*>(text.data() + text.size()));
    return resource_record{
        .name = std::string(name),
        .type = record_type::TXT,
        .cls = record_class::IN,
        .ttl = ttl,
        .data = std::move(data),
    };
}

export auto cname_record(std::string_view name, std::string_view canonical,
                         std::uint32_t ttl = 60) -> std::expected<resource_record, std::error_code>
{
    std::vector<std::byte> data;
    if (!detail::write_name(data, canonical)) {
        return std::unexpected(make_error_code(std::errc::invalid_argument));
    }
    return resource_record{
        .name = std::string(name),
        .type = record_type::CNAME,
        .cls = record_class::IN,
        .ttl = ttl,
        .data = std::move(data),
    };
}

} // namespace cnetmod::dns
