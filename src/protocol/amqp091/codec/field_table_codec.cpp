module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.amqp091;
import :field_table_codec;

import std;
import :protocol_constants;

namespace cnetmod::amqp091 {
namespace {

    class writer
    {
    public:
        void u8(std::uint8_t v)
        {
            data_.push_back(static_cast<std::byte>(v));
        }

        void u16(std::uint16_t v)
        {
            integral(v);
        }

        void u32(std::uint32_t v)
        {
            integral(v);
        }

        void u64(std::uint64_t v)
        {
            integral(v);
        }

        void bytes(std::span<const std::byte> v)
        {
            data_.insert(data_.end(), v.begin(), v.end());
        }

        void short_string(std::string_view value)
        {
            u8(static_cast<std::uint8_t>(value.size()));
            bytes(std::as_bytes(std::span{value.data(), value.size()}));
        }

        void long_bytes(std::span<const std::byte> value)
        {
            u32(static_cast<std::uint32_t>(value.size()));
            bytes(value);
        }

        auto take() -> std::vector<std::byte>
        {
            return std::move(data_);
        }

    private:
        template <typename T> void integral(T value)
        {
            for (std::size_t shift = sizeof(T); shift-- > 0;)
                u8(static_cast<std::uint8_t>(value >> (shift * 8)));
        }

        std::vector<std::byte> data_;
    };

    class reader
    {
    public:
        explicit reader(std::span<const std::byte> data)
            : data_(data) {}

        auto u8() -> std::optional<std::uint8_t>
        {
            if (position_ == data_.size())
                return std::nullopt;
            return std::to_integer<std::uint8_t>(data_[position_++]);
        }

        template <typename T> auto integral() -> std::optional<T>
        {
            if (data_.size() - position_ < sizeof(T))
                return std::nullopt;
            T value{};
            for (std::size_t i = 0; i < sizeof(T); ++i)
                value = static_cast<T>((value << 8) | *u8());
            return value;
        }

        auto bytes(std::size_t size) -> std::optional<std::span<const std::byte>>
        {
            if (data_.size() - position_ < size)
                return std::nullopt;
            auto value = data_.subspan(position_, size);
            position_ += size;
            return value;
        }

        auto short_string() -> std::optional<std::string>
        {
            auto size = u8();
            if (!size)
                return std::nullopt;
            auto value = bytes(*size);
            if (!value)
                return std::nullopt;
            return std::string(reinterpret_cast<const char*>(value->data()),
                value->size());
        }

        [[nodiscard]] auto position() const noexcept -> std::size_t
        {
            return position_;
        }

        [[nodiscard]] auto remaining() const noexcept -> std::size_t
        {
            return data_.size() - position_;
        }

    private:
        std::span<const std::byte> data_;
        std::size_t position_ = 0;
    };

    auto encode_value(writer& out, const field_value& value, std::size_t depth)
        -> result<void>;

    auto encode_table_payload(writer& out, const field_table& table,
        std::size_t depth) -> result<void>
    {
        if (depth > 32)
            return std::unexpected(
                make_error(error_code::invalid_field, "field nesting exceeds 32"));
        for (const auto& [key, value] : table.values)
        {
            if (key.size() > 255)
                return std::unexpected(make_error(error_code::invalid_field,
                    "field name exceeds 255 bytes"));
            out.short_string(key);
            if (auto r = encode_value(out, value, depth + 1); !r)
                return r;
        }
        return {};
    }

    auto encode_value(writer& out, const field_value& value, std::size_t depth)
        -> result<void>
    {
        return std::visit(
            [&](const auto& item) -> result<void>
            {
                using type = std::decay_t<decltype(item)>;
                if constexpr (std::same_as<type, std::monostate>)
                    out.u8('V');
                else if constexpr (std::same_as<type, bool>)
                {
                    out.u8('t');
                    out.u8(item ? 1 : 0);
                }
                else if constexpr (std::same_as<type, std::int8_t>)
                {
                    out.u8('b');
                    out.u8(static_cast<std::uint8_t>(item));
                }
                else if constexpr (std::same_as<type, std::uint8_t>)
                {
                    out.u8('B');
                    out.u8(item);
                }
                else if constexpr (std::same_as<type, std::int16_t>)
                {
                    out.u8('U');
                    out.u16(static_cast<std::uint16_t>(item));
                }
                else if constexpr (std::same_as<type, std::uint16_t>)
                {
                    out.u8('u');
                    out.u16(item);
                }
                else if constexpr (std::same_as<type, std::int32_t>)
                {
                    out.u8('I');
                    out.u32(static_cast<std::uint32_t>(item));
                }
                else if constexpr (std::same_as<type, std::uint32_t>)
                {
                    out.u8('i');
                    out.u32(item);
                }
                else if constexpr (std::same_as<type, std::int64_t>)
                {
                    out.u8('L');
                    out.u64(static_cast<std::uint64_t>(item));
                }
                else if constexpr (std::same_as<type, std::uint64_t>)
                {
                    out.u8('l');
                    out.u64(item);
                }
                else if constexpr (std::same_as<type, float>)
                {
                    out.u8('f');
                    out.u32(std::bit_cast<std::uint32_t>(item));
                }
                else if constexpr (std::same_as<type, double>)
                {
                    out.u8('d');
                    out.u64(std::bit_cast<std::uint64_t>(item));
                }
                else if constexpr (std::same_as<type, decimal_value>)
                {
                    out.u8('D');
                    out.u8(item.scale);
                    out.u32(static_cast<std::uint32_t>(item.value));
                }
                else if constexpr (std::same_as<type, std::string>)
                {
                    out.u8('S');
                    out.long_bytes(std::as_bytes(std::span{item.data(), item.size()}));
                }
                else if constexpr (std::same_as<type, std::vector<std::byte>>)
                {
                    out.u8('x');
                    out.long_bytes(item);
                }
                else if constexpr (std::same_as<type, std::shared_ptr<field_table>>)
                {
                    if (!item)
                    {
                        out.u8('V');
                        return {};
                    }
                    out.u8('F');
                    writer nested;
                    if (auto r = encode_table_payload(nested, *item, depth); !r)
                        return r;
                    auto data = nested.take();
                    out.long_bytes(data);
                }
                else if constexpr (std::same_as<type, std::shared_ptr<field_array>>)
                {
                    if (!item)
                    {
                        out.u8('V');
                        return {};
                    }
                    out.u8('A');
                    writer nested;
                    for (const auto& child : item->values)
                        if (auto r = encode_value(nested, child, depth + 1); !r)
                            return r;
                    auto data = nested.take();
                    out.long_bytes(data);
                }
                return {};
            },
            value);
    }

    auto decode_value(reader& in, std::size_t depth) -> result<field_value>;

    auto decode_table_payload(reader& in, std::size_t depth)
        -> result<field_table>
    {
        if (depth > 32)
            return std::unexpected(
                make_error(error_code::invalid_field, "field nesting exceeds 32"));
        field_table table;
        while (in.remaining() != 0)
        {
            auto name = in.short_string();
            if (!name)
                return std::unexpected(
                    make_error(error_code::invalid_field, "truncated field name"));
            auto value = decode_value(in, depth + 1);
            if (!value)
                return std::unexpected(value.error());
            table.values.insert_or_assign(std::move(*name), std::move(*value));
        }
        return table;
    }

    auto decode_value(reader& in, std::size_t depth) -> result<field_value>
    {
        auto tag = in.u8();
        if (!tag)
            return std::unexpected(
                make_error(error_code::invalid_field, "missing field type"));
        auto truncated = []
        {
            return std::unexpected(
                make_error(error_code::invalid_field, "truncated field value"));
        };
        switch (*tag)
        {
        case 'V':
            return field_value{std::monostate{}};
        case 't':
        {
            auto v = in.u8();
            if (!v)
                return truncated();
            return field_value{*v != 0};
        }
        case 'b':
        {
            auto v = in.u8();
            if (!v)
                return truncated();
            return field_value{static_cast<std::int8_t>(*v)};
        }
        case 'B':
        {
            auto v = in.u8();
            if (!v)
                return truncated();
            return field_value{*v};
        }
        case 'U':
        {
            auto v = in.integral<std::uint16_t>();
            if (!v)
                return truncated();
            return field_value{static_cast<std::int16_t>(*v)};
        }
        case 'u':
        {
            auto v = in.integral<std::uint16_t>();
            if (!v)
                return truncated();
            return field_value{*v};
        }
        case 'I':
        {
            auto v = in.integral<std::uint32_t>();
            if (!v)
                return truncated();
            return field_value{static_cast<std::int32_t>(*v)};
        }
        case 'i':
        {
            auto v = in.integral<std::uint32_t>();
            if (!v)
                return truncated();
            return field_value{*v};
        }
        case 'L':
        {
            auto v = in.integral<std::uint64_t>();
            if (!v)
                return truncated();
            return field_value{static_cast<std::int64_t>(*v)};
        }
        case 'l':
        case 'T':
        {
            auto v = in.integral<std::uint64_t>();
            if (!v)
                return truncated();
            return field_value{*v};
        }
        case 'f':
        {
            auto v = in.integral<std::uint32_t>();
            if (!v)
                return truncated();
            return field_value{std::bit_cast<float>(*v)};
        }
        case 'd':
        {
            auto v = in.integral<std::uint64_t>();
            if (!v)
                return truncated();
            return field_value{std::bit_cast<double>(*v)};
        }
        case 's':
        {
            auto v = in.integral<std::uint16_t>();
            if (!v)
                return truncated();
            return field_value{static_cast<std::int16_t>(*v)};
        }
        case 'D':
        {
            auto scale = in.u8();
            auto value = in.integral<std::uint32_t>();
            if (!scale || !value)
                return truncated();
            return field_value{
                decimal_value{*scale, static_cast<std::int32_t>(*value)}};
        }
        case 'S':
        case 'x':
        {
            auto size = in.integral<std::uint32_t>();
            if (!size)
                return truncated();
            auto data = in.bytes(*size);
            if (!data)
                return truncated();
            if (*tag == 'x')
                return field_value{std::vector<std::byte>(data->begin(), data->end())};
            return field_value{std::string(reinterpret_cast<const char*>(data->data()),
                data->size())};
        }
        case 'F':
        case 'A':
        {
            auto size = in.integral<std::uint32_t>();
            if (!size)
                return truncated();
            auto data = in.bytes(*size);
            if (!data)
                return truncated();
            reader nested(*data);
            if (*tag == 'F')
            {
                auto table = decode_table_payload(nested, depth + 1);
                if (!table)
                    return std::unexpected(table.error());
                return field_value{std::make_shared<field_table>(std::move(*table))};
            }
            auto array = std::make_shared<field_array>();
            while (nested.remaining() != 0)
            {
                auto child = decode_value(nested, depth + 1);
                if (!child)
                    return std::unexpected(child.error());
                array->values.push_back(std::move(*child));
            }
            return field_value{std::move(array)};
        }
        default:
            return std::unexpected(
                make_error(error_code::invalid_field, "unsupported AMQP field type"));
        }
    }

} // namespace

auto encode_field_table(const field_table& table)
    -> result<std::vector<std::byte>>
{
    writer payload;
    if (auto r = encode_table_payload(payload, table, 0); !r)
        return std::unexpected(r.error());
    auto bytes = payload.take();
    writer framed;
    framed.long_bytes(bytes);
    return framed.take();
}

auto decode_field_table(std::span<const std::byte> bytes, std::size_t& consumed)
    -> result<field_table>
{
    reader input(bytes);
    auto size = input.integral<std::uint32_t>();
    if (!size)
        return std::unexpected(
            make_error(error_code::invalid_field, "missing table length"));
    auto payload = input.bytes(*size);
    if (!payload)
        return std::unexpected(
            make_error(error_code::invalid_field, "truncated table"));
    reader fields(*payload);
    auto table = decode_table_payload(fields, 0);
    if (!table)
        return table;
    consumed = input.position();
    return table;
}

} // namespace cnetmod::amqp091
