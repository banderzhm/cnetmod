export module cnetmod.utils.concurrent_containers.accumulators;

import std;

namespace cnetmod::concurrent_containers {

/// Padding prevents independent writers from false-sharing a cache line.
export inline constexpr std::size_t concurrent_cache_line_size = 64U;

/// A striped numeric accumulator. Updates are relaxed because each stripe is
/// independent; value() establishes an acquire snapshot of all stripes.
export template <class T>
requires std::is_arithmetic_v<T>
class striped_accumulator
{
public:
    explicit striped_accumulator(std::size_t stripes = 0);
    striped_accumulator(const striped_accumulator&) = delete;
    auto operator=(const striped_accumulator&) -> striped_accumulator& = delete;

    void add(T value) noexcept;
    [[nodiscard]] auto value() const noexcept -> T;
    void reset(T value = T{}) noexcept;
    [[nodiscard]] auto stripe_count() const noexcept -> std::size_t;

private:
    struct alignas(concurrent_cache_line_size) stripe
    {
        std::atomic<T> value{};
    };

    [[nodiscard]] auto index_for_current_thread() const noexcept -> std::size_t;
    std::unique_ptr<stripe[]> stripes_;
    std::size_t count_{};
};

export using concurrent_counter = striped_accumulator<std::uint64_t>;

} // namespace cnetmod::concurrent_containers

#include "striped_accumulator.inl"
