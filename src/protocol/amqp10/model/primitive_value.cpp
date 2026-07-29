module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp10;
import :primitive_value;
import std;

namespace cnetmod::amqp10 {
auto value::make_list(list v) -> value
{
    return value{std::make_shared<list>(std::move(v))};
}

auto value::make_map(map v) -> value
{
    return value{std::make_shared<map>(std::move(v))};
}

auto value::make_array(array v) -> value
{
    return value{std::make_shared<array>(std::move(v))};
}

auto value::described(descriptor d, value v) -> value
{
    return value{std::make_shared<described_value>(
        described_value{std::move(d), std::make_shared<value>(std::move(v))})};
}

auto value::is_null() const noexcept -> bool
{
    return std::holds_alternative<std::monostate>(data);
}
} // namespace cnetmod::amqp10
