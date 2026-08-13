export module cnetmod.utils.concurrent_containers.atomic_hash_map;

import std;

namespace cnetmod::concurrent_containers {

/// Fixed-capacity, open-addressed map. Nodes are immutable and published with
/// atomic shared_ptr CAS, so readers never lock and erased keys remain safe
/// tombstones until the map is destroyed.
export template <class Key, class Value, class Hash = std::hash<Key>, class Equal = std::equal_to<Key>>
class atomic_hash_map
{
public:
    explicit atomic_hash_map(std::size_t capacity);
    [[nodiscard]] auto try_emplace(Key key, Value value) -> bool;

    /// Reserve one logical entry and insert only while the active entry count
    /// stays below `maximum_size`. The reservation is atomic, so independent
    /// producers cannot oversubscribe an application-level capacity.
    [[nodiscard]] auto try_emplace_bounded(Key key, Value value,
        std::size_t maximum_size) -> bool;

    [[nodiscard]] auto try_insert_or_assign(Key key, Value value) -> bool;
    void insert_or_assign(Key key, Value value);
    [[nodiscard]] auto find(const Key& key) const -> std::optional<Value>;
    [[nodiscard]] auto contains(const Key& key) const -> bool;
    [[nodiscard]] auto erase(const Key& key) -> bool;

    /// Remove entries for which `predicate(key, value)` returns true. Each
    /// slot is retired with CAS, so concurrent readers retain a safe immutable
    /// snapshot and writers never need a global lock.
    template <class Predicate>
    requires std::predicate<Predicate&, const Key&, const Value&>
    auto erase_if(Predicate&& predicate) -> std::size_t;

    /// Retire the entry selected by `less`. Selection reads immutable entry
    /// snapshots; the winning removal is still protected by a slot CAS.
    template <class Compare>
    requires std::predicate<Compare&, const Key&, const Value&, const Key&, const Value&>
    [[nodiscard]] auto erase_min_by(Compare&& less) -> bool;

    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto capacity() const noexcept -> std::size_t;

private:
    struct entry
    {
        Key key;
        Value value;
        bool erased{};
    };

    struct alignas(64) slot
    {
        // Use the free shared_ptr atomic operations rather than
        // std::atomic<shared_ptr<T>>. The former is the portable C++20
        // interface and remains available with libc++ configurations which
        // intentionally do not provide the atomic<T> specialization.
        std::shared_ptr<const entry> value;
    };

    [[nodiscard]] static auto load_slot(const slot& source) noexcept
        -> std::shared_ptr<const entry>;
    [[nodiscard]] static auto compare_exchange_slot(slot& target,
        std::shared_ptr<const entry>& expected,
        const std::shared_ptr<const entry>& replacement) noexcept -> bool;

    [[nodiscard]] auto slot_index(const Key& key) const -> std::size_t;
    std::unique_ptr<slot[]> slots_;
    std::size_t capacity_{};
    std::size_t mask_{};
    std::atomic<std::size_t> size_{};
    [[no_unique_address]] Hash hash_{};
    [[no_unique_address]] Equal equal_{};
};

} // namespace cnetmod::concurrent_containers

#include "atomic_hash_map.inl"
