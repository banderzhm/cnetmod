namespace cnetmod::concurrent_containers {
template <class K, class V, class C> concurrent_skip_list<K, V, C>::concurrent_skip_list() : head_(new node(max_level)) {}

template <class K, class V, class C> concurrent_skip_list<K, V, C>::~concurrent_skip_list()
{
    auto* current = head_;
    while (current)
    {
        auto* next = current->next[0];
        delete current;
        current = next;
    }
}

template <class K, class V, class C> auto concurrent_skip_list<K, V, C>::lower_bound_unlocked(const K& key, std::array<node*, max_level>* update) const -> node*
{
    auto* current = head_;
    for (std::size_t level = level_; level-- > 0U;)
    {
        while (current->next[level] && compare_(*current->next[level]->key, key))
            current = current->next[level];
        if (update)
            (*update)[level] = current;
    }
    return current->next[0];
}

template <class K, class V, class C> auto concurrent_skip_list<K, V, C>::random_level() noexcept -> std::size_t
{
    random_state_ ^= random_state_ << 7U;
    random_state_ ^= random_state_ >> 9U;
    random_state_ ^= random_state_ << 8U;
    std::size_t result = 1U;
    while (result < max_level && (random_state_ & 1U) == 0U)
    {
        ++result;
        random_state_ >>= 1U;
    }
    return result;
}

template <class K, class V, class C> auto concurrent_skip_list<K, V, C>::insert_or_assign(K key, V value) -> bool
{
    exclusive_latch_guard lock(latch_);
    std::array<node*, max_level> update{};
    auto* found = lower_bound_unlocked(key, &update);
    if (found && !compare_(key, *found->key) && !compare_(*found->key, key))
    {
        *found->value = std::move(value);
        return false;
    }
    const auto height = random_level();
    if (height > level_)
    {
        for (std::size_t i = level_; i < height; ++i)
            update[i] = head_;
        level_ = height;
    }
    auto* inserted = new node(std::move(key), std::move(value), height);
    for (std::size_t i{}; i < height; ++i)
    {
        inserted->next[i] = update[i]->next[i];
        update[i]->next[i] = inserted;
    }
    ++size_;
    return true;
}

template <class K, class V, class C> auto concurrent_skip_list<K, V, C>::find(const K& key) const -> std::optional<V>
{
    shared_latch_guard lock(latch_);
    auto* found = lower_bound_unlocked(key);
    if (found && !compare_(key, *found->key) && !compare_(*found->key, key))
        return *found->value;
    return std::nullopt;
}

template <class K, class V, class C> auto concurrent_skip_list<K, V, C>::erase(const K& key) -> bool
{
    exclusive_latch_guard lock(latch_);
    std::array<node*, max_level> update{};
    auto* found = lower_bound_unlocked(key, &update);
    if (!found || compare_(key, *found->key) || compare_(*found->key, key))
        return false;
    for (std::size_t i{}; i < level_; ++i)
        if (update[i]->next[i] == found)
            update[i]->next[i] = found->next[i];
    delete found;
    while (level_ > 1U && !head_->next[level_ - 1U])
        --level_;
    --size_;
    return true;
}

template <class K, class V, class C> auto concurrent_skip_list<K, V, C>::contains(const K& key) const -> bool
{
    return find(key).has_value();
}

template <class K, class V, class C> auto concurrent_skip_list<K, V, C>::snapshot() const -> std::vector<std::pair<K, V>>
{
    shared_latch_guard lock(latch_);
    std::vector<std::pair<K, V>> result;
    result.reserve(size_);
    for (auto* item = head_->next[0]; item; item = item->next[0])
        result.emplace_back(*item->key, *item->value);
    return result;
}

template <class K, class V, class C> auto concurrent_skip_list<K, V, C>::size() const noexcept -> std::size_t
{
    shared_latch_guard lock(latch_);
    return size_;
}
} // namespace cnetmod::concurrent_containers
