module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp091:publisher_confirm;
import std;
import :protocol_constants;

export namespace cnetmod::amqp091 {
struct publisher_confirmation
{
    std::uint64_t delivery_tag = 0;
    bool acknowledged = false;
    bool multiple = false;
};

class publisher_confirm_observer
{
public:
    virtual ~publisher_confirm_observer() = default;
    virtual void on_confirm(const publisher_confirmation& confirmation) = 0;
    virtual void on_confirm_failure(const error& reason) = 0;
};

class publisher_confirm_tracker
{
public:
    publisher_confirm_tracker() = default;
    [[nodiscard]] auto reserve_sequence() noexcept -> std::uint64_t;
    void observe(std::weak_ptr<publisher_confirm_observer> observer);
    void settle(std::uint64_t tag, bool acknowledged, bool multiple);
    void fail_all(error reason);
    [[nodiscard]] auto pending() const noexcept -> std::size_t;

private:
    mutable std::mutex mutex_;
    std::uint64_t next_ = 1;
    std::set<std::uint64_t> pending_;
    std::vector<std::weak_ptr<publisher_confirm_observer>> observers_;
};
} // namespace cnetmod::amqp091
