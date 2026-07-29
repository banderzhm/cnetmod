module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp10:described_value;
import std;

export namespace cnetmod::amqp10 {
using binary = std::vector<std::byte>;
using timestamp = std::chrono::milliseconds;

struct symbol
{
    std::string text;
    symbol() = default;

    symbol(std::string value)
        : text(std::move(value)) {}

    symbol(std::string_view value)
        : text(value) {}

    symbol(const char* value)
        : text(value) {}

    [[nodiscard]] auto empty() const noexcept -> bool
    {
        return text.empty();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t
    {
        return text.size();
    }

    [[nodiscard]] auto data() const noexcept -> const char*
    {
        return text.data();
    }

    operator std::string_view() const noexcept
    {
        return text;
    }

    auto operator<=>(const symbol&) const = default;
};
struct value;

struct descriptor
{
    std::variant<std::uint64_t, symbol> value = std::uint64_t{};
    auto operator==(const descriptor&) const -> bool = default;
};

struct described_value
{
    descriptor type;
    std::shared_ptr<value> body;
};
} // namespace cnetmod::amqp10
