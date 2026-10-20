module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.quic:write_queue;

import std;
import :foundation;
import cnetmod.utils.concurrent_containers.queue;

namespace cnetmod::quic {

export struct quic_write_completion
{
    std::vector<std::byte> rejected_bytes;
    std::shared_ptr<const std::string> rejected_shared_bytes;

    struct receiver
    {
        quic_write_completion& owner;
        std::optional<std::expected<void, std::error_code>> value;

        [[nodiscard]] auto await_ready() noexcept -> bool;
        auto await_suspend(std::coroutine_handle<> handle) noexcept
            -> std::coroutine_handle<>;
        auto await_resume() noexcept -> std::expected<void, std::error_code>;
    };

    [[nodiscard]] auto receive() noexcept -> receiver;
    auto complete(std::expected<void, std::error_code> value) noexcept -> void;

private:
    class spin_lock
    {
    public:
        auto lock() noexcept -> void;
        auto unlock() noexcept -> void;

    private:
        std::atomic_flag flag_{};
    };

    spin_lock lock_;
    std::optional<std::expected<void, std::error_code>> value_;
    std::coroutine_handle<> waiter_{};
};

export struct quic_write_command
{
    enum class operation : std::uint8_t
    {
        write,
        datagram,
        set_priority
    };

    operation type{operation::write};
    stream_id stream{};
    std::vector<std::byte> bytes;
    bool fin{};
    std::uint8_t urgency{};
    bool incremental{};
    quic_write_completion* completion{};
    std::shared_ptr<const std::string> shared_bytes;
};

/// Bounded Command queue between application coroutines and the connection's
/// single packet owner. Completion objects come from a fixed Object Pool, so
/// producers do not allocate synchronization state on the response hot path.
export class quic_write_queue
{
public:
    static constexpr std::size_t capacity{512U};

    quic_write_queue();
    [[nodiscard]] auto try_acquire() noexcept -> quic_write_completion*;
    auto release(quic_write_completion& completion) noexcept -> void;
    [[nodiscard]] auto try_enqueue(quic_write_command command) noexcept -> bool;
    [[nodiscard]] auto try_dequeue() noexcept -> std::optional<quic_write_command>;
    [[nodiscard]] auto approximate_size() const noexcept -> std::size_t;
    auto fail_all(std::error_code error) noexcept -> void;

private:
    std::unique_ptr<quic_write_completion[]> slots_;
    concurrent_containers::bounded_mpmc_queue<quic_write_completion*> available_;
    concurrent_containers::bounded_mpmc_queue<quic_write_command> commands_;
};

} // namespace cnetmod::quic
