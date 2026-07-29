module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.amqp10;
import :amqp_value_codec;

import std;
import :primitive_value;
import :protocol_error;

namespace cnetmod::amqp10 {
namespace {
    enum type_code : std::uint8_t
    {
        described = 0x00,
        null = 0x40,
        boolean = 0x56,
        boolean_true = 0x41,
        boolean_false = 0x42,
        ubyte = 0x50,
        ushort = 0x60,
        uint = 0x70,
        small_uint = 0x52,
        uint_zero = 0x43,
        ulong = 0x80,
        small_ulong = 0x53,
        ulong_zero = 0x44,
        byte = 0x51,
        short_ = 0x61,
        int_ = 0x71,
        small_int = 0x54,
        long_ = 0x81,
        small_long = 0x55,
        float_ = 0x72,
        double_ = 0x82,
        char_ = 0x73,
        timestamp_ = 0x83,
        uuid = 0x98,
        binary8 = 0xa0,
        binary32 = 0xb0,
        string8 = 0xa1,
        string32 = 0xb1,
        symbol8 = 0xa3,
        symbol32 = 0xb3,
        list0 = 0x45,
        list8 = 0xc0,
        list32 = 0xd0,
        map8 = 0xc1,
        map32 = 0xd1,
        array8 = 0xe0,
        array32 = 0xf0
    };

    template <typename UInt> void append_be(binary& out, UInt v)
    {
        for (std::size_t i = 0; i < sizeof(UInt); ++i)
            out.push_back(std::byte((v >> ((sizeof(UInt) - i - 1) * 8)) & 0xff));
    }

    template <typename UInt>
    auto consume_be(std::span<const std::byte> input, std::size_t& pos)
        -> std::expected<UInt, std::error_code>
    {
        if (input.size() - pos < sizeof(UInt))
            return std::unexpected(make_error_code(errc::malformed_frame));
        UInt result{};
        for (std::size_t i = 0; i < sizeof(UInt); ++i)
            result = static_cast<UInt>((result << 8) |
                std::to_integer<unsigned>(input[pos++]));
        return result;
    }
} // namespace

struct encoder::impl
{
    binary output;
};

encoder::encoder()
    : impl_(std::make_unique<impl>()) {}

encoder::~encoder() = default;
encoder::encoder(encoder&&) noexcept = default;
auto encoder::operator=(encoder&&) noexcept -> encoder& = default;

void encoder::write_u8(std::uint8_t v)
{
    impl_->output.push_back(std::byte(v));
}

void encoder::write_u16(std::uint16_t v)
{
    append_be(impl_->output, v);
}

void encoder::write_u32(std::uint32_t v)
{
    append_be(impl_->output, v);
}

void encoder::write_u64(std::uint64_t v)
{
    append_be(impl_->output, v);
}

void encoder::write_bytes(std::span<const std::byte> v)
{
    impl_->output.insert(impl_->output.end(), v.begin(), v.end());
}

void encoder::write_value(const value& input)
{
    auto& out = impl_->output;
    std::visit(
        [&](const auto& item)
        {
            using T = std::remove_cvref_t<decltype(item)>;
            if constexpr (std::same_as<T, std::monostate>)
                write_u8(null);
            else if constexpr (std::same_as<T, bool>)
                write_u8(item ? boolean_true : boolean_false);
            else if constexpr (std::same_as<T, std::uint8_t>)
            {
                write_u8(ubyte);
                write_u8(item);
            }
            else if constexpr (std::same_as<T, std::uint16_t>)
            {
                write_u8(ushort);
                write_u16(item);
            }
            else if constexpr (std::same_as<T, std::uint32_t>)
            {
                if (!item)
                    write_u8(uint_zero);
                else if (item <= 255)
                {
                    write_u8(small_uint);
                    write_u8(item);
                }
                else
                {
                    write_u8(uint);
                    write_u32(item);
                }
            }
            else if constexpr (std::same_as<T, std::uint64_t>)
            {
                if (!item)
                    write_u8(ulong_zero);
                else if (item <= 255)
                {
                    write_u8(small_ulong);
                    write_u8(item);
                }
                else
                {
                    write_u8(ulong);
                    write_u64(item);
                }
            }
            else if constexpr (std::same_as<T, std::int8_t>)
            {
                write_u8(byte);
                write_u8(static_cast<std::uint8_t>(item));
            }
            else if constexpr (std::same_as<T, std::int16_t>)
            {
                write_u8(short_);
                write_u16(static_cast<std::uint16_t>(item));
            }
            else if constexpr (std::same_as<T, std::int32_t>)
            {
                write_u8(int_);
                write_u32(static_cast<std::uint32_t>(item));
            }
            else if constexpr (std::same_as<T, std::int64_t>)
            {
                write_u8(long_);
                write_u64(static_cast<std::uint64_t>(item));
            }
            else if constexpr (std::same_as<T, float>)
            {
                write_u8(float_);
                write_u32(std::bit_cast<std::uint32_t>(item));
            }
            else if constexpr (std::same_as<T, double>)
            {
                write_u8(double_);
                write_u64(std::bit_cast<std::uint64_t>(item));
            }
            else if constexpr (std::same_as<T, char32_t>)
            {
                write_u8(char_);
                write_u32(static_cast<std::uint32_t>(item));
            }
            else if constexpr (std::same_as<T, timestamp>)
            {
                write_u8(timestamp_);
                write_u64(static_cast<std::uint64_t>(item.count()));
            }
            else if constexpr (std::same_as<T, std::array<std::byte, 16>>)
            {
                write_u8(uuid);
                write_bytes(item);
            }
            else if constexpr (std::same_as<T, binary>)
            {
                if (item.size() <= 255)
                {
                    write_u8(binary8);
                    write_u8(item.size());
                }
                else
                {
                    write_u8(binary32);
                    write_u32(static_cast<std::uint32_t>(item.size()));
                }
                write_bytes(item);
            }
            else if constexpr (std::same_as<T, std::string> ||
                std::same_as<T, symbol>)
            {
                constexpr auto sym = std::same_as<T, symbol>;
                if (item.size() <= 255)
                {
                    write_u8(sym ? symbol8 : string8);
                    write_u8(item.size());
                }
                else
                {
                    write_u8(sym ? symbol32 : string32);
                    write_u32(static_cast<std::uint32_t>(item.size()));
                }
                if constexpr (sym)
                    write_bytes(std::as_bytes(std::span(item.text)));
                else
                    write_bytes(std::as_bytes(std::span(item)));
            }
            else if constexpr (std::same_as<T, std::shared_ptr<list>>)
            {
                if (!item || item->empty())
                {
                    write_u8(list0);
                    return;
                }
                encoder nested;
                for (const auto& v : *item)
                    nested.write_value(v);
                auto payload = nested.release();
                if (payload.size() + 1 <= 255)
                {
                    write_u8(list8);
                    write_u8(payload.size() + 1);
                    write_u8(item->size());
                }
                else
                {
                    write_u8(list32);
                    write_u32(static_cast<std::uint32_t>(payload.size() + 4));
                    write_u32(static_cast<std::uint32_t>(item->size()));
                }
                write_bytes(payload);
            }
            else if constexpr (std::same_as<T, std::shared_ptr<map>>)
            {
                encoder nested;
                if (item)
                    for (const auto& [k, v] : *item)
                    {
                        nested.write_value(k);
                        nested.write_value(v);
                    }
                auto payload = nested.release();
                const auto count = item ? item->size() * 2 : 0;
                if (payload.size() + 1 <= 255)
                {
                    write_u8(map8);
                    write_u8(payload.size() + 1);
                    write_u8(count);
                }
                else
                {
                    write_u8(map32);
                    write_u32(static_cast<std::uint32_t>(payload.size() + 4));
                    write_u32(static_cast<std::uint32_t>(count));
                }
                write_bytes(payload);
            }
            else if constexpr (std::same_as<T, std::shared_ptr<array>>)
            {
                if (!item || item->empty())
                {
                    write_u8(array8);
                    write_u8(2);
                    write_u8(0);
                    write_u8(null);
                    return;
                }
                std::vector<binary> encoded;
                encoded.reserve(item->size());
                for (const auto& v : *item)
                {
                    encoder one;
                    one.write_value(v);
                    encoded.push_back(one.release());
                }
                const auto constructor = encoded.front().front();
                binary payload{constructor};
                for (const auto& entry : encoded)
                {
                    if (entry.empty() || entry.front() != constructor)
                        throw std::invalid_argument(
                            "AMQP array elements must share a constructor");
                    payload.insert(payload.end(), entry.begin() + 1, entry.end());
                }
                if (payload.size() + 1 <= 255)
                {
                    write_u8(array8);
                    write_u8(payload.size() + 1);
                    write_u8(item->size());
                }
                else
                {
                    write_u8(array32);
                    write_u32(static_cast<std::uint32_t>(payload.size() + 4));
                    write_u32(static_cast<std::uint32_t>(item->size()));
                }
                write_bytes(payload);
            }
            else if constexpr (std::same_as<T,
                                   std::shared_ptr<described_value>>)
            {
                if (!item || !item->body)
                {
                    write_u8(null);
                    return;
                }
                write_u8(described);
                std::visit([&](const auto& d)
                    {
                        write_value(value{d});
                    },
                    item->type.value);
                write_value(*item->body);
            }
        },
        input.data);
}

auto encoder::bytes() const noexcept -> std::span<const std::byte>
{
    return impl_->output;
}

auto encoder::release() -> binary
{
    return std::move(impl_->output);
}

struct decoder::impl
{
    std::span<const std::byte> input;
    std::size_t pos = 0;
};

decoder::decoder(std::span<const std::byte> input) noexcept
    : impl_(std::make_unique<impl>(impl{input})) {}

decoder::~decoder() = default;
decoder::decoder(decoder&&) noexcept = default;
auto decoder::operator=(decoder&&) noexcept -> decoder& = default;

auto decoder::read_u8() -> std::expected<std::uint8_t, std::error_code>
{
    if (impl_->pos == impl_->input.size())
        return std::unexpected(make_error_code(errc::malformed_frame));
    return std::to_integer<std::uint8_t>(impl_->input[impl_->pos++]);
}

auto decoder::read_u16() -> std::expected<std::uint16_t, std::error_code>
{
    return consume_be<std::uint16_t>(impl_->input, impl_->pos);
}

auto decoder::read_u32() -> std::expected<std::uint32_t, std::error_code>
{
    return consume_be<std::uint32_t>(impl_->input, impl_->pos);
}

auto decoder::read_u64() -> std::expected<std::uint64_t, std::error_code>
{
    return consume_be<std::uint64_t>(impl_->input, impl_->pos);
}

auto decoder::read_bytes(std::size_t count)
    -> std::expected<std::span<const std::byte>, std::error_code>
{
    if (count > remaining())
        return std::unexpected(make_error_code(errc::malformed_frame));
    auto result = impl_->input.subspan(impl_->pos, count);
    impl_->pos += count;
    return result;
}

auto decoder::remaining() const noexcept -> std::size_t
{
    return impl_->input.size() - impl_->pos;
}

auto decoder::read_value() -> std::expected<value, std::error_code>
{
    auto tc = read_u8();
    if (!tc)
        return std::unexpected(tc.error());
    auto read_counted = [&](bool wide)
        -> std::expected<std::span<const std::byte>, std::error_code>
    {
        std::uint32_t count{};
        if (wide)
        {
            auto n = read_u32();
            if (!n)
                return std::unexpected(n.error());
            count = *n;
        }
        else
        {
            auto n = read_u8();
            if (!n)
                return std::unexpected(n.error());
            count = *n;
        }
        return read_bytes(count);
    };
    switch (*tc)
    {
    case null:
        return value{};
    case boolean_true:
        return value{true};
    case boolean_false:
        return value{false};
    case boolean:
    {
        auto v = read_u8();
        if (!v)
            return std::unexpected(v.error());
        return value{*v != 0};
    }
    case ubyte:
    {
        auto v = read_u8();
        if (!v)
            return std::unexpected(v.error());
        return value{*v};
    }
    case ushort:
    {
        auto v = read_u16();
        if (!v)
            return std::unexpected(v.error());
        return value{*v};
    }
    case uint_zero:
        return value{std::uint32_t{}};
    case ulong_zero:
        return value{std::uint64_t{}};
    case small_uint:
    {
        auto v = read_u8();
        if (!v)
            return std::unexpected(v.error());
        return value{std::uint32_t(*v)};
    }
    case small_ulong:
    {
        auto v = read_u8();
        if (!v)
            return std::unexpected(v.error());
        return value{std::uint64_t(*v)};
    }
    case uint:
    {
        auto v = read_u32();
        if (!v)
            return std::unexpected(v.error());
        return value{*v};
    }
    case ulong:
    {
        auto v = read_u64();
        if (!v)
            return std::unexpected(v.error());
        return value{*v};
    }
    case byte:
    {
        auto v = read_u8();
        if (!v)
            return std::unexpected(v.error());
        return value{static_cast<std::int8_t>(*v)};
    }
    case short_:
    {
        auto v = read_u16();
        if (!v)
            return std::unexpected(v.error());
        return value{static_cast<std::int16_t>(*v)};
    }
    case int_:
    {
        auto v = read_u32();
        if (!v)
            return std::unexpected(v.error());
        return value{static_cast<std::int32_t>(*v)};
    }
    case long_:
    {
        auto v = read_u64();
        if (!v)
            return std::unexpected(v.error());
        return value{static_cast<std::int64_t>(*v)};
    }
    case float_:
    {
        auto v = read_u32();
        if (!v)
            return std::unexpected(v.error());
        return value{std::bit_cast<float>(*v)};
    }
    case double_:
    {
        auto v = read_u64();
        if (!v)
            return std::unexpected(v.error());
        return value{std::bit_cast<double>(*v)};
    }
    case char_:
    {
        auto v = read_u32();
        if (!v)
            return std::unexpected(v.error());
        return value{static_cast<char32_t>(*v)};
    }
    case timestamp_:
    {
        auto v = read_u64();
        if (!v)
            return std::unexpected(v.error());
        return value{timestamp{static_cast<std::int64_t>(*v)}};
    }
    case uuid:
    {
        auto b = read_bytes(16);
        if (!b)
            return std::unexpected(b.error());
        std::array<std::byte, 16> id{};
        std::ranges::copy(*b, id.begin());
        return value{id};
    }
    case binary8:
    case binary32:
    {
        auto b = read_counted(*tc == binary32);
        if (!b)
            return std::unexpected(b.error());
        return value{binary(b->begin(), b->end())};
    }
    case string8:
    case string32:
    case symbol8:
    case symbol32:
    {
        auto b = read_counted(*tc == string32 || *tc == symbol32);
        if (!b)
            return std::unexpected(b.error());
        std::string s(reinterpret_cast<const char*>(b->data()), b->size());
        if (*tc == symbol8 || *tc == symbol32)
            return value{symbol(std::move(s))};
        return value{std::move(s)};
    }
    case list0:
        return value::make_list({});
    case list8:
    case list32:
    case map8:
    case map32:
    {
        const bool wide = *tc == list32 || *tc == map32;
        std::uint32_t size_value{};
        std::uint32_t count_value{};
        if (wide)
        {
            auto size = read_u32();
            auto count = read_u32();
            if (!size || !count)
                return std::unexpected(make_error_code(errc::malformed_frame));
            size_value = *size;
            count_value = *count;
        }
        else
        {
            auto size = read_u8();
            auto count = read_u8();
            if (!size || !count)
                return std::unexpected(make_error_code(errc::malformed_frame));
            size_value = *size;
            count_value = *count;
        }
        const auto count = count_value;
        (void)size_value;
        if (*tc == list8 || *tc == list32)
        {
            list items;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                auto v = read_value();
                if (!v)
                    return std::unexpected(v.error());
                items.push_back(std::move(*v));
            }
            return value::make_list(std::move(items));
        }
        map items;
        if (count % 2)
            return std::unexpected(make_error_code(errc::malformed_frame));
        for (std::uint32_t i = 0; i < count / 2; ++i)
        {
            auto k = read_value();
            auto v = read_value();
            if (!k || !v)
                return std::unexpected(make_error_code(errc::malformed_frame));
            items.emplace_back(std::move(*k), std::move(*v));
        }
        return value::make_map(std::move(items));
    }
    case array8:
    case array32:
    {
        std::uint32_t size_value{};
        std::uint32_t count{};
        if (*tc == array32)
        {
            auto s = read_u32();
            auto c = read_u32();
            if (!s || !c)
                return std::unexpected(make_error_code(errc::malformed_frame));
            size_value = *s;
            count = *c;
        }
        else
        {
            auto s = read_u8();
            auto c = read_u8();
            if (!s || !c)
                return std::unexpected(make_error_code(errc::malformed_frame));
            size_value = *s;
            count = *c;
        }
        auto constructor = read_u8();
        if (!constructor)
            return std::unexpected(constructor.error());
        array items;
        items.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (*constructor == symbol8 || *constructor == symbol32)
            {
                std::uint32_t n{};
                if (*constructor == symbol32)
                {
                    auto v = read_u32();
                    if (!v)
                        return std::unexpected(v.error());
                    n = *v;
                }
                else
                {
                    auto v = read_u8();
                    if (!v)
                        return std::unexpected(v.error());
                    n = *v;
                }
                auto bytes = read_bytes(n);
                if (!bytes)
                    return std::unexpected(bytes.error());
                items.emplace_back(symbol{std::string(
                    reinterpret_cast<const char*>(bytes->data()), bytes->size())});
            }
            else
                return std::unexpected(make_error_code(errc::invalid_field));
        }
        (void)size_value;
        return value::make_array(std::move(items));
    }
    case described:
    {
        auto d = read_value();
        auto body = read_value();
        if (!d || !body)
            return std::unexpected(make_error_code(errc::malformed_frame));
        descriptor desc;
        if (auto p = std::get_if<std::uint64_t>(&d->data))
            desc.value = *p;
        else if (auto p = std::get_if<symbol>(&d->data))
            desc.value = *p;
        else
            return std::unexpected(make_error_code(errc::invalid_field));
        return value::described(std::move(desc), std::move(*body));
    }
    default:
        return std::unexpected(make_error_code(errc::malformed_frame));
    }
}
} // namespace cnetmod::amqp10
