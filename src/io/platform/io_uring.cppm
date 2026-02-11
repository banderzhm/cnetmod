module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IO_URING

#include <liburing.h>
#include <unistd.h>
#include <fcntl.h>

export module cnetmod.io.platform.io_uring;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.io.io_operation;

namespace cnetmod {

// =============================================================================
// io_uring 协程操作基类
// =============================================================================

/// io_uring 完成信息（类似 IOCP 的 iocp_overlapped）
/// 每个异步操作创建一个，设为 SQE user_data，CQE 完成后恢复协程
export struct uring_overlapped {
    std::coroutine_handle<> coroutine{};  // 完成后恢复的协程
    int32_t result = 0;                   // CQE 结果（>=0 成功字节数，<0 错误码取反）
};

// =============================================================================
// io_uring Context 实现
// =============================================================================

/// Linux io_uring 实现
export class io_uring_context : public io_context {
public:
    explicit io_uring_context(unsigned queue_depth = 256) {
        int ret = ::io_uring_queue_init(queue_depth, &ring_, 0);
        if (ret < 0)
            throw std::system_error(
                -ret, std::generic_category(),
                "io_uring_queue_init failed");
        initialized_ = true;

        // 创建非阻塞 pipe 用于 post() 唤醒
        if (::pipe2(pipe_fds_, O_NONBLOCK | O_CLOEXEC) < 0) {
            ::io_uring_queue_exit(&ring_);
            throw std::system_error(
                errno, std::generic_category(), "pipe2 failed");
        }
        // 提交对 pipe 读端的 read，用 wake_ov_ 作为哨兵
        submit_wake_read();
    }

    ~io_uring_context() override {
        if (pipe_fds_[0] >= 0) ::close(pipe_fds_[0]);
        if (pipe_fds_[1] >= 0) ::close(pipe_fds_[1]);
        if (initialized_)
            ::io_uring_queue_exit(&ring_);
    }

    void run() override {
        while (!stopped_.load(std::memory_order_relaxed)) {
            run_one_impl(true);
        }
    }

    auto run_one() -> std::size_t override {
        return run_one_impl(true);
    }

    auto poll() -> std::size_t override {
        return run_one_impl(false);
    }

    void stop() override {
        stopped_.store(true, std::memory_order_relaxed);
        // 提交一个 NOP 唤醒等待中的 io_uring_wait_cqe
        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (sqe) {
            ::io_uring_prep_nop(sqe);
            ::io_uring_sqe_set_data(sqe, nullptr);  // nullptr = stop 信号
            ::io_uring_submit(&ring_);
        }
    }

    [[nodiscard]] auto stopped() const noexcept -> bool override {
        return stopped_.load(std::memory_order_relaxed);
    }

    void restart() override {
        stopped_.store(false, std::memory_order_relaxed);
    }

    /// 获取 io_uring 提交队列条目
    [[nodiscard]] auto get_sqe() -> ::io_uring_sqe* {
        return ::io_uring_get_sqe(&ring_);
    }

    /// 提交 I/O 请求
    [[nodiscard]] auto submit() -> std::expected<int, std::error_code> {
        int ret = ::io_uring_submit(&ring_);
        if (ret < 0)
            return std::unexpected(
                std::error_code(-ret, std::generic_category()));
        return ret;
    }

protected:
    void wake() override {
        char c = 1;
        ::write(pipe_fds_[1], &c, 1);
    }

private:
    auto run_one_impl(bool blocking) -> std::size_t {
        ::io_uring_cqe* cqe = nullptr;

        int ret;
        if (blocking) {
            ret = ::io_uring_wait_cqe(&ring_, &cqe);
        } else {
            ret = ::io_uring_peek_cqe(&ring_, &cqe);
        }

        if (ret < 0 || cqe == nullptr)
            return 0;

        auto* ov = static_cast<uring_overlapped*>(
            ::io_uring_cqe_get_data(cqe));
        auto result = cqe->res;
        ::io_uring_cqe_seen(&ring_, cqe);

        if (ov == nullptr)
            return 0;  // NOP (stop 唤醒信号)

        // pipe 唤醒哨兵 — drain post 队列并重新注册 pipe 读
        if (ov == &wake_ov_) {
            // io_uring 已消费 1 字节，排空 pipe 中可能的剩余字节（多次 wake）
            char buf[64];
            while (::read(pipe_fds_[0], buf, sizeof(buf)) > 0) {}
            submit_wake_read();
            return drain_post_queue();
        }

        ov->result = result;

        // 恢复等待此操作的协程
        if (ov->coroutine)
            ov->coroutine.resume();

        return 1;
    }

    void submit_wake_read() {
        auto* sqe = ::io_uring_get_sqe(&ring_);
        if (sqe) {
            ::io_uring_prep_read(sqe, pipe_fds_[0], &pipe_buf_, 1, 0);
            ::io_uring_sqe_set_data(sqe, &wake_ov_);
            ::io_uring_submit(&ring_);
        }
    }

    ::io_uring ring_{};
    std::atomic<bool> stopped_{false};
    bool initialized_ = false;
    int pipe_fds_[2] = {-1, -1};
    char pipe_buf_ = 0;
    uring_overlapped wake_ov_{};  // 哨兵，不存协程
};

} // namespace cnetmod

#endif // CNETMOD_HAS_IO_URING
