module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.tcp;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.io.io_context;

namespace cnetmod::tcp {

// =============================================================================
// TCP Acceptor
// =============================================================================

/// TCP 连接接受器
/// 用于监听端口并接受传入连接
export class acceptor {
public:
    explicit acceptor(io_context& ctx)
        : ctx_(&ctx) {}

    ~acceptor() = default;

    // 不可复制
    acceptor(const acceptor&) = delete;
    auto operator=(const acceptor&) -> acceptor& = delete;

    // 可移动
    acceptor(acceptor&&) noexcept = default;
    auto operator=(acceptor&&) noexcept -> acceptor& = default;

    /// 打开并绑定到指定端点
    [[nodiscard]] auto open(const endpoint& ep, const socket_options& opts = {})
        -> std::expected<void, std::error_code>;

    /// 关闭 acceptor
    void close() noexcept {
        socket_.close();
    }

    /// 是否打开
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return socket_.is_open();
    }

    /// 获取底层 socket
    [[nodiscard]] auto native_socket() noexcept -> socket& {
        return socket_;
    }

    /// 获取关联的 io_context
    [[nodiscard]] auto context() noexcept -> io_context& {
        return *ctx_;
    }

private:
    io_context* ctx_;
    socket socket_;
};

// =============================================================================
// TCP Connection
// =============================================================================

/// TCP 连接
/// 表示一个已建立的 TCP 连接
export class connection {
public:
    explicit connection(io_context& ctx)
        : ctx_(&ctx) {}

    connection(io_context& ctx, socket sock)
        : ctx_(&ctx), socket_(std::move(sock)) {}

    ~connection() = default;

    // 不可复制
    connection(const connection&) = delete;
    auto operator=(const connection&) -> connection& = delete;

    // 可移动
    connection(connection&&) noexcept = default;
    auto operator=(connection&&) noexcept -> connection& = default;

    /// 获取远程端点
    [[nodiscard]] auto remote_endpoint() const -> std::expected<endpoint, std::error_code>;

    /// 获取本地端点
    [[nodiscard]] auto local_endpoint() const -> std::expected<endpoint, std::error_code>;

    /// 关闭连接
    void close() noexcept {
        socket_.close();
    }

    /// 是否打开
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return socket_.is_open();
    }

    /// 获取底层 socket
    [[nodiscard]] auto native_socket() noexcept -> socket& {
        return socket_;
    }

    /// 获取关联的 io_context
    [[nodiscard]] auto context() noexcept -> io_context& {
        return *ctx_;
    }

private:
    io_context* ctx_;
    socket socket_;
};

} // namespace cnetmod::tcp
