namespace cnetmod::concurrent_containers {
template <class K, class V, class H, class E>
atomic_hash_map<K, V, H, E>::atomic_hash_map(std::size_t capacity)
{
    capacity_ = std::bit_ceil(std::max<std::size_t>(capacity, 1U));
    mask_ = capacity_ - 1U;
    slots_ = std::make_unique<slot[]>(capacity_);
}

template <class K, class V, class H, class E>
auto atomic_hash_map<K, V, H, E>::slot_index(const K& key) const -> std::size_t
{
    return hash_(key) & mask_;
}

template <class K, class V, class H, class E>
auto atomic_hash_map<K, V, H, E>::load_slot(const slot& source) noexcept
    -> std::shared_ptr<const entry>
{
    return std::atomic_load_explicit(std::addressof(source.value),
        std::memory_order_acquire);
}

template <class K, class V, class H, class E>
auto atomic_hash_map<K, V, H, E>::compare_exchange_slot(slot& target,
    std::shared_ptr<const entry>& expected,
    const std::shared_ptr<const entry>& replacement) noexcept -> bool
{
    return std::atomic_compare_exchange_weak_explicit(std::addressof(target.value),
        std::addressof(expected), replacement, std::memory_order_release,
        std::memory_order_acquire);
}

template <class K, class V, class H, class E>
auto atomic_hash_map<K, V, H, E>::try_emplace(K key, V value) -> bool
{
    return try_emplace_bounded(std::move(key), std::move(value), capacity_);
}

template <class K, class V, class H, class E>
auto atomic_hash_map<K, V, H, E>::try_emplace_bounded(K key, V value,
    std::size_t maximum_size) -> bool
{
    if (maximum_size == 0U)
        return false;

    const auto previous_size = size_.fetch_add(1U, std::memory_order_acq_rel);
    if (previous_size >= maximum_size)
    {
        size_.fetch_sub(1U, std::memory_order_release);
        return false;
    }

    const auto candidate = std::make_shared<const entry>(entry{std::move(key), std::move(value), false});
    for (;;)
    {
        std::optional<std::size_t> first_tombstone;
        bool retry{};
        for (std::size_t step{}; step < capacity_; ++step)
        {
            const auto index = (slot_index(candidate->key) + step) & mask_;
            auto observed = load_slot(slots_[index]);
            if (!observed)
            {
                const auto target = first_tombstone.value_or(index);
                auto expected = load_slot(slots_[target]);
                if (expected && !expected->erased)
                {
                    retry = true;
                    break;
                }
                if (compare_exchange_slot(slots_[target], expected, candidate))
                    return true;
                retry = true;
                break;
            }
            if (equal_(observed->key, candidate->key) && !observed->erased)
            {
                size_.fetch_sub(1U, std::memory_order_release);
                return false;
            }
            if (observed->erased && !first_tombstone)
                first_tombstone = index;
        }

        if (retry)
            continue;
        if (first_tombstone)
        {
            auto expected = load_slot(slots_[*first_tombstone]);
            if (expected && expected->erased &&
                compare_exchange_slot(slots_[*first_tombstone], expected, candidate))
                return true;
            continue;
        }
        size_.fetch_sub(1U, std::memory_order_release);
        return false;
    }
}

template <class K, class V, class H, class E>
auto atomic_hash_map<K, V, H, E>::try_insert_or_assign(K key, V value) -> bool
{
    const auto candidate = std::make_shared<const entry>(entry{std::move(key), std::move(value), false});
    for (;;)
    {
        std::optional<std::size_t> first_tombstone;
        std::optional<std::size_t> target;
        for (std::size_t step{}; step < capacity_; ++step)
        {
            const auto index = (slot_index(candidate->key) + step) & mask_;
            const auto observed = load_slot(slots_[index]);
            if (!observed)
            {
                target = first_tombstone.value_or(index);
                break;
            }
            if (equal_(observed->key, candidate->key))
            {
                target = index;
                break;
            }
            if (observed->erased && !first_tombstone)
                first_tombstone = index;
        }

        if (!target)
            target = first_tombstone;
        if (!target)
            return false;

        auto observed = load_slot(slots_[*target]);
        if (observed && !observed->erased && !equal_(observed->key, candidate->key))
            continue;
        const bool grows = !observed || observed->erased;
        if (compare_exchange_slot(slots_[*target], observed, candidate))
        {
            if (grows)
                size_.fetch_add(1U, std::memory_order_relaxed);
            return true;
        }
    }
}

template <class K, class V, class H, class E>
void atomic_hash_map<K, V, H, E>::insert_or_assign(K key, V value)
{
    if (try_insert_or_assign(std::move(key), std::move(value)))
        return;
    throw std::overflow_error("cnetmod::atomic_hash_map is full");
}

template <class K, class V, class H, class E>
auto atomic_hash_map<K, V, H, E>::find(const K& key) const -> std::optional<V>
{
    for (std::size_t step{}; step < capacity_; ++step)
    {
        const auto item = load_slot(slots_[(slot_index(key) + step) & mask_]);
        if (!item)
            return std::nullopt;
        if (equal_(item->key, key) && !item->erased)
            return item->value;
    }
    return std::nullopt;
}

template <class K, class V, class H, class E>
auto atomic_hash_map<K, V, H, E>::contains(const K& key) const -> bool
{
    return find(key).has_value();
}

template <class K, class V, class H, class E>
auto atomic_hash_map<K, V, H, E>::erase(const K& key) -> bool
{
    for (std::size_t step{}; step < capacity_; ++step)
    {
        auto& slot = slots_[(slot_index(key) + step) & mask_];
        auto item = load_slot(slot);
        if (!item)
            return false;
        if (!equal_(item->key, key))
            continue;
        if (item->erased)
            return false;
        auto tomb = std::make_shared<const entry>(entry{item->key, item->value, true});
        if (compare_exchange_slot(slot, item, tomb))
        {
            size_.fetch_sub(1U, std::memory_order_relaxed);
            return true;
        }
        --step;
    }
    return false;
}

template <class K, class V, class H, class E>
template <class Predicate>
requires std::predicate<Predicate&, const K&, const V&>
auto atomic_hash_map<K, V, H, E>::erase_if(Predicate&& predicate) -> std::size_t
{
    std::size_t removed{};
    for (std::size_t index{}; index < capacity_; ++index)
    {
        auto& slot = slots_[index];
        auto observed = load_slot(slot);
        while (observed && !observed->erased &&
            std::invoke(predicate, observed->key, observed->value))
        {
            auto tombstone = std::make_shared<const entry>(
                entry{observed->key, observed->value, true});
            if (compare_exchange_slot(slot, observed, tombstone))
            {
                size_.fetch_sub(1U, std::memory_order_relaxed);
                ++removed;
                break;
            }
        }
    }
    return removed;
}

template <class K, class V, class H, class E>
template <class Compare>
requires std::predicate<Compare&, const K&, const V&, const K&, const V&>
auto atomic_hash_map<K, V, H, E>::erase_min_by(Compare&& less) -> bool
{
    for (;;)
    {
        std::size_t candidate_index{capacity_};
        std::shared_ptr<const entry> candidate;
        for (std::size_t index{}; index < capacity_; ++index)
        {
            auto observed = load_slot(slots_[index]);
            if (!observed || observed->erased)
                continue;
            if (!candidate || std::invoke(less, observed->key, observed->value, candidate->key, candidate->value))
            {
                candidate_index = index;
                candidate = std::move(observed);
            }
        }
        if (!candidate)
            return false;

        auto tombstone = std::make_shared<const entry>(
            entry{candidate->key, candidate->value, true});
        if (compare_exchange_slot(slots_[candidate_index], candidate, tombstone))
        {
            size_.fetch_sub(1U, std::memory_order_relaxed);
            return true;
        }
    }
}

template <class K, class V, class H, class E>
auto atomic_hash_map<K, V, H, E>::size() const noexcept -> std::size_t
{
    return size_.load(std::memory_order_acquire);
}

template <class K, class V, class H, class E>
auto atomic_hash_map<K, V, H, E>::capacity() const noexcept -> std::size_t
{
    return capacity_;
}
} // namespace cnetmod::concurrent_containers
