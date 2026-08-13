namespace cnetmod::concurrent_containers {

template <class T>
requires std::is_arithmetic_v<T>
striped_accumulator<T>::striped_accumulator(std::size_t stripes)
{
    if (stripes == 0U)
        stripes = std::max<std::size_t>(2U, std::thread::hardware_concurrency() * 2U);
    count_ = std::bit_ceil(stripes);
    stripes_ = std::make_unique<stripe[]>(count_);
}

template <class T>
requires std::is_arithmetic_v<T>
auto striped_accumulator<T>::index_for_current_thread() const noexcept -> std::size_t
{
    const auto value = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return value & (count_ - 1U);
}

template <class T>
requires std::is_arithmetic_v<T>
void striped_accumulator<T>::add(T value) noexcept
{
    stripes_[index_for_current_thread()].value.fetch_add(value, std::memory_order_relaxed);
}

template <class T>
requires std::is_arithmetic_v<T>
auto striped_accumulator<T>::value() const noexcept -> T
{
    T result{};
    for (std::size_t index{}; index < count_; ++index)
        result += stripes_[index].value.load(std::memory_order_acquire);
    return result;
}

template <class T>
requires std::is_arithmetic_v<T>
void striped_accumulator<T>::reset(T value) noexcept
{
    for (std::size_t index{}; index < count_; ++index)
        stripes_[index].value.store(value, std::memory_order_release);
}

template <class T>
requires std::is_arithmetic_v<T>
auto striped_accumulator<T>::stripe_count() const noexcept -> std::size_t
{
    return count_;
}

} // namespace cnetmod::concurrent_containers
