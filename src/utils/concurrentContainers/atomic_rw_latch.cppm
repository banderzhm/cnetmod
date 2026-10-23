/// Project-owned reader/writer latch for synchronous concurrent containers.
///
/// The latch uses an atomic state word only: the high bits publish writer
/// ownership/pending intent and the remaining bits count readers. `wait` is
/// used after optimistic CAS retries, so a contended operation does not burn a
/// worker core indefinitely and never depends on platform locking primitives.
export module cnetmod.utils.concurrent_containers.atomic_rw_latch;

import std;

namespace cnetmod::concurrent_containers {

export class atomic_rw_latch
{
public:
    atomic_rw_latch() noexcept = default;
    atomic_rw_latch(const atomic_rw_latch&) = delete;
    auto operator=(const atomic_rw_latch&) -> atomic_rw_latch& = delete;

    void lock() noexcept
    {
        // Uncontended acquisition is one CAS; the two-step pending protocol
        // below is only needed to keep readers from starving a writer.
        std::uint32_t idle = 0U;
        if (state_.compare_exchange_strong(idle, writer_bit,
                std::memory_order_acquire, std::memory_order_relaxed))
            return;
        for (;;)
        {
            auto observed = state_.load(std::memory_order_acquire);
            if ((observed & writer_pending_bit) == 0U)
            {
                const auto desired = observed | writer_pending_bit;
                if (!state_.compare_exchange_weak(observed, desired,
                        std::memory_order_acq_rel, std::memory_order_acquire))
                    continue;
                observed = desired;
            }
            if (observed == writer_pending_bit)
            {
                auto expected = writer_pending_bit;
                if (state_.compare_exchange_strong(expected, writer_bit,
                        std::memory_order_acquire, std::memory_order_relaxed))
                    return;
                continue;
            }
            wait_for_change(observed);
        }
    }

    void unlock() noexcept
    {
        (void)state_.exchange(0U, std::memory_order_seq_cst);
        notify_waiters();
    }

    void lock_shared() const noexcept
    {
        for (;;)
        {
            auto observed = state_.load(std::memory_order_acquire);
            if ((observed & writer_mask) == 0U)
            {
                if (state_.compare_exchange_weak(observed, observed + 1U,
                        std::memory_order_acquire, std::memory_order_relaxed))
                    return;
                continue;
            }
            wait_for_change(observed);
        }
    }

    void unlock_shared() const noexcept
    {
        const auto previous = state_.fetch_sub(1U, std::memory_order_seq_cst);
        if ((previous & reader_mask) == 1U)
            notify_waiters();
    }

private:
    static constexpr std::uint32_t writer_bit = 0x80000000U;
    static constexpr std::uint32_t writer_pending_bit = 0x40000000U;
    static constexpr std::uint32_t writer_mask = writer_bit | writer_pending_bit;
    static constexpr std::uint32_t reader_mask = ~writer_mask;

    // A contender counts itself before it sleeps and every release reads the
    // count after changing the state (all sequentially consistent), so a
    // release either sees the sleeper or the sleeper sees the new state.
    // Uncontended releases therefore never enter the platform wait table.
    void wait_for_change(std::uint32_t observed) const noexcept
    {
        waiters_.fetch_add(1U, std::memory_order_seq_cst);
        if (state_.load(std::memory_order_seq_cst) == observed)
            state_.wait(observed, std::memory_order_relaxed);
        waiters_.fetch_sub(1U, std::memory_order_relaxed);
    }

    void notify_waiters() const noexcept
    {
        if (waiters_.load(std::memory_order_seq_cst) != 0U)
            state_.notify_all();
    }

    alignas(64) mutable std::atomic<std::uint32_t> state_{};
    mutable std::atomic<std::uint32_t> waiters_{};
};

export class exclusive_latch_guard
{
public:
    explicit exclusive_latch_guard(atomic_rw_latch& latch) noexcept : latch_(&latch)
    {
        latch_->lock();
    }

    ~exclusive_latch_guard()
    {
        unlock();
    }

    exclusive_latch_guard(const exclusive_latch_guard&) = delete;
    auto operator=(const exclusive_latch_guard&) -> exclusive_latch_guard& = delete;

    void unlock() noexcept
    {
        if (latch_)
            std::exchange(latch_, nullptr)->unlock();
    }

private:
    atomic_rw_latch* latch_{};
};

export class shared_latch_guard
{
public:
    explicit shared_latch_guard(const atomic_rw_latch& latch) noexcept : latch_(&latch)
    {
        latch_->lock_shared();
    }

    ~shared_latch_guard()
    {
        if (latch_)
            latch_->unlock_shared();
    }

    shared_latch_guard(const shared_latch_guard&) = delete;
    auto operator=(const shared_latch_guard&) -> shared_latch_guard& = delete;

private:
    const atomic_rw_latch* latch_{};
};

} // namespace cnetmod::concurrent_containers
