/// cnetmod.protocol.coap:coaps_session_manager - CoAPS peer table and UDP demux.

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.coap:coaps_session_manager;

#ifdef CNETMOD_HAS_SSL

import std;
import :coaps_session;
import cnetmod.core.address;
import cnetmod.core.socket;
import cnetmod.core.ssl;
import cnetmod.coro.task;
import cnetmod.io.io_context;

namespace cnetmod::coap {

export struct coaps_session_manager_options {
    coaps_session_options session;
    std::size_t max_sessions = 1024;
    std::size_t queue_capacity = 64;
    std::chrono::steady_clock::duration idle_timeout = std::chrono::seconds{120};
};

export class coaps_session_manager {
public:
    coaps_session_manager(io_context& ctx,
                          ssl_context& ssl_ctx,
                          socket& sock,
                          coaps_session_manager_options options);

    coaps_session_manager(const coaps_session_manager&) = delete;
    auto operator=(const coaps_session_manager&) -> coaps_session_manager& = delete;

    void set_response_handler(coaps_response_handler handler);
    auto dispatch(const endpoint& peer, std::span<const std::byte> datagram) -> bool;
    void stop() noexcept;
    [[nodiscard]] auto session_count() const -> std::size_t;

private:
    struct session_state;

    static auto peer_key(const endpoint& peer) -> std::string;
    auto run_session(endpoint peer, std::shared_ptr<session_state> state) -> task<void>;
    void remove_session(std::string key, const std::shared_ptr<session_state>& state);
    void prune_idle_sessions_locked(std::chrono::steady_clock::time_point now);

    io_context& ctx_;
    ssl_context& ssl_ctx_;
    socket& sock_;
    coaps_session_manager_options options_;
    coaps_response_handler response_handler_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<session_state>> sessions_;
    bool stopping_ = false;
};

} // namespace cnetmod::coap

#endif // CNETMOD_HAS_SSL
