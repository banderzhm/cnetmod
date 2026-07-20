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
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.coro.cancel;
import cnetmod.core.address;
import cnetmod.core.error;
import cnetmod.core.socket;
import cnetmod.executor.async_op;

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

struct dns_cache_entry {
    std::vector<ip_address> addresses;
    std::chrono::steady_clock::time_point expires_at;
};

struct address_health {
    std::uint64_t failures = 0;
    std::uint64_t successes = 0;
    std::chrono::steady_clock::time_point downgrade_until{};
    std::string last_error;
};

struct resolver_state {
    std::mutex mutex;
    bool cache_enabled = true;
    std::chrono::milliseconds ttl{60000};
    std::unordered_map<std::string, dns_cache_entry> cache;
    std::unordered_map<std::string, address_health> health;
    std::uint64_t cache_hits = 0;
    std::uint64_t cache_misses = 0;
    std::uint64_t resolve_failures = 0;
    std::uint64_t ipv4_successes = 0;
    std::uint64_t ipv6_successes = 0;
    std::uint64_t connection_failures = 0;
};

inline auto resolver_state_instance() -> resolver_state& {
    static resolver_state state;
    return state;
}

inline auto cache_key(std::string_view host, std::string_view service) -> std::string {
    std::string key(host);
    key.push_back('\0');
    key.append(service);
    return key;
}

inline auto address_key(const ip_address& addr) -> std::string {
    return addr.to_string();
}

inline auto apply_address_policy(std::vector<ip_address> addresses)
    -> std::vector<ip_address>
{
    auto& state = resolver_state_instance();
    const auto now = std::chrono::steady_clock::now();

    std::unordered_map<std::string, address_health> health;
    {
        std::scoped_lock lock(state.mutex);
        health = state.health;
    }

    std::stable_sort(addresses.begin(), addresses.end(),
        [&](const ip_address& lhs, const ip_address& rhs) {
            const auto lk = address_key(lhs);
            const auto rk = address_key(rhs);
            const auto li = health.find(lk);
            const auto ri = health.find(rk);
            const bool l_down = li != health.end() && li->second.downgrade_until > now;
            const bool r_down = ri != health.end() && ri->second.downgrade_until > now;
            if (l_down != r_down) return !l_down;
            const auto lf = li == health.end() ? 0 : li->second.failures;
            const auto rf = ri == health.end() ? 0 : ri->second.failures;
            if (lf != rf) return lf < rf;
            return lhs.to_string() < rhs.to_string();
        });
    return addresses;
}

inline auto happy_order(const std::vector<ip_address>& addresses)
    -> std::vector<ip_address>
{
    std::vector<ip_address> v4;
    std::vector<ip_address> v6;
    for (const auto& addr : addresses) {
        if (addr.is_v4()) v4.push_back(addr);
        else v6.push_back(addr);
    }

    const bool prefer_v6 = !addresses.empty() && addresses.front().is_v6();
    std::vector<ip_address> ordered;
    ordered.reserve(addresses.size());
    std::size_t i4 = 0;
    std::size_t i6 = 0;
    while (i4 < v4.size() || i6 < v6.size()) {
        if (prefer_v6) {
            if (i6 < v6.size()) ordered.push_back(v6[i6++]);
            if (i4 < v4.size()) ordered.push_back(v4[i4++]);
        } else {
            if (i4 < v4.size()) ordered.push_back(v4[i4++]);
            if (i6 < v6.size()) ordered.push_back(v6[i6++]);
        }
    }
    return ordered;
}

} // namespace detail

export struct dns_cache_config {
    bool enabled = true;
    std::chrono::milliseconds ttl{60000};
};

export struct dns_cache_metrics {
    std::uint64_t cache_hits = 0;
    std::uint64_t cache_misses = 0;
    std::uint64_t resolve_failures = 0;
    std::uint64_t ipv4_successes = 0;
    std::uint64_t ipv6_successes = 0;
    std::uint64_t connection_failures = 0;
    std::size_t cached_hosts = 0;
    std::size_t downgraded_addresses = 0;
};

export struct connect_metrics {
    std::size_t resolved_address_count = 0;
    std::size_t attempted_count = 0;
    std::size_t fallback_count = 0;
    address_family selected_family = address_family::unspecified;
    endpoint selected_endpoint{};
    std::string last_error;
};

export struct happy_eyeballs_options {
    std::chrono::milliseconds fallback_delay{250};
    std::chrono::steady_clock::duration connect_timeout{std::chrono::seconds{10}};
    socket_options socket_opts{.reuse_address = true, .non_blocking = true};
};

export struct connect_result {
    socket sock;
    endpoint remote;
    connect_metrics metrics;
};

export void configure_dns_cache(dns_cache_config cfg) {
    auto& state = detail::resolver_state_instance();
    std::scoped_lock lock(state.mutex);
    state.cache_enabled = cfg.enabled;
    state.ttl = cfg.ttl;
    if (!state.cache_enabled) {
        state.cache.clear();
    }
}

export void clear_dns_cache() {
    auto& state = detail::resolver_state_instance();
    std::scoped_lock lock(state.mutex);
    state.cache.clear();
}

export auto get_dns_cache_metrics() -> dns_cache_metrics {
    auto& state = detail::resolver_state_instance();
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock lock(state.mutex);
    std::size_t downgraded = 0;
    for (const auto& [_, health] : state.health) {
        if (health.downgrade_until > now) {
            ++downgraded;
        }
    }
    return dns_cache_metrics{
        .cache_hits = state.cache_hits,
        .cache_misses = state.cache_misses,
        .resolve_failures = state.resolve_failures,
        .ipv4_successes = state.ipv4_successes,
        .ipv6_successes = state.ipv6_successes,
        .connection_failures = state.connection_failures,
        .cached_hosts = state.cache.size(),
        .downgraded_addresses = downgraded,
    };
}

export void report_address_connect_success(const ip_address& addr) {
    auto& state = detail::resolver_state_instance();
    std::scoped_lock lock(state.mutex);
    auto& health = state.health[detail::address_key(addr)];
    ++health.successes;
    health.failures = 0;
    health.downgrade_until = {};
    health.last_error.clear();
    if (addr.is_v4()) ++state.ipv4_successes;
    else ++state.ipv6_successes;
}

export void report_address_connect_failure(const ip_address& addr,
                                           std::string_view error) {
    auto& state = detail::resolver_state_instance();
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock lock(state.mutex);
    auto& health = state.health[detail::address_key(addr)];
    ++health.failures;
    ++state.connection_failures;
    const auto shift = static_cast<int>(std::min<std::uint64_t>(health.failures, 6));
    const auto backoff = std::min(std::chrono::seconds{1 << shift}, std::chrono::seconds{60});
    health.downgrade_until = now + backoff;
    health.last_error = std::string(error);
}

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

export auto async_resolve_addresses(io_context& ctx,
                                    std::string_view host,
                                    std::string_view service = {})
    -> task<std::expected<std::vector<ip_address>, std::string>>
{
    if (auto literal = ip_address::from_string(host)) {
        co_return std::vector<ip_address>{*literal};
    }

    const auto key = detail::cache_key(host, service);
    const auto now = std::chrono::steady_clock::now();
    {
        auto& state = detail::resolver_state_instance();
        std::scoped_lock lock(state.mutex);
        if (state.cache_enabled) {
            auto it = state.cache.find(key);
            if (it != state.cache.end() && it->second.expires_at > now) {
                ++state.cache_hits;
                co_return detail::apply_address_policy(it->second.addresses);
            }
            if (it != state.cache.end()) {
                state.cache.erase(it);
            }
        }
        ++state.cache_misses;
    }

    auto resolved = co_await async_resolve(ctx, host, service);
    if (!resolved) {
        auto& state = detail::resolver_state_instance();
        std::scoped_lock lock(state.mutex);
        ++state.resolve_failures;
        co_return std::unexpected(resolved.error());
    }

    std::vector<ip_address> addrs;
    std::unordered_set<std::string> seen;
    for (const auto& candidate : *resolved) {
        auto parsed = ip_address::from_string(candidate);
        if (!parsed) {
            continue;
        }
        auto key = parsed->to_string();
        if (seen.insert(key).second) {
            addrs.push_back(*parsed);
        }
    }

    if (addrs.empty()) {
        co_return std::unexpected(std::string("no usable resolved addresses"));
    }

    {
        auto& state = detail::resolver_state_instance();
        std::scoped_lock lock(state.mutex);
        if (state.cache_enabled && state.ttl.count() > 0) {
            state.cache[key] = detail::dns_cache_entry{
                .addresses = addrs,
                .expires_at = now + state.ttl,
            };
        }
    }

    co_return detail::apply_address_policy(std::move(addrs));
}

namespace detail {

struct connect_race_state {
    io_context* ctx = nullptr;
    std::mutex mutex;
    std::coroutine_handle<> waiter{};
    bool completed = false;
    std::size_t remaining = 0;
    std::optional<connect_result> winner;
    connect_metrics metrics;
    std::vector<std::shared_ptr<cancel_token>> tokens;
};

inline void resume_connect_waiter(std::shared_ptr<connect_race_state> state) {
    std::coroutine_handle<> waiter;
    {
        std::scoped_lock lock(state->mutex);
        waiter = std::exchange(state->waiter, {});
    }
    if (waiter) {
        state->ctx->post(waiter);
    }
}

inline auto connect_attempt(io_context& ctx,
                            std::shared_ptr<connect_race_state> state,
                            endpoint remote,
                            socket_options socket_opts,
                            std::chrono::steady_clock::duration start_delay,
                            std::chrono::steady_clock::duration timeout)
    -> task<void>
{
    if (start_delay > std::chrono::steady_clock::duration::zero()) {
        (void)co_await async_timer_wait(ctx, start_delay);
    }

    {
        std::scoped_lock lock(state->mutex);
        if (state->completed) {
            co_return;
        }
        ++state->metrics.attempted_count;
        if (state->metrics.attempted_count > 1) {
            ++state->metrics.fallback_count;
        }
    }

    auto family = remote.address().is_v4() ? address_family::ipv4 : address_family::ipv6;
    auto sock_r = socket::create(family, socket_type::stream);
    if (!sock_r) {
        report_address_connect_failure(remote.address(), sock_r.error().message());
        bool done = false;
        {
            std::scoped_lock lock(state->mutex);
            state->metrics.last_error = sock_r.error().message();
            done = (--state->remaining == 0 && !state->completed);
            if (done) state->completed = true;
        }
        if (done) resume_connect_waiter(std::move(state));
        co_return;
    }

    auto sock = std::move(*sock_r);
    if (auto opts_r = sock.apply_options(socket_opts); !opts_r) {
        report_address_connect_failure(remote.address(), opts_r.error().message());
        bool done = false;
        {
            std::scoped_lock lock(state->mutex);
            state->metrics.last_error = opts_r.error().message();
            done = (--state->remaining == 0 && !state->completed);
            if (done) state->completed = true;
        }
        if (done) resume_connect_waiter(std::move(state));
        co_return;
    }

    auto token = std::make_shared<cancel_token>();
    {
        std::scoped_lock lock(state->mutex);
        state->tokens.push_back(token);
    }

    auto cr = timeout > std::chrono::steady_clock::duration::zero()
        ? co_await with_timeout(ctx, timeout,
              async_connect(ctx, sock, remote, *token), *token)
        : co_await async_connect(ctx, sock, remote, *token);

    if (cr) {
        report_address_connect_success(remote.address());
        bool won = false;
        {
            std::scoped_lock lock(state->mutex);
            if (!state->completed) {
                state->completed = true;
                state->metrics.selected_family = family;
                state->metrics.selected_endpoint = remote;
                state->winner.emplace(connect_result{
                    .sock = std::move(sock),
                    .remote = remote,
                    .metrics = state->metrics,
                });
                for (auto& other : state->tokens) {
                    other->cancel();
                }
                won = true;
            }
        }
        if (won) resume_connect_waiter(std::move(state));
        co_return;
    }

    report_address_connect_failure(remote.address(), cr.error().message());
    bool done = false;
    {
        std::scoped_lock lock(state->mutex);
        state->metrics.last_error = cr.error().message();
        done = (--state->remaining == 0 && !state->completed);
        if (done) state->completed = true;
    }
    if (done) resume_connect_waiter(std::move(state));
}

struct connect_race_awaitable {
    std::shared_ptr<connect_race_state> state;

    auto await_ready() const noexcept -> bool {
        std::scoped_lock lock(state->mutex);
        return state->completed;
    }

    void await_suspend(std::coroutine_handle<> h) {
        bool resume_now = false;
        {
            std::scoped_lock lock(state->mutex);
            if (state->completed) {
                resume_now = true;
            } else {
                state->waiter = h;
            }
        }
        if (resume_now) {
            state->ctx->post(h);
        }
    }

    auto await_resume() -> std::expected<connect_result, std::error_code> {
        std::scoped_lock lock(state->mutex);
        if (state->winner) {
            return std::move(*state->winner);
        }
        return std::unexpected(make_error_code(std::errc::host_unreachable));
    }
};

} // namespace detail

export auto async_connect_happy_eyeballs(io_context& ctx,
                                         std::string_view host,
                                         std::uint16_t port,
                                         happy_eyeballs_options opts = {})
    -> task<std::expected<connect_result, std::error_code>>
{
    auto resolved = co_await async_resolve_addresses(ctx, host, std::to_string(port));
    if (!resolved || resolved->empty()) {
        co_return std::unexpected(make_error_code(std::errc::host_unreachable));
    }

    auto ordered = detail::happy_order(*resolved);
    auto state = std::make_shared<detail::connect_race_state>();
    state->ctx = &ctx;
    state->remaining = ordered.size();
    state->metrics.resolved_address_count = ordered.size();

    for (std::size_t i = 0; i < ordered.size(); ++i) {
        auto delay = opts.fallback_delay * static_cast<int>(i);
        spawn(ctx, detail::connect_attempt(
            ctx,
            state,
            endpoint{ordered[i], port},
            opts.socket_opts,
            delay,
            opts.connect_timeout));
    }

    co_return co_await detail::connect_race_awaitable{state};
}

} // namespace cnetmod
