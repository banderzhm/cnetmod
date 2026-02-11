module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.udp;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.io.io_context;

namespace cnetmod::udp {

// =============================================================================
// UDP Socket
// =============================================================================

/// UDP 套接字
/// 无连接的数据报通信
export class udp_socket {
public:
    explicit udp_socket(io_context& ctx)
        : ctx_(&ctx) {}

    ~udp_socket() = default;

    // 不可复制
    udp_socket(const udp_socket&) = delete;
    auto operator=(const udp_socket&) -> udp_socket& = delete;

    // 可移动
    udp_socket(udp_socket&&) noexcept = default;
    auto operator=(udp_socket&&) noexcept -> udp_socket& = default;

    /// 打开并绑定到指定端点
    [[nodiscard]] auto open(const endpoint& ep, const socket_options& opts = {})
        -> std::expected<void, std::error_code>;

    /// 仅打开（不绑定，用于发送）
    [[nodiscard]] auto open(address_family family = address_family::ipv4)
        -> std::expected<void, std::error_code>;

    /// 关闭
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

} // namespace cnetmod::udp
