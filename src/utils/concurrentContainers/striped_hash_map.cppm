export module cnetmod.utils.concurrent_containers.striped_hash_map;

import std;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::concurrent_containers {

/// Segmented, growable hash map. A key selects one stripe; readers and writers
/// contend only with that stripe and a rehash pauses only that stripe.
export template <class Key, class Value, class Hash = std::hash<Key>,
    class Equal = std::equal_to<Key>>
class striped_hash_map
{
public:
    explicit striped_hash_map(std::size_t initial_capacity = 64U, std::size_t stripes = 0U);
    striped_hash_map(const striped_hash_map&) = delete;
    auto operator=(const striped_hash_map&) -> striped_hash_map& = delete;

    [[nodiscard]] auto try_emplace(Key key, Value value) -> bool;
    void insert_or_assign(Key key, Value value);
    /// Mutate exactly one key while its stripe is exclusively latched.  The
    /// callback receives the value and whether this invocation created it;
    /// this preserves read-modify-write invariants without a process-wide
    /// mutex or a stale copy/replace cycle.
    template <class Function>
    requires std::invocable<Function&, Value&, bool> &&
        (!std::is_void_v<std::invoke_result_t<Function&, Value&, bool>>)
    [[nodiscard]] auto update_or_emplace(Key key, Value initial, Function&& update)
        -> std::invoke_result_t<Function&, Value&, bool>;
    [[nodiscard]] auto find(const Key& key) const -> std::optional<Value>;
    [[nodiscard]] auto contains(const Key& key) const -> bool;
    [[nodiscard]] auto erase(const Key& key) -> bool;
    /// Remove matching entries one stripe at a time. Other stripes remain
    /// available throughout the sweep, which makes TTL cleanup suitable for
    /// high-cardinality per-key state such as rate limiters.
    template <class Predicate>
    requires std::predicate<Predicate&, const Key&, const Value&>
    auto erase_if(Predicate&& predicate) -> std::size_t;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto stripe_count() const noexcept -> std::size_t;

private:
    enum class slot_state : unsigned char
    {
        empty,
        occupied,
        tombstone
    };

    struct entry
    {
        std::optional<Key> key;
        std::optional<Value> value;
        slot_state state{slot_state::empty};
    };

    struct stripe
    {
        mutable atomic_rw_latch latch;
        std::unique_ptr<entry[]> entries;
        std::size_t capacity{};
        std::size_t size{};
        std::size_t tombstones{};
    };

    [[nodiscard]] auto stripe_for(const Key& key) -> stripe&;
    [[nodiscard]] auto stripe_for(const Key& key) const -> const stripe&;
    [[nodiscard]] auto entry_index(const Key& key, std::size_t capacity) const -> std::size_t;
    void rehash_unlocked(stripe& target, std::size_t capacity);
    [[nodiscard]] auto insert_unlocked(stripe& target, Key key, Value value, bool assign) -> bool;

    std::unique_ptr<stripe[]> stripes_;
    std::size_t stripe_count_{};
    std::atomic<std::size_t> size_{};
    [[no_unique_address]] Hash hash_{};
    [[no_unique_address]] Equal equal_{};
};

} // namespace cnetmod::concurrent_containers

#include "striped_hash_map.inl"
