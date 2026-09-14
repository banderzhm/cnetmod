module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp091:publisher_confirm;
import std;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
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
    /**
     * @brief Reserves a sequence, preserving the counter if allocation fails.
     */
    [[nodiscard]] auto reserve_sequence() -> std::uint64_t;
    void observe(std::weak_ptr<publisher_confirm_observer> observer);
    /**
     * @brief Settles confirmations and isolates failures from every observer.
     *
     * Notifications use an immutable registration snapshot, allowing observers
     * to register additional listeners reentrantly. Exceptions raised by one
     * observer never prevent later observers from receiving the settlement and
     * never escape into the protocol frame pump.
     */
    void settle(std::uint64_t tag, bool acknowledged, bool multiple) noexcept;
    /**
     * @brief Clears pending confirmations and isolates observer failures.
     * Notification uses an immutable registration snapshot without allocation.
     */
    void fail_all(const error& reason) noexcept;
    [[nodiscard]] auto pending() const noexcept -> std::size_t;

private:
    // Confirmation settlement mutates both the sequence set and observer
    // registry.  Keep those mutations in one project-owned atomic latch so a
    // multi-ack cannot race sequence allocation or observer pruning.
    mutable concurrent_containers::atomic_rw_latch state_latch_;
    std::uint64_t next_ = 1;
    std::set<std::uint64_t> pending_;
    using observer_list = std::vector<std::weak_ptr<publisher_confirm_observer>>;
    std::shared_ptr<const observer_list> observers_;
};
} // namespace cnetmod::amqp091
