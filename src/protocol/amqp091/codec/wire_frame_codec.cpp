module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.amqp091;
import :wire_frame_codec;

import std;
import :protocol_constants;
import :field_table_codec;

namespace cnetmod::amqp091 {
namespace {
    class writer
    {
    public:
        void u8(std::uint8_t v)
        {
            data.push_back(static_cast<std::byte>(v));
        }

        template <typename T> void integer(T v)
        {
            for (std::size_t s = sizeof(T); s-- > 0;)
                u8(static_cast<std::uint8_t>(v >> (s * 8)));
        }

        void bytes(std::span<const std::byte> v)
        {
            data.insert(data.end(), v.begin(), v.end());
        }

        void short_string(std::string_view v)
        {
            u8(static_cast<std::uint8_t>(v.size()));
            bytes(std::as_bytes(std::span{v.data(), v.size()}));
        }

        std::vector<std::byte> data;
    };

    class reader
    {
    public:
        explicit reader(std::span<const std::byte> v)
            : data(v) {}

        auto u8() -> std::optional<std::uint8_t>
        {
            if (pos == data.size())
                return {};
            return std::to_integer<std::uint8_t>(data[pos++]);
        }

        template <typename T> auto integer() -> std::optional<T>
        {
            if (data.size() - pos < sizeof(T))
                return {};
            T v{};
            for (std::size_t i = 0; i < sizeof(T); ++i)
                v = static_cast<T>((v << 8) | *u8());
            return v;
        }

        auto bytes(std::size_t n) -> std::optional<std::span<const std::byte>>
        {
            if (data.size() - pos < n)
                return {};
            auto v = data.subspan(pos, n);
            pos += n;
            return v;
        }

        auto short_string() -> std::optional<std::string>
        {
            auto n = u8();
            if (!n)
                return {};
            auto v = bytes(*n);
            if (!v)
                return {};
            return std::string(reinterpret_cast<const char*>(v->data()), v->size());
        }

        std::span<const std::byte> data;
        std::size_t pos = 0;
    };

    auto malformed(std::string text) -> error
    {
        return make_error(error_code::malformed_frame, std::move(text));
    }
} // namespace

frame_parser::frame_parser(std::uint32_t maximum) noexcept
    : frame_max_(std::max(maximum, 4096u)) {}

auto frame_parser::feed(std::span<const std::byte> bytes)
    -> result<std::vector<frame>>
{
    pending_.insert(pending_.end(), bytes.begin(), bytes.end());
    std::vector<frame> output;
    std::size_t used = 0;
    while (pending_.size() - used >= 8)
    {
        reader head(std::span<const std::byte>{pending_}.subspan(used, 7));
        auto type = head.u8();
        auto channel = head.integer<std::uint16_t>();
        auto size = head.integer<std::uint32_t>();
        if (!type || !channel || !size)
            return std::unexpected(malformed("truncated frame header"));
        if (*size > frame_max_ - 8)
            return std::unexpected(make_error(error_code::frame_too_large,
                "frame exceeds negotiated frame_max"));
        if (pending_.size() - used < 8ull + *size)
            break;
        if (pending_[used + 7 + *size] != frame_end)
            return std::unexpected(malformed("invalid frame end marker"));
        if (*type != 1 && *type != 2 && *type != 3 && *type != 8)
            return std::unexpected(malformed("unknown frame type"));
        frame value{.type = static_cast<frame_type>(*type), .channel = *channel,
            .payload = std::vector<std::byte>(
                pending_.begin() + static_cast<std::ptrdiff_t>(used + 7),
                pending_.begin() + static_cast<std::ptrdiff_t>(used + 7 + *size))};
        output.push_back(std::move(value));
        used += 8 + *size;
    }
    pending_.erase(pending_.begin(),
        pending_.begin() + static_cast<std::ptrdiff_t>(used));
    return output;
}

void frame_parser::reset() noexcept
{
    pending_.clear();
}

auto encode_frame(const frame& value) -> result<std::vector<std::byte>>
{
    if (value.payload.size() > std::numeric_limits<std::uint32_t>::max())
        return std::unexpected(
            make_error(error_code::frame_too_large, "payload exceeds uint32"));
    writer out;
    out.u8(static_cast<std::uint8_t>(value.type));
    out.integer(value.channel);
    out.integer(static_cast<std::uint32_t>(value.payload.size()));
    out.bytes(value.payload);
    out.data.push_back(frame_end);
    return std::move(out.data);
}

auto encode_method(const method_frame& value) -> result<frame>
{
    writer out;
    out.integer(value.class_id);
    out.integer(value.method_id);
    out.bytes(value.arguments);
    return frame{.type = frame_type::method,
        .channel = value.channel,
        .payload = std::move(out.data)};
}

auto decode_method(const frame& value) -> result<method_frame>
{
    if (value.type != frame_type::method)
        return std::unexpected(
            make_error(error_code::unexpected_frame, "expected method frame"));
    reader in(value.payload);
    auto cls = in.integer<std::uint16_t>();
    auto id = in.integer<std::uint16_t>();
    if (!cls || !id)
        return std::unexpected(malformed("truncated method frame"));
    method_frame method{
        .channel = value.channel,
        .class_id = *cls,
        .method_id = *id,
        .arguments = std::vector<std::byte>(value.payload.begin() + 4, value.payload.end())};
    return method;
}

auto encode_content_header(const content_header& value) -> result<frame>
{
    writer out;
    out.integer(value.class_id);
    out.integer<std::uint16_t>(0);
    out.integer(value.body_size);
    std::uint16_t flags = 0;
    if (!value.properties.content_type.empty())
        flags |= 1u << 15;
    if (!value.properties.content_encoding.empty())
        flags |= 1u << 14;
    if (!value.properties.headers.empty())
        flags |= 1u << 13;
    if (value.properties.durable)
        flags |= 1u << 12;
    if (!value.properties.correlation_id.empty())
        flags |= 1u << 10;
    if (!value.properties.reply_to.empty())
        flags |= 1u << 9;
    if (value.properties.ttl)
        flags |= 1u << 8;
    if (!value.properties.message_id.empty())
        flags |= 1u << 7;
    if (value.properties.timestamp)
        flags |= 1u << 6;
    out.integer(flags);
    if (flags & (1u << 15))
        out.short_string(value.properties.content_type);
    if (flags & (1u << 14))
        out.short_string(value.properties.content_encoding);
    if (flags & (1u << 13))
    {
        field_table table;
        for (const auto& [key, text] : value.properties.headers)
            table.values[key] = text;
        auto encoded = encode_field_table(table);
        if (!encoded)
            return std::unexpected(encoded.error());
        out.bytes(*encoded);
    }
    if (flags & (1u << 12))
        out.u8(2);
    if (flags & (1u << 10))
        out.short_string(value.properties.correlation_id);
    if (flags & (1u << 9))
        out.short_string(value.properties.reply_to);
    if (flags & (1u << 8))
        out.short_string(std::to_string(value.properties.ttl->count()));
    if (flags & (1u << 7))
        out.short_string(value.properties.message_id);
    if (flags & (1u << 6))
        out.integer(static_cast<std::uint64_t>(*value.properties.timestamp));
    return frame{.type = frame_type::header,
        .channel = value.channel,
        .payload = std::move(out.data)};
}

auto decode_content_header(const frame& value) -> result<content_header>
{
    if (value.type != frame_type::header)
        return std::unexpected(
            make_error(error_code::unexpected_frame, "expected content header"));
    reader in(value.payload);
    auto cls = in.integer<std::uint16_t>();
    auto weight = in.integer<std::uint16_t>();
    auto size = in.integer<std::uint64_t>();
    auto flags = in.integer<std::uint16_t>();
    if (!cls || !weight || !size || !flags || *weight != 0)
        return std::unexpected(malformed("invalid content header"));
    content_header header{
        .channel = value.channel,
        .class_id = *cls,
        .body_size = *size,
        .properties = {}};
    auto text = [&]() -> result<std::string>
    {
        auto v = in.short_string();
        if (!v)
            return std::unexpected(malformed("truncated content property"));
        return *v;
    };
    if (*flags & (1u << 15))
    {
        auto v = text();
        if (!v)
            return std::unexpected(v.error());
        header.properties.content_type = std::move(*v);
    }
    if (*flags & (1u << 14))
    {
        auto v = text();
        if (!v)
            return std::unexpected(v.error());
        header.properties.content_encoding = std::move(*v);
    }
    if (*flags & (1u << 13))
    {
        std::size_t n = 0;
        auto table = decode_field_table(
            std::span<const std::byte>{value.payload}.subspan(in.pos), n);
        if (!table)
            return std::unexpected(table.error());
        in.pos += n;
        for (const auto& [key, item] : table->values)
            if (auto v = std::get_if<std::string>(&item))
                header.properties.headers[key] = *v;
    }
    if (*flags & (1u << 12))
    {
        auto v = in.u8();
        if (!v)
            return std::unexpected(malformed("truncated delivery mode"));
        header.properties.durable = *v == 2;
    }
    if (*flags & (1u << 11))
    {
        if (!in.u8())
            return std::unexpected(malformed("truncated priority"));
    }
    if (*flags & (1u << 10))
    {
        auto v = text();
        if (!v)
            return std::unexpected(v.error());
        header.properties.correlation_id = std::move(*v);
    }
    if (*flags & (1u << 9))
    {
        auto v = text();
        if (!v)
            return std::unexpected(v.error());
        header.properties.reply_to = std::move(*v);
    }
    if (*flags & (1u << 8))
    {
        auto v = text();
        if (!v)
            return std::unexpected(v.error());
        std::int64_t ms{};
        auto [p, ec] = std::from_chars(v->data(), v->data() + v->size(), ms);
        if (ec == std::errc{})
            header.properties.ttl = std::chrono::milliseconds{ms};
    }
    if (*flags & (1u << 7))
    {
        auto v = text();
        if (!v)
            return std::unexpected(v.error());
        header.properties.message_id = std::move(*v);
    }
    if (*flags & (1u << 6))
    {
        auto v = in.integer<std::uint64_t>();
        if (!v)
            return std::unexpected(malformed("truncated timestamp"));
        header.properties.timestamp = static_cast<std::int64_t>(*v);
    }
    for (int bit : {5, 4, 3, 2})
        if (*flags & (1u << bit))
            if (!in.short_string())
                return std::unexpected(malformed("truncated content property"));
    if (*flags & 1u)
        return std::unexpected(malformed("multi-word property flags unsupported"));
    return header;
}
} // namespace cnetmod::amqp091
