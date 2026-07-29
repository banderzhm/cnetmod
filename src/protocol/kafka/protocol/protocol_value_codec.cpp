module cnetmod.protocol.kafka.protocol_value_codec;
import std;

namespace cnetmod::kafka::protocol {
namespace {
    auto malformed(std::string m)
    {
        return make_error(error_code::malformed_response, std::move(m));
    }

    template <class T> void put_be(bytes& out, T value)
    {
        using U = std::make_unsigned_t<T>;
        auto v = static_cast<U>(value);
        for (std::size_t i = sizeof(T); i > 0; --i)
            out.push_back(static_cast<std::byte>((v >> ((i - 1) * 8)) & 0xff));
    }

    template <class T>
    auto get_be(std::span<const std::byte> in, std::size_t& pos) -> result<T>
    {
        if (in.size() - pos < sizeof(T))
            return std::unexpected(malformed("truncated integer"));
        using U = std::make_unsigned_t<T>;
        U v = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i)
            v = (v << 8) | std::to_integer<unsigned>(in[pos++]);
        return static_cast<T>(v);
    }

    auto zig32(std::int32_t v) -> std::uint32_t
    {
        return (static_cast<std::uint32_t>(v) << 1) ^
            static_cast<std::uint32_t>(v >> 31);
    }

    auto zig64(std::int64_t v) -> std::uint64_t
    {
        return (static_cast<std::uint64_t>(v) << 1) ^
            static_cast<std::uint64_t>(v >> 63);
    }
} // namespace

void encoder::int8(std::int8_t v)
{
    put_be(data_, v);
}

void encoder::int16(std::int16_t v)
{
    put_be(data_, v);
}

void encoder::int32(std::int32_t v)
{
    put_be(data_, v);
}

void encoder::int64(std::int64_t v)
{
    put_be(data_, v);
}

void encoder::boolean(bool v)
{
    int8(v ? 1 : 0);
}

void encoder::string(std::string_view v)
{
    int16(static_cast<std::int16_t>(v.size()));
    raw({reinterpret_cast<const std::byte*>(v.data()), v.size()});
}

void encoder::nullable_string(const std::optional<std::string>& v)
{
    if (!v)
        int16(-1);
    else
        string(*v);
}

void encoder::raw(std::span<const std::byte> v)
{
    data_.insert(data_.end(), v.begin(), v.end());
}

void encoder::byte_array(const std::optional<bytes>& v)
{
    if (!v)
        int32(-1);
    else
    {
        int32(static_cast<std::int32_t>(v->size()));
        raw(*v);
    }
}

void encoder::unsigned_varint(std::uint32_t v)
{
    while (v >= 0x80)
    {
        data_.push_back(static_cast<std::byte>((v & 0x7f) | 0x80));
        v >>= 7;
    }
    data_.push_back(static_cast<std::byte>(v));
}

void encoder::varint(std::int32_t v)
{
    unsigned_varint(zig32(v));
}

void encoder::varlong(std::int64_t v)
{
    auto n = zig64(v);
    while (n >= 0x80)
    {
        data_.push_back(static_cast<std::byte>((n & 0x7f) | 0x80));
        n >>= 7;
    }
    data_.push_back(static_cast<std::byte>(n));
}

auto encoder::take() && -> bytes
{
    return std::move(data_);
}

decoder::decoder(std::span<const std::byte> in) noexcept
    : input_(in) {}

auto decoder::int8() -> result<std::int8_t>
{
    return get_be<std::int8_t>(input_, pos_);
}

auto decoder::int16() -> result<std::int16_t>
{
    return get_be<std::int16_t>(input_, pos_);
}

auto decoder::int32() -> result<std::int32_t>
{
    return get_be<std::int32_t>(input_, pos_);
}

auto decoder::int64() -> result<std::int64_t>
{
    return get_be<std::int64_t>(input_, pos_);
}

auto decoder::boolean() -> result<bool>
{
    auto v = int8();
    if (!v)
        return std::unexpected(v.error());
    return *v != 0;
}

auto decoder::slice(std::size_t n) -> result<std::span<const std::byte>>
{
    if (n > remaining())
        return std::unexpected(malformed("truncated field"));
    auto r = input_.subspan(pos_, n);
    pos_ += n;
    return r;
}

auto decoder::string() -> result<std::string>
{
    auto n = int16();
    if (!n)
        return std::unexpected(n.error());
    if (*n < 0)
        return std::unexpected(malformed("null non-null string"));
    auto s = slice(*n);
    if (!s)
        return std::unexpected(s.error());
    return std::string(reinterpret_cast<const char*>(s->data()), s->size());
}

auto decoder::nullable_string() -> result<std::optional<std::string>>
{
    auto n = int16();
    if (!n)
        return std::unexpected(n.error());
    if (*n < 0)
        return std::optional<std::string>{};
    auto s = slice(*n);
    if (!s)
        return std::unexpected(s.error());
    return std::optional<std::string>(
        std::string(reinterpret_cast<const char*>(s->data()), s->size()));
}

auto decoder::byte_array() -> result<std::optional<bytes>>
{
    auto n = int32();
    if (!n)
        return std::unexpected(n.error());
    if (*n < 0)
        return std::optional<bytes>{};
    auto s = slice(static_cast<std::size_t>(*n));
    if (!s)
        return std::unexpected(s.error());
    return std::optional<bytes>(bytes(s->begin(), s->end()));
}

auto decoder::unsigned_varint() -> result<std::uint32_t>
{
    std::uint32_t v = 0;
    for (unsigned shift = 0; shift < 35; shift += 7)
    {
        auto b = int8();
        if (!b)
            return std::unexpected(b.error());
        auto u = static_cast<std::uint8_t>(*b);
        v |= static_cast<std::uint32_t>(u & 0x7f) << shift;
        if (!(u & 0x80))
            return v;
    }
    return std::unexpected(malformed("invalid varint"));
}

auto decoder::varint() -> result<std::int32_t>
{
    auto u = unsigned_varint();
    if (!u)
        return std::unexpected(u.error());
    return static_cast<std::int32_t>((*u >> 1) ^
        -static_cast<std::int32_t>(*u & 1));
}

auto decoder::varlong() -> result<std::int64_t>
{
    std::uint64_t v = 0;
    for (unsigned shift = 0; shift < 70; shift += 7)
    {
        auto b = int8();
        if (!b)
            return std::unexpected(b.error());
        auto u = static_cast<std::uint8_t>(*b);
        v |= static_cast<std::uint64_t>(u & 0x7f) << shift;
        if (!(u & 0x80))
            return static_cast<std::int64_t>((v >> 1) ^
                -static_cast<std::int64_t>(v & 1));
    }
    return std::unexpected(malformed("invalid varlong"));
}

auto decoder::remaining() const noexcept -> std::size_t
{
    return input_.size() - pos_;
}

auto encode_request(request_header h, std::span<const std::byte> b) -> bytes
{
    encoder e;
    e.int32(
        static_cast<std::int32_t>(2 + 2 + 4 + 2 + h.client_id.size() + b.size()));
    e.int16(static_cast<std::int16_t>(h.key));
    e.int16(h.version);
    e.int32(h.correlation_id);
    e.string(h.client_id);
    e.raw(b);
    return std::move(e).take();
}

auto decode_response_header(decoder& d) -> result<response_header>
{
    auto id = d.int32();
    if (!id)
        return std::unexpected(id.error());
    return response_header{*id};
}

auto crc32c(std::span<const std::byte> data) noexcept -> std::uint32_t
{
    std::uint32_t crc = ~0u;
    for (auto b : data)
    {
        crc ^= std::to_integer<std::uint8_t>(b);
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0x82f63b78u & -(crc & 1u));
    }
    return ~crc;
}
} // namespace cnetmod::kafka::protocol
