module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IO_URING
#include <liburing.h>
#include <cerrno>
#include <sys/socket.h>
#endif

module cnetmod.io.platform.io_uring_multishot_recv;

#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT) && \
    defined(IORING_CQE_F_BUFFER) && defined(IORING_CQE_F_MORE)

import std;
import cnetmod.coro.task;

namespace cnetmod {

namespace {

struct recv_suspend {
    uring_overlapped& operation;
    auto await_ready() const noexcept -> bool { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept {
        operation.coroutine = handle;
    }
    void await_resume() const noexcept {}
};

auto as_error(int result) -> std::error_code {
    return std::error_code(-result, std::generic_category());
}

} // namespace

io_uring_received_buffer::io_uring_received_buffer(
    io_uring_multishot_recv* owner, std::uint16_t buffer_id,
    std::size_t size) noexcept
    : owner_(owner), buffer_id_(buffer_id), size_(size) {}

io_uring_received_buffer::~io_uring_received_buffer() { release(); }

io_uring_received_buffer::io_uring_received_buffer(
    io_uring_received_buffer&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)),
      buffer_id_(other.buffer_id_), size_(other.size_) {}

auto io_uring_received_buffer::operator=(io_uring_received_buffer&& other) noexcept
    -> io_uring_received_buffer&
{
    if (this != &other) {
        release();
        owner_ = std::exchange(other.owner_, nullptr);
        buffer_id_ = other.buffer_id_;
        size_ = other.size_;
    }
    return *this;
}

auto io_uring_received_buffer::data() const noexcept -> const_buffer {
    if (!owner_) return {};
    auto view = owner_->buffers_->buffer(buffer_id_);
    return {view.data, size_};
}

auto io_uring_received_buffer::size() const noexcept -> std::size_t { return size_; }

void io_uring_received_buffer::release() noexcept {
    if (owner_) {
        owner_->release_buffer(buffer_id_);
        owner_ = nullptr;
    }
}

io_uring_multishot_recv::io_uring_multishot_recv(
    io_uring_context& context, socket& socket, io_uring_buffer_ring& buffers)
    : context_(&context), socket_fd_(static_cast<int>(socket.native_handle())),
      buffers_(&buffers)
{
    operation_.completion = &on_completion;
    operation_.completion_context = this;
}

io_uring_multishot_recv::~io_uring_multishot_recv() {
    for (const auto& ready : ready_)
        release_buffer(ready.buffer_id);
}

void io_uring_multishot_recv::on_completion(
    uring_overlapped& operation, int32_t result, std::uint32_t flags)
{
    static_cast<io_uring_multishot_recv*>(operation.completion_context)
        ->handle_completion(result, flags);
}

void io_uring_multishot_recv::handle_completion(int32_t result, std::uint32_t flags) {
    active_ = (flags & IORING_CQE_F_MORE) != 0;

    if (result > 0 && (flags & IORING_CQE_F_BUFFER) != 0) {
        const auto buffer_id = static_cast<std::uint16_t>(flags >> IORING_CQE_BUFFER_SHIFT);
        if (next_waiter_ && next_waiter_->coroutine) {
            auto* waiter = std::exchange(next_waiter_, nullptr);
            waiter->buffer_id = buffer_id;
            waiter->result = result;
            waiter->coroutine.resume();
        } else {
            ready_.push_back({buffer_id, static_cast<std::size_t>(result)});
        }
    } else if (result <= 0) {
        terminal_result_ = result;
        if (next_waiter_ && next_waiter_->coroutine) {
            auto* waiter = std::exchange(next_waiter_, nullptr);
            waiter->result = result;
            waiter->coroutine.resume();
        }
    } else {
        terminal_result_ = -ENOBUFS;
    }

    if (!active_ && stop_waiter_ && stop_waiter_->coroutine) {
        auto* waiter = std::exchange(stop_waiter_, nullptr);
        waiter->result = 0;
        waiter->coroutine.resume();
    }
}

auto io_uring_multishot_recv::start() -> std::expected<void, std::error_code> {
    auto* sqe = context_->prepare_sqe();
    if (!sqe)
        return std::unexpected(std::make_error_code(std::errc::no_buffer_space));

    ::io_uring_prep_recv_multishot(sqe, socket_fd_, nullptr, 0, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = buffers_->group_id();
    ::io_uring_sqe_set_data(sqe, &operation_);
    active_ = true;
    terminal_result_ = 0;

    if (auto result = context_->flush(); !result) {
        active_ = false;
        return std::unexpected(result.error());
    }
    return {};
}

auto io_uring_multishot_recv::async_next()
    -> task<std::expected<io_uring_received_buffer, std::error_code>>
{
    if (!ready_.empty()) {
        const auto ready = ready_.front();
        ready_.pop_front();
        co_return io_uring_received_buffer{this, ready.buffer_id, ready.size};
    }
    if (terminal_result_ < 0)
        co_return std::unexpected(as_error(terminal_result_));
    if (!active_) {
        if (auto result = start(); !result)
            co_return std::unexpected(result.error());
    }
    if (next_waiter_)
        co_return std::unexpected(std::make_error_code(std::errc::operation_in_progress));

    uring_overlapped waiter;
    next_waiter_ = &waiter;
    co_await recv_suspend{waiter};

    if (waiter.result > 0)
        co_return io_uring_received_buffer{this, waiter.buffer_id,
                                            static_cast<std::size_t>(waiter.result)};
    if (waiter.result == 0)
        co_return io_uring_received_buffer{};
    co_return std::unexpected(as_error(waiter.result));
}

auto io_uring_multishot_recv::async_stop()
    -> task<std::expected<void, std::error_code>>
{
    if (!active_)
        co_return std::expected<void, std::error_code>{};

    stopping_ = true;
    uring_overlapped cancel;
    auto* sqe = context_->prepare_sqe();
    if (!sqe)
        co_return std::unexpected(std::make_error_code(std::errc::no_buffer_space));
    ::io_uring_prep_cancel(sqe, &operation_, 0);
    ::io_uring_sqe_set_data(sqe, &cancel);
    if (auto result = context_->flush(); !result)
        co_return std::unexpected(result.error());
    co_await recv_suspend{cancel};

    if (active_) {
        uring_overlapped terminal;
        stop_waiter_ = &terminal;
        co_await recv_suspend{terminal};
    }
    co_return std::expected<void, std::error_code>{};
}

void io_uring_multishot_recv::release_buffer(std::uint16_t buffer_id) noexcept {
    buffers_->release(buffer_id);
}

} // namespace cnetmod

#endif
