module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.websocket:client;

import std;
import :types;
import :connection;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.timer;

namespace cnetmod::ws {

export struct client_options {
    connect_options connect;
    std::chrono::steady_clock::duration heartbeat_interval{0};
    std::vector<std::byte> heartbeat_payload;
};

export class client {
public:
    explicit client(io_context& ctx) noexcept
        : ctx_(ctx), conn_(ctx) {}

    client(const client&) = delete;
    auto operator=(const client&) -> client& = delete;

    client(client&&) noexcept = default;
    auto operator=(client&&) noexcept -> client& = default;

    [[nodiscard]] auto connect(std::string_view url, const client_options& opts = {})
        -> task<std::expected<void, std::error_code>>
    {
        options_ = opts;
        co_return co_await conn_.async_connect(url, opts.connect);
    }

    [[nodiscard]] auto send_text(std::string_view text)
        -> task<std::expected<void, std::error_code>>
    {
        co_return co_await conn_.async_send_text(text);
    }

    [[nodiscard]] auto send_binary(std::span<const std::byte> data)
        -> task<std::expected<void, std::error_code>>
    {
        co_return co_await conn_.async_send_binary(data);
    }

    [[nodiscard]] auto ping(std::span<const std::byte> payload = {})
        -> task<std::expected<void, std::error_code>>
    {
        co_return co_await conn_.async_ping(payload);
    }

    [[nodiscard]] auto recv()
        -> task<std::expected<ws_message, std::error_code>>
    {
        co_return co_await conn_.async_recv();
    }

    [[nodiscard]] auto close(std::uint16_t code = close_code::normal,
                             std::string_view reason = "")
        -> task<std::expected<void, std::error_code>>
    {
        co_return co_await conn_.async_close(code, reason);
    }

    /// Run heartbeat until the connection closes or ping fails.
    [[nodiscard]] auto run_heartbeat()
        -> task<std::expected<void, std::error_code>>
    {
        if (options_.heartbeat_interval <= std::chrono::steady_clock::duration::zero()) {
            co_return {};
        }

        steady_timer timer{ctx_};
        while (conn_.is_open()) {
            auto wait = co_await timer.async_wait(options_.heartbeat_interval);
            if (!wait) {
                co_return std::unexpected(wait.error());
            }

            auto pong = co_await conn_.async_ping(options_.heartbeat_payload);
            if (!pong) {
                co_return std::unexpected(pong.error());
            }
        }
        co_return {};
    }

    [[nodiscard]] auto is_open() const noexcept -> bool { return conn_.is_open(); }
    [[nodiscard]] auto& native_connection() noexcept { return conn_; }
    [[nodiscard]] auto& native_connection() const noexcept { return conn_; }

private:
    io_context& ctx_;
    connection conn_;
    client_options options_;
};

} // namespace cnetmod::ws
