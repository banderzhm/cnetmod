/// cnetmod.core.dns — 异步 DNS 解析
/// 使用 stdexec::static_thread_pool 执行阻塞的 getaddrinfo
/// 完成后通过 io_context::post 恢复协程

module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif

#include <new>
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/async_scope.hpp>

export module cnetmod.core.dns;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;

namespace cnetmod {

// =============================================================================
// 全局线程池 (stdexec) — 用于阻塞操作卸载
// =============================================================================

namespace detail {

inline auto& blocking_pool() {
    static exec::static_thread_pool pool;
    return pool;
}

inline auto& blocking_scope() {
    static exec::async_scope scope;
    return scope;
}

} // namespace detail

// =============================================================================
// detail::resolve_awaitable — stdexec 线程池异步 DNS
// =============================================================================

namespace detail {

struct resolve_awaitable {
    io_context& ctx_;
    std::string host_;
    std::string service_;
    std::expected<std::vector<std::string>, std::string> result_;
    std::coroutine_handle<> caller_{};

    auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        caller_ = h;
        // 在 stdexec 线程池上调度阻塞的 getaddrinfo，完成后 post 回 io_context
        auto work = stdexec::then(
            stdexec::schedule(blocking_pool().get_scheduler()),
            [this]() noexcept { run(); }
        );
        blocking_scope().spawn(std::move(work));
    }

    auto await_resume() -> std::expected<std::vector<std::string>, std::string> {
        return std::move(result_);
    }

private:
    void do_resolve() noexcept {
        ::addrinfo hints{};
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        ::addrinfo* res = nullptr;
        const char* svc = service_.empty() ? nullptr : service_.c_str();
        int rc = ::getaddrinfo(host_.c_str(), svc, &hints, &res);

        if (rc != 0) {
#ifdef CNETMOD_PLATFORM_WINDOWS
            result_ = std::unexpected(
                std::format("getaddrinfo failed (error {})", rc));
#else
            result_ = std::unexpected(
                std::format("getaddrinfo: {}", ::gai_strerror(rc)));
#endif
            return;
        }
        if (!res) {
            result_ = std::unexpected(std::string("no results from getaddrinfo"));
            return;
        }

        std::vector<std::string> addrs;
        char buf[INET6_ADDRSTRLEN]{};
        for (auto* p = res; p; p = p->ai_next) {
            const void* addr_ptr = nullptr;
            if (p->ai_family == AF_INET)
                addr_ptr = &reinterpret_cast<::sockaddr_in*>(p->ai_addr)->sin_addr;
            else if (p->ai_family == AF_INET6)
                addr_ptr = &reinterpret_cast<::sockaddr_in6*>(p->ai_addr)->sin6_addr;

            if (addr_ptr && ::inet_ntop(p->ai_family, addr_ptr, buf, sizeof(buf)))
                addrs.emplace_back(buf);
        }
        ::freeaddrinfo(res);

        if (addrs.empty())
            result_ = std::unexpected(std::string("no addresses resolved"));
        else
            result_ = std::move(addrs);
    }

    void run() noexcept {
        do_resolve();
        ctx_.post(caller_);
    }
};

} // namespace detail

// =============================================================================
// async_resolve — 异步 DNS 解析
// =============================================================================

/// 异步解析主机名为 IP 地址列表
/// @param ctx      事件循环上下文（用于恢复协程）
/// @param host     主机名，如 "api.openai.com"
/// @param service  服务名或端口字符串，如 "443"（可选）
/// @return         解析到的 IP 地址字符串列表（IPv4/IPv6 混合）
export auto async_resolve(io_context& ctx, std::string_view host,
                          std::string_view service = {})
    -> task<std::expected<std::vector<std::string>, std::string>>
{
    detail::resolve_awaitable aw{
        .ctx_     = ctx,
        .host_    = std::string(host),
        .service_ = std::string(service),
        .result_  = std::unexpected(std::string("not resolved")),
        .caller_  = {}
    };
    co_return co_await aw;
}

} // namespace cnetmod