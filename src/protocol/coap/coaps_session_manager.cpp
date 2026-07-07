module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

#ifdef CNETMOD_HAS_SSL

import :coaps_session_manager;

import std;
import :coaps_session;
import cnetmod.core.address;
import cnetmod.coro.spawn;
import cnetmod.coro.task;

namespace cnetmod::coap {

struct coaps_session_manager::session_state {
    explicit session_state(std::size_t queue_capacity)
        : inbound(std::make_shared<coaps_datagram_queue>(
              std::max<std::size_t>(queue_capacity, 1))) {}

    std::shared_ptr<coaps_datagram_queue> inbound;
    std::chrono::steady_clock::time_point updated_at = std::chrono::steady_clock::now();
};

coaps_session_manager::coaps_session_manager(io_context& ctx,
                                             ssl_context& ssl_ctx,
                                             socket& sock,
                                             coaps_session_manager_options options)
    : ctx_(ctx)
    , ssl_ctx_(ssl_ctx)
    , sock_(sock)
    , options_(options)
{}

void coaps_session_manager::set_response_handler(coaps_response_handler handler) {
    response_handler_ = std::move(handler);
}

auto coaps_session_manager::dispatch(const endpoint& peer,
                                     std::span<const std::byte> datagram) -> bool
{
    if (datagram.empty()) {
        return false;
    }

    auto key = peer_key(peer);
    std::shared_ptr<session_state> state;
    bool created = false;
    {
        std::scoped_lock lock(mutex_);
        if (stopping_) {
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        prune_idle_sessions_locked(now);

        auto it = sessions_.find(key);
        if (it == sessions_.end()) {
            if (sessions_.size() >= options_.max_sessions) {
                return false;
            }
            state = std::make_shared<session_state>(options_.queue_capacity);
            sessions_.emplace(key, state);
            created = true;
        } else {
            state = it->second;
        }
        state->updated_at = now;
    }

    std::vector<std::byte> copy(datagram.begin(), datagram.end());
    if (!state->inbound->try_send(std::move(copy))) {
        return false;
    }

    if (created) {
        spawn(ctx_, run_session(peer, state));
    }
    return true;
}

void coaps_session_manager::stop() noexcept {
    std::scoped_lock lock(mutex_);
    stopping_ = true;
    for (auto& [_, state] : sessions_) {
        state->inbound->close();
    }
    sessions_.clear();
}

auto coaps_session_manager::session_count() const -> std::size_t {
    std::scoped_lock lock(mutex_);
    return sessions_.size();
}

auto coaps_session_manager::peer_key(const endpoint& peer) -> std::string {
    return peer.to_string();
}

auto coaps_session_manager::run_session(endpoint peer,
                                        std::shared_ptr<session_state> state) -> task<void>
{
    const auto key = peer_key(peer);
    coaps_session session{
        ctx_,
        ssl_ctx_,
        sock_,
        peer,
        options_.session,
        response_handler_,
    };
    co_await session.run(state->inbound);
    remove_session(key, state);
}

void coaps_session_manager::remove_session(std::string key,
                                           const std::shared_ptr<session_state>& state)
{
    std::scoped_lock lock(mutex_);
    auto it = sessions_.find(key);
    if (it != sessions_.end() && it->second == state) {
        sessions_.erase(it);
    }
    state->inbound->close();
}

void coaps_session_manager::prune_idle_sessions_locked(
    std::chrono::steady_clock::time_point now)
{
    if (options_.idle_timeout <= std::chrono::steady_clock::duration::zero()) {
        return;
    }

    for (auto it = sessions_.begin(); it != sessions_.end();) {
        auto& state = it->second;
        if (now - state->updated_at >= options_.idle_timeout) {
            state->inbound->close();
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace cnetmod::coap

#endif // CNETMOD_HAS_SSL
