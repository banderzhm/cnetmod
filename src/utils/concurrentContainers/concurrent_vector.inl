namespace cnetmod::concurrent_containers {
template <class T> concurrent_vector<T>::~concurrent_vector()
{
    clear();
}

template <class T> template <class... Args>
auto concurrent_vector<T>::emplace_back(Args&&... args) -> std::size_t
{
    exclusive_latch_guard lock(latch_);
    if (size_ == capacity_)
        grow_unlocked(size_ + 1U);
    std::construct_at(slots_[size_].value(), std::forward<Args>(args)...);
    slots_[size_].occupied = true;
    return size_++;
}

template <class T> void concurrent_vector<T>::push_back(T value)
{
    (void)emplace_back(std::move(value));
}

template <class T> void concurrent_vector<T>::grow_unlocked(std::size_t minimum_capacity)
{
    const auto next_capacity = std::bit_ceil(std::max<std::size_t>(minimum_capacity, capacity_ == 0U ? 8U : capacity_ * 2U));
    auto next = std::make_unique<slot[]>(next_capacity);
    for (std::size_t i{}; i < size_; ++i)
    {
        std::construct_at(next[i].value(), std::move(*slots_[i].value()));
        next[i].occupied = true;
        std::destroy_at(slots_[i].value());
        slots_[i].occupied = false;
    }
    slots_ = std::move(next);
    capacity_ = next_capacity;
}

template <class T> auto concurrent_vector<T>::try_get(std::size_t index) const -> std::optional<T>
{
    shared_latch_guard lock(latch_);
    if (index >= size_)
        return std::nullopt;
    return *slots_[index].value();
}

template <class T> template <class Visitor>
requires std::invocable<Visitor&, const T&>
void concurrent_vector<T>::for_each_snapshot(Visitor&& visitor) const
{
    shared_latch_guard lock(latch_);
    for (std::size_t i{}; i < size_; ++i)
        std::invoke(visitor, *slots_[i].value());
}

template <class T> auto concurrent_vector<T>::size() const noexcept -> std::size_t
{
    shared_latch_guard lock(latch_);
    return size_;
}

template <class T> auto concurrent_vector<T>::empty() const noexcept -> bool
{
    return size() == 0U;
}

template <class T> void concurrent_vector<T>::clear()
{
    exclusive_latch_guard lock(latch_);
    for (std::size_t i{}; i < size_; ++i)
    {
        std::destroy_at(slots_[i].value());
        slots_[i].occupied = false;
    }
    size_ = 0U;
}
} // namespace cnetmod::concurrent_containers
