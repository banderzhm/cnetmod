export module cnetmod.utils.concurrent_containers.slot_latch_hash_map;

import std;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::concurrent_containers {

/// Fixed-capacity hash map with one cache-line-isolated reader/writer latch per
/// probe slot.  Independent keys can mutate independent slots concurrently;
/// tombstones preserve probe chains after erase.
export template <class Key, class Value, class Hash = std::hash<Key>,
    class Equal = std::equal_to<Key>>
class slot_latch_hash_map
{
public:
    explicit slot_latch_hash_map(std::size_t capacity);
    slot_latch_hash_map(const slot_latch_hash_map&) = delete;
    auto operator=(const slot_latch_hash_map&) -> slot_latch_hash_map& = delete;

    [[nodiscard]] auto try_emplace(Key key, Value value) -> bool;
    void insert_or_assign(Key key, Value value);
    [[nodiscard]] auto find(const Key& key) const -> std::optional<Value>;
    [[nodiscard]] auto contains(const Key& key) const -> bool;
    [[nodiscard]] auto erase(const Key& key) -> bool;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto capacity() const noexcept -> std::size_t;

private:
    enum class slot_state : unsigned char
    {
        empty,
        occupied,
        tombstone
    };

    struct alignas(64) slot
    {
        mutable atomic_rw_latch latch;
        std::optional<Key> key;
        std::optional<Value> value;
        slot_state state{slot_state::empty};
    };

    [[nodiscard]] auto slot_index(const Key& key) const -> std::size_t;
    std::unique_ptr<slot[]> slots_;
    std::size_t capacity_{};
    std::size_t mask_{};
    std::atomic<std::size_t> size_{};
    [[no_unique_address]] Hash hash_{};
    [[no_unique_address]] Equal equal_{};
};

} // namespace cnetmod::concurrent_containers

#include "slot_latch_hash_map.inl"
