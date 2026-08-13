export module cnetmod.utils.concurrent_containers.vector;

import std;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::concurrent_containers {

/// Concurrent growable vector backed by project-owned aligned slots. Reads use
/// shared locking; mutation uses a short exclusive critical section. It never
/// exposes unstable references or iterators.
export template <class T>
class concurrent_vector
{
public:
    concurrent_vector() = default;
    ~concurrent_vector();
    template <class... Args> auto emplace_back(Args&&... args) -> std::size_t;
    void push_back(T value);
    [[nodiscard]] auto try_get(std::size_t index) const -> std::optional<T>;
    template <class Visitor>
    requires std::invocable<Visitor&, const T&>
    void for_each_snapshot(Visitor&& visitor) const;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto empty() const noexcept -> bool;
    void clear();

private:
    struct slot
    {
        alignas(T) std::byte storage[sizeof(T)];
        bool occupied{};

        [[nodiscard]] auto value() noexcept -> T*
        {
            return std::launder(reinterpret_cast<T*>(storage));
        }

        [[nodiscard]] auto value() const noexcept -> const T*
        {
            return std::launder(reinterpret_cast<const T*>(storage));
        }
    };

    void grow_unlocked(std::size_t minimum_capacity);
    mutable atomic_rw_latch latch_;
    std::unique_ptr<slot[]> slots_;
    std::size_t size_{};
    std::size_t capacity_{};
};
} // namespace cnetmod::concurrent_containers

#include "concurrent_vector.inl"
