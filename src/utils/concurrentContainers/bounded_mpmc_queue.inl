namespace cnetmod::concurrent_containers {

template <class T>
bounded_mpmc_queue<T>::bounded_mpmc_queue(std::size_t capacity)
{
    if (capacity < 2U)
        capacity = 2U;
    capacity_ = std::bit_ceil(capacity);
    mask_ = capacity_ - 1U;
    cells_ = std::make_unique<cell[]>(capacity_);
    for (std::size_t index{}; index < capacity_; ++index)
        cells_[index].sequence.store(index, std::memory_order_relaxed);
}

template <class T>
bounded_mpmc_queue<T>::~bounded_mpmc_queue()
{
    while (try_dequeue())
    {}
}

template <class T>
template <class... Args>
auto bounded_mpmc_queue<T>::try_emplace(Args&&... args) -> bool
{
    auto position = enqueue_pos_.value.load(std::memory_order_relaxed);
    for (;;)
    {
        auto& target = cells_[position & mask_];
        const auto sequence = target.sequence.load(std::memory_order_acquire);
        const auto difference = static_cast<std::intptr_t>(sequence) - static_cast<std::intptr_t>(position);
        if (difference == 0)
        {
            if (enqueue_pos_.value.compare_exchange_weak(position, position + 1U,
                    std::memory_order_relaxed, std::memory_order_relaxed))
            {
                std::construct_at(target.value(), std::forward<Args>(args)...);
                target.sequence.store(position + 1U, std::memory_order_release);
                return true;
            }
        }
        else if (difference < 0)
        {
            return false;
        }
        else
        {
            position = enqueue_pos_.value.load(std::memory_order_relaxed);
        }
    }
}

template <class T>
auto bounded_mpmc_queue<T>::try_enqueue(T value) -> bool
{
    return try_emplace(std::move(value));
}

template <class T>
auto bounded_mpmc_queue<T>::try_dequeue() -> std::optional<T>
{
    auto position = dequeue_pos_.value.load(std::memory_order_relaxed);
    for (;;)
    {
        auto& target = cells_[position & mask_];
        const auto sequence = target.sequence.load(std::memory_order_acquire);
        const auto difference = static_cast<std::intptr_t>(sequence) - static_cast<std::intptr_t>(position + 1U);
        if (difference == 0)
        {
            if (dequeue_pos_.value.compare_exchange_weak(position, position + 1U,
                    std::memory_order_relaxed, std::memory_order_relaxed))
            {
                std::optional<T> result{std::in_place, std::move(*target.value())};
                std::destroy_at(target.value());
                target.sequence.store(position + capacity_, std::memory_order_release);
                return result;
            }
        }
        else if (difference < 0)
        {
            return std::nullopt;
        }
        else
        {
            position = dequeue_pos_.value.load(std::memory_order_relaxed);
        }
    }
}

template <class T>
auto bounded_mpmc_queue<T>::capacity() const noexcept -> std::size_t
{
    return capacity_;
}

template <class T>
auto bounded_mpmc_queue<T>::approximate_size() const noexcept -> std::size_t
{
    const auto put = enqueue_pos_.value.load(std::memory_order_relaxed);
    const auto take = dequeue_pos_.value.load(std::memory_order_relaxed);
    return std::min(capacity_, put >= take ? put - take : 0U);
}

} // namespace cnetmod::concurrent_containers
