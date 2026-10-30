#pragma once

namespace cnetmod::mongodb::wire_detail {
template <class T>
void append_le(std::vector<std::byte>& out, T value)
{
    using U = std::make_unsigned_t<T>;
    auto bits = static_cast<U>(value);
    for (std::size_t i{}; i < sizeof(T); ++i)
        out.push_back(static_cast<std::byte>((bits >> (i * 8)) & 0xff));
}

template <class T>
auto read_le(std::span<const std::byte> bytes, std::size_t& pos) -> std::optional<T>
{
    if (pos > bytes.size() || bytes.size() - pos < sizeof(T))
        return {};
    using U = std::make_unsigned_t<T>;
    U bits{};
    for (std::size_t i{}; i < sizeof(T); ++i)
        bits |= static_cast<U>(std::to_integer<unsigned>(bytes[pos++])) << (8 * i);
    return static_cast<T>(bits);
}

} // namespace cnetmod::mongodb::wire_detail
