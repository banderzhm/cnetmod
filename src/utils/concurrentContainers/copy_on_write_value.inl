namespace cnetmod::concurrent_containers {

template <class T>
copy_on_write_value<T>::copy_on_write_value()
    : value_(std::make_shared<const T>())
{
}

template <class T>
copy_on_write_value<T>::copy_on_write_value(T initial)
    : value_(std::make_shared<const T>(std::move(initial)))
{
}

template <class T>
auto copy_on_write_value<T>::read() const noexcept -> std::shared_ptr<const T>
{
    return std::atomic_load_explicit(std::addressof(value_), std::memory_order_acquire);
}

template <class T>
auto copy_on_write_value<T>::snapshot() const -> T
{
    return *read();
}

template <class T>
void copy_on_write_value<T>::store(T value)
{
    std::atomic_store_explicit(std::addressof(value_),
        std::make_shared<const T>(std::move(value)), std::memory_order_release);
}

template <class T>
template <class Mutator>
requires std::invocable<Mutator&, T&>
auto copy_on_write_value<T>::update(Mutator&& mutator) -> void
{
    auto observed = std::atomic_load_explicit(std::addressof(value_),
        std::memory_order_acquire);
    for (;;)
    {
        auto replacement = std::make_shared<const T>([&]
            {
                T copy{*observed};
                std::invoke(mutator, copy);
                return copy;
            }());
        if (std::atomic_compare_exchange_weak_explicit(std::addressof(value_),
                std::addressof(observed), replacement, std::memory_order_release,
                std::memory_order_acquire))
            return;
    }
}

template <class T>
template <class Predicate>
requires std::predicate<Predicate&, const T&>
auto copy_on_write_value<T>::compare_update(Predicate&& predicate, T replacement) -> bool
{
    auto observed = std::atomic_load_explicit(std::addressof(value_),
        std::memory_order_acquire);
    for (;;)
    {
        if (!std::invoke(predicate, *observed))
            return false;
        auto next = std::make_shared<const T>(replacement);
        if (std::atomic_compare_exchange_weak_explicit(std::addressof(value_),
                std::addressof(observed), next, std::memory_order_release,
                std::memory_order_acquire))
            return true;
    }
}

} // namespace cnetmod::concurrent_containers
