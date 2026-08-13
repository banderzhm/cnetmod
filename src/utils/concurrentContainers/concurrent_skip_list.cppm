export module cnetmod.utils.concurrent_containers.skip_list;

import std;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::concurrent_containers {

/// Ordered concurrent skip list. Structural mutation is guarded by one writer
/// lock; lock-free reads are intentionally not claimed because safe node
/// reclamation needs a dedicated hazard-pointer domain.
export template <class Key, class Value, class Compare = std::less<Key>>
class concurrent_skip_list
{
public:
    concurrent_skip_list();
    ~concurrent_skip_list();
    concurrent_skip_list(const concurrent_skip_list&) = delete;
    auto operator=(const concurrent_skip_list&) -> concurrent_skip_list& = delete;
    [[nodiscard]] auto insert_or_assign(Key key, Value value) -> bool;
    [[nodiscard]] auto find(const Key& key) const -> std::optional<Value>;
    [[nodiscard]] auto erase(const Key& key) -> bool;
    [[nodiscard]] auto contains(const Key& key) const -> bool;
    [[nodiscard]] auto snapshot() const -> std::vector<std::pair<Key, Value>>;
    [[nodiscard]] auto size() const noexcept -> std::size_t;

private:
    static constexpr std::size_t max_level = 16U;

    struct node
    {
        std::optional<Key> key;
        std::optional<Value> value;
        std::array<node*, max_level> next{};
        std::size_t height{};

        explicit node(std::size_t level) : height(level) {}

        node(Key item_key, Value item_value, std::size_t level)
            : key(std::move(item_key)), value(std::move(item_value)), height(level) {}
    };

    [[nodiscard]] auto lower_bound_unlocked(const Key& key,
        std::array<node*, max_level>* update = nullptr) const -> node*;
    [[nodiscard]] auto random_level() noexcept -> std::size_t;
    mutable atomic_rw_latch latch_;
    node* head_{};
    std::size_t level_{1U};
    std::size_t size_{};
    std::uint64_t random_state_{0x9e3779b97f4a7c15ULL};
    [[no_unique_address]] Compare compare_{};
};
} // namespace cnetmod::concurrent_containers

#include "concurrent_skip_list.inl"
