module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.amqp10:reconnect_policy;

import std;

export namespace cnetmod::amqp10 {

struct reconnect_context
{
    std::size_t attempt = 0;
    std::chrono::milliseconds previous_delay{};
};

class reconnect_policy
{
public:
    virtual ~reconnect_policy() = default;
    [[nodiscard]] virtual auto next_delay(const reconnect_context& context) const
        -> std::optional<std::chrono::milliseconds> = 0;
};

class exponential_backoff final : public reconnect_policy
{
public:
    explicit exponential_backoff(
        std::chrono::milliseconds initial = std::chrono::seconds(1),
        std::chrono::milliseconds maximum = std::chrono::seconds(60),
        double multiplier = 2.0, std::size_t maximum_attempts = 0) noexcept;

    [[nodiscard]] auto next_delay(const reconnect_context& context) const
        -> std::optional<std::chrono::milliseconds> override;

private:
    std::chrono::milliseconds initial_;
    std::chrono::milliseconds maximum_;
    double multiplier_;
    std::size_t maximum_attempts_;
};

} // namespace cnetmod::amqp10
