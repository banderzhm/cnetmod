namespace cnetmod::concurrent_containers {

template <class K, class V, class H, class E>
slot_latch_hash_map<K, V, H, E>::slot_latch_hash_map(std::size_t capacity)
{
    capacity_ = std::bit_ceil(std::max<std::size_t>(capacity, 2U));
    mask_ = capacity_ - 1U;
    slots_ = std::make_unique<slot[]>(capacity_);
}

template <class K, class V, class H, class E>
auto slot_latch_hash_map<K, V, H, E>::slot_index(const K& key) const -> std::size_t
{
    return hash_(key) & mask_;
}

template <class K, class V, class H, class E>
auto slot_latch_hash_map<K, V, H, E>::try_emplace(K key, V value) -> bool
{
    for (;;)
    {
        slot* reusable{};
        bool retry{};
        for (std::size_t step{}; step < capacity_; ++step)
        {
            auto& current = slots_[(slot_index(key) + step) & mask_];
            exclusive_latch_guard lock(current.latch);
            if (current.state == slot_state::occupied && equal_(*current.key, key))
                return false;
            if (current.state == slot_state::tombstone && !reusable)
            {
                reusable = &current;
                continue;
            }
            if (current.state != slot_state::empty)
                continue;
            if (!reusable)
            {
                current.key.emplace(std::move(key));
                current.value.emplace(std::move(value));
                current.state = slot_state::occupied;
                size_.fetch_add(1U, std::memory_order_relaxed);
                return true;
            }
            lock.unlock();
            exclusive_latch_guard reusable_lock(reusable->latch);
            if (reusable->state != slot_state::tombstone)
            {
                retry = true;
                break;
            }
            reusable->key.emplace(std::move(key));
            reusable->value.emplace(std::move(value));
            reusable->state = slot_state::occupied;
            size_.fetch_add(1U, std::memory_order_relaxed);
            return true;
        }
        if (retry)
            continue;
        if (!reusable)
            return false;
        exclusive_latch_guard reusable_lock(reusable->latch);
        if (reusable->state != slot_state::tombstone)
            continue;
        reusable->key.emplace(std::move(key));
        reusable->value.emplace(std::move(value));
        reusable->state = slot_state::occupied;
        size_.fetch_add(1U, std::memory_order_relaxed);
        return true;
    }
}

template <class K, class V, class H, class E>
void slot_latch_hash_map<K, V, H, E>::insert_or_assign(K key, V value)
{
    for (;;)
    {
        slot* reusable{};
        bool retry{};
        for (std::size_t step{}; step < capacity_; ++step)
        {
            auto& current = slots_[(slot_index(key) + step) & mask_];
            exclusive_latch_guard lock(current.latch);
            if (current.state == slot_state::occupied && equal_(*current.key, key))
            {
                current.value.emplace(std::move(value));
                return;
            }
            if (current.state == slot_state::tombstone && !reusable)
            {
                reusable = &current;
                continue;
            }
            if (current.state != slot_state::empty)
                continue;
            if (!reusable)
            {
                current.key.emplace(std::move(key));
                current.value.emplace(std::move(value));
                current.state = slot_state::occupied;
                size_.fetch_add(1U, std::memory_order_relaxed);
                return;
            }
            lock.unlock();
            exclusive_latch_guard reusable_lock(reusable->latch);
            if (reusable->state != slot_state::tombstone)
            {
                retry = true;
                break;
            }
            reusable->key.emplace(std::move(key));
            reusable->value.emplace(std::move(value));
            reusable->state = slot_state::occupied;
            size_.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        if (retry)
            continue;
        if (!reusable)
            throw std::overflow_error("cnetmod::slot_latch_hash_map is full");
        exclusive_latch_guard reusable_lock(reusable->latch);
        if (reusable->state != slot_state::tombstone)
            continue;
        reusable->key.emplace(std::move(key));
        reusable->value.emplace(std::move(value));
        reusable->state = slot_state::occupied;
        size_.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
}

template <class K, class V, class H, class E>
auto slot_latch_hash_map<K, V, H, E>::find(const K& key) const -> std::optional<V>
{
    for (std::size_t step{}; step < capacity_; ++step)
    {
        const auto& current = slots_[(slot_index(key) + step) & mask_];
        shared_latch_guard lock(current.latch);
        if (current.state == slot_state::empty)
            return std::nullopt;
        if (current.state == slot_state::occupied && equal_(*current.key, key))
            return *current.value;
    }
    return std::nullopt;
}

template <class K, class V, class H, class E>
auto slot_latch_hash_map<K, V, H, E>::contains(const K& key) const -> bool
{
    return find(key).has_value();
}

template <class K, class V, class H, class E>
auto slot_latch_hash_map<K, V, H, E>::erase(const K& key) -> bool
{
    for (std::size_t step{}; step < capacity_; ++step)
    {
        auto& current = slots_[(slot_index(key) + step) & mask_];
        exclusive_latch_guard lock(current.latch);
        if (current.state == slot_state::empty)
            return false;
        if (current.state == slot_state::occupied && equal_(*current.key, key))
        {
            current.key.reset();
            current.value.reset();
            current.state = slot_state::tombstone;
            size_.fetch_sub(1U, std::memory_order_relaxed);
            return true;
        }
    }
    return false;
}

template <class K, class V, class H, class E>
auto slot_latch_hash_map<K, V, H, E>::size() const noexcept -> std::size_t
{
    return size_.load(std::memory_order_acquire);
}

template <class K, class V, class H, class E>
auto slot_latch_hash_map<K, V, H, E>::capacity() const noexcept -> std::size_t
{
    return capacity_;
}

} // namespace cnetmod::concurrent_containers
