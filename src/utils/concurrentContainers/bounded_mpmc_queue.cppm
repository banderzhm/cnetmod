export module cnetmod.utils.concurrent_containers.queue;

import std;

namespace cnetmod::concurrent_containers {

/// Bounded MPMC queue based on per-slot sequence numbers. Producers and
/// consumers are lock-free; capacity is rounded up to a power of two.
export template <class T>
class bounded_mpmc_queue
{
public:
    explicit bounded_mpmc_queue(std::size_t capacity);
    ~bounded_mpmc_queue();
    bounded_mpmc_queue(const bounded_mpmc_queue&) = delete;
    auto operator=(const bounded_mpmc_queue&) -> bounded_mpmc_queue& = delete;

    template <class... Args>
    [[nodiscard]] auto try_emplace(Args&&... args) -> bool;
    [[nodiscard]] auto try_enqueue(T value) -> bool;
    [[nodiscard]] auto try_dequeue() -> std::optional<T>;
    [[nodiscard]] auto capacity() const noexcept -> std::size_t;
    [[nodiscard]] auto approximate_size() const noexcept -> std::size_t;

private:
    struct alignas(64) cell
    {
        std::atomic<std::size_t> sequence{};
        alignas(T) std::byte storage[sizeof(T)];

        [[nodiscard]] auto value() noexcept -> T*
        {
            return std::launder(reinterpret_cast<T*>(storage));
        }
    };

    struct alignas(64) cursor
    {
        std::atomic<std::size_t> value{};
    };

    std::unique_ptr<cell[]> cells_;
    std::size_t capacity_{};
    std::size_t mask_{};
    cursor enqueue_pos_;
    cursor dequeue_pos_;
};

} // namespace cnetmod::concurrent_containers

#include "bounded_mpmc_queue.inl"
