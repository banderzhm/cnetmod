/// cnetmod.core.dns — Async DNS resolution
/// Uses stdexec::static_thread_pool to execute blocking getaddrinfo
/// Resumes coroutine via io_context::post after completion

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
// Global thread pool (stdexec) — For offloading blocking operations
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
// detail::resolve_awaitable — stdexec thread pool async DNS
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
        // Schedule blocking getaddrinfo on stdexec thread pool, post back to io_context when done
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
// async_resolve — Async DNS resolution
// =============================================================================

/// Async resolve hostname to IP address list
/// @param ctx      Event loop context (for resuming coroutine)
/// @param host     Hostname, e.g. "api.openai.com"
/// @param service  Service name or port string, e.g. "443" (optional)
/// @return         List of resolved IP address strings (IPv4/IPv6 mixed)
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