module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.amqp10:primitive_value;

import std;
export import :described_value;

export namespace cnetmod::amqp10 {
struct value;

struct list : std::vector<value>
{
    using std::vector<value>::vector;
    using std::vector<value>::operator=;

    list() = default;
    list(std::initializer_list<value> entries);
};

struct map : std::vector<std::pair<value, value>>
{
    using std::vector<std::pair<value, value>>::vector;
};

struct array : std::vector<value>
{
    using std::vector<value>::vector;
};

struct value
{
    using storage =
        std::variant<std::monostate, bool, std::uint8_t, std::uint16_t,
            std::uint32_t, std::uint64_t, std::int8_t, std::int16_t,
            std::int32_t, std::int64_t, float, double, char32_t,
            timestamp, std::array<std::byte, 16>, binary, std::string,
            symbol, std::shared_ptr<list>, std::shared_ptr<map>,
            std::shared_ptr<array>, std::shared_ptr<described_value>>;
    storage data{};
    value() = default;

    template <typename T>
    requires(!std::same_as<std::remove_cvref_t<T>, value>)
    value(T&& v)
        : data(std::forward<T>(v))
    {}

    [[nodiscard]] static auto make_list(list entries) -> value;
    [[nodiscard]] static auto make_map(map entries) -> value;
    [[nodiscard]] static auto make_array(array entries) -> value;
    [[nodiscard]] static auto described(descriptor descriptor, value body)
        -> value;
    [[nodiscard]] auto is_null() const noexcept -> bool;
};

inline list::list(std::initializer_list<value> entries)
    : std::vector<value>(entries) {}
} // namespace cnetmod::amqp10
