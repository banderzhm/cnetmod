namespace cnetmod::concurrent_containers {

template <class K, class V, class H, class E>
striped_hash_map<K, V, H, E>::striped_hash_map(std::size_t initial_capacity, std::size_t stripes)
{
    stripe_count_ = std::bit_ceil(std::max<std::size_t>(stripes == 0U ? std::thread::hardware_concurrency() : stripes, 2U));
    const auto per_stripe = std::bit_ceil(std::max<std::size_t>((initial_capacity + stripe_count_ - 1U) / stripe_count_, 4U));
    stripes_ = std::make_unique<stripe[]>(stripe_count_);
    for (std::size_t index{}; index < stripe_count_; ++index)
    {
        stripes_[index].capacity = per_stripe;
        stripes_[index].entries = std::make_unique<entry[]>(per_stripe);
    }
}

template <class K, class V, class H, class E>
auto striped_hash_map<K, V, H, E>::stripe_for(const K& key) -> stripe&
{
    return stripes_[hash_(key) & (stripe_count_ - 1U)];
}

template <class K, class V, class H, class E>
auto striped_hash_map<K, V, H, E>::stripe_for(const K& key) const -> const stripe&
{
    return stripes_[hash_(key) & (stripe_count_ - 1U)];
}

template <class K, class V, class H, class E>
auto striped_hash_map<K, V, H, E>::entry_index(const K& key, std::size_t capacity) const -> std::size_t
{
    return hash_(key) & (capacity - 1U);
}

template <class K, class V, class H, class E>
void striped_hash_map<K, V, H, E>::rehash_unlocked(stripe& target, std::size_t capacity)
{
    auto next = std::make_unique<entry[]>(capacity);
    for (std::size_t index{}; index < target.capacity; ++index)
    {
        auto& source = target.entries[index];
        if (source.state != slot_state::occupied)
            continue;
        for (std::size_t step{}; step < capacity; ++step)
        {
            auto& destination = next[(entry_index(*source.key, capacity) + step) & (capacity - 1U)];
            if (destination.state == slot_state::empty)
            {
                destination.key.emplace(std::move(*source.key));
                destination.value.emplace(std::move(*source.value));
                destination.state = slot_state::occupied;
                break;
            }
        }
    }
    target.entries = std::move(next);
    target.capacity = capacity;
    target.tombstones = 0U;
}

template <class K, class V, class H, class E>
auto striped_hash_map<K, V, H, E>::insert_unlocked(stripe& target, K key, V value, bool assign) -> bool
{
    if ((target.size + target.tombstones + 1U) * 10U >= target.capacity * 7U)
        rehash_unlocked(target, target.capacity * 2U);
    std::optional<std::size_t> reusable;
    for (std::size_t step{}; step < target.capacity; ++step)
    {
        auto& current = target.entries[(entry_index(key, target.capacity) + step) & (target.capacity - 1U)];
        if (current.state == slot_state::occupied && equal_(*current.key, key))
        {
            if (assign)
                current.value.emplace(std::move(value));
            return false;
        }
        if (current.state == slot_state::tombstone && !reusable)
            reusable = (entry_index(key, target.capacity) + step) & (target.capacity - 1U);
        if (current.state != slot_state::empty)
            continue;
        auto& destination = reusable ? target.entries[*reusable] : current;
        if (destination.state == slot_state::tombstone)
            --target.tombstones;
        destination.key.emplace(std::move(key));
        destination.value.emplace(std::move(value));
        destination.state = slot_state::occupied;
        ++target.size;
        size_.fetch_add(1U, std::memory_order_relaxed);
        return true;
    }
    return false;
}

template <class K, class V, class H, class E>
auto striped_hash_map<K, V, H, E>::try_emplace(K key, V value) -> bool
{
    auto& target = stripe_for(key);
    exclusive_latch_guard lock(target.latch);
    return insert_unlocked(target, std::move(key), std::move(value), false);
}

template <class K, class V, class H, class E>
void striped_hash_map<K, V, H, E>::insert_or_assign(K key, V value)
{
    auto& target = stripe_for(key);
    exclusive_latch_guard lock(target.latch);
    (void)insert_unlocked(target, std::move(key), std::move(value), true);
}

template <class K, class V, class H, class E>
template <class Function>
requires std::invocable<Function&, V&, bool> &&
    (!std::is_void_v<std::invoke_result_t<Function&, V&, bool>>)
auto striped_hash_map<K, V, H, E>::update_or_emplace(K key, V initial,
    Function&& update) -> std::invoke_result_t<Function&, V&, bool>
{
    auto& target = stripe_for(key);
    exclusive_latch_guard lock(target.latch);
    if ((target.size + target.tombstones + 1U) * 10U >= target.capacity * 7U)
        rehash_unlocked(target, target.capacity * 2U);
    std::optional<std::size_t> reusable;
    for (std::size_t step{}; step < target.capacity; ++step)
    {
        const auto index = (entry_index(key, target.capacity) + step) &
            (target.capacity - 1U);
        auto& current = target.entries[index];
        if (current.state == slot_state::occupied && equal_(*current.key, key))
            return std::invoke(std::forward<Function>(update), *current.value, false);
        if (current.state == slot_state::tombstone && !reusable)
            reusable = index;
        if (current.state != slot_state::empty)
            continue;
        auto& destination = reusable ? target.entries[*reusable] : current;
        if (destination.state == slot_state::tombstone)
            --target.tombstones;
        destination.key.emplace(std::move(key));
        destination.value.emplace(std::move(initial));
        destination.state = slot_state::occupied;
        ++target.size;
        size_.fetch_add(1U, std::memory_order_relaxed);
        return std::invoke(std::forward<Function>(update), *destination.value, true);
    }
    std::terminate();
}

template <class K, class V, class H, class E>
auto striped_hash_map<K, V, H, E>::find(const K& key) const -> std::optional<V>
{
    const auto& target = stripe_for(key);
    shared_latch_guard lock(target.latch);
    for (std::size_t step{}; step < target.capacity; ++step)
    {
        const auto& current = target.entries[(entry_index(key, target.capacity) + step) & (target.capacity - 1U)];
        if (current.state == slot_state::empty)
            return std::nullopt;
        if (current.state == slot_state::occupied && equal_(*current.key, key))
            return *current.value;
    }
    return std::nullopt;
}

template <class K, class V, class H, class E>
auto striped_hash_map<K, V, H, E>::contains(const K& key) const -> bool
{
    return find(key).has_value();
}

template <class K, class V, class H, class E>
auto striped_hash_map<K, V, H, E>::erase(const K& key) -> bool
{
    auto& target = stripe_for(key);
    exclusive_latch_guard lock(target.latch);
    for (std::size_t step{}; step < target.capacity; ++step)
    {
        auto& current = target.entries[(entry_index(key, target.capacity) + step) & (target.capacity - 1U)];
        if (current.state == slot_state::empty)
            return false;
        if (current.state == slot_state::occupied && equal_(*current.key, key))
        {
            current.key.reset();
            current.value.reset();
            current.state = slot_state::tombstone;
            --target.size;
            ++target.tombstones;
            size_.fetch_sub(1U, std::memory_order_relaxed);
            return true;
        }
    }
    return false;
}

template <class K, class V, class H, class E>
template <class Predicate>
requires std::predicate<Predicate&, const K&, const V&>
auto striped_hash_map<K, V, H, E>::erase_if(Predicate&& predicate) -> std::size_t
{
    std::size_t erased{};
    for (std::size_t stripe_index{}; stripe_index < stripe_count_; ++stripe_index)
    {
        auto& target = stripes_[stripe_index];
        exclusive_latch_guard lock(target.latch);
        for (std::size_t index{}; index < target.capacity; ++index)
        {
            auto& current = target.entries[index];
            if (current.state != slot_state::occupied ||
                !std::invoke(predicate, *current.key, *current.value))
                continue;
            current.key.reset();
            current.value.reset();
            current.state = slot_state::tombstone;
            --target.size;
            ++target.tombstones;
            ++erased;
        }
    }
    if (erased != 0U)
        size_.fetch_sub(erased, std::memory_order_relaxed);
    return erased;
}

template <class K, class V, class H, class E>
auto striped_hash_map<K, V, H, E>::size() const noexcept -> std::size_t
{
    return size_.load(std::memory_order_acquire);
}

template <class K, class V, class H, class E>
auto striped_hash_map<K, V, H, E>::stripe_count() const noexcept -> std::size_t
{
    return stripe_count_;
}

} // namespace cnetmod::concurrent_containers
