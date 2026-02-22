/// cnetmod.protocol.mqtt:sync_client — MQTT synchronous client
/// Wraps async client, provides blocking API

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:sync_client;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import :types;
import :client;

namespace cnetmod::mqtt {

// =============================================================================
// MQTT synchronous client
// =============================================================================

/// Synchronous client: wraps async client, uses io_context to drive coroutines to completion
/// Suitable for simple scripts, tests, and scenarios that don't need coroutines
export class sync_client {
public:
    explicit sync_client()
        : ctx_(make_io_context()), client_(*ctx_) {}

    ~sync_client() = default;

    // Non-copyable
    sync_client(const sync_client&) = delete;
    auto operator=(const sync_client&) -> sync_client& = delete;

    /// Synchronously connect to MQTT Broker
    auto connect_sync(connect_options opts = {}) -> std::expected<void, std::string> {
        std::expected<void, std::string> result;
        bool done = false;

        spawn(*ctx_, [&]() -> task<void> {
            result = co_await client_.connect(std::move(opts));
            done = true;
        }());

        run_until(done);
        return result;
    }

    /// Synchronously publish message
    auto publish_sync(
        std::string_view topic,
        std::string_view payload,
        qos q = qos::at_most_once,
        bool retain = false,
        const properties& props = {}
    ) -> std::expected<void, std::string>
    {
        std::expected<void, std::string> result;
        bool done = false;

        spawn(*ctx_, [&]() -> task<void> {
            result = co_await client_.publish(topic, payload, q, retain, props);
            done = true;
        }());

        run_until(done);
        return result;
    }

    /// Synchronously subscribe
    auto subscribe_sync(
        std::string topic_filter,
        qos max_qos = qos::at_most_once,
        const properties& props = {}
    ) -> std::expected<std::vector<std::uint8_t>, std::string>
    {
        std::expected<std::vector<std::uint8_t>, std::string> result;
        bool done = false;

        spawn(*ctx_, [&]() -> task<void> {
            result = co_await client_.subscribe(std::move(topic_filter), max_qos, props);
            done = true;
        }());

        run_until(done);
        return result;
    }

    /// Synchronously unsubscribe
    auto unsubscribe_sync(
        std::vector<std::string> topic_filters,
        const properties& props = {}
    ) -> std::expected<void, std::string>
    {
        std::expected<void, std::string> result;
        bool done = false;

        spawn(*ctx_, [&]() -> task<void> {
            result = co_await client_.unsubscribe(std::move(topic_filters), props);
            done = true;
        }());

        run_until(done);
        return result;
    }

    /// Synchronously disconnect
    auto disconnect_sync(
        std::uint8_t reason_code = 0,
        const properties& props = {}
    ) -> std::expected<void, std::string>
    {
        std::expected<void, std::string> result;
        bool done = false;

        spawn(*ctx_, [&]() -> task<void> {
            result = co_await client_.disconnect(reason_code, props);
            done = true;
        }());

        run_until(done);
        return result;
    }

    /// Register message arrival callback
    void on_message(message_callback cb) { client_.on_message(std::move(cb)); }

    /// Register disconnection callback
    void on_disconnect(disconnect_callback cb) { client_.on_disconnect(std::move(cb)); }

    /// Drive event loop once (process received messages, etc.)
    void poll() { ctx_->poll(); }

    /// Drive event loop until no pending events
    void poll_all() { ctx_->poll(); }

    /// Get underlying async client
    [[nodiscard]] auto async_client() noexcept -> client& { return client_; }
    [[nodiscard]] auto async_client() const noexcept -> const client& { return client_; }

    /// Get io_context
    [[nodiscard]] auto context() noexcept -> io_context& { return *ctx_; }

    /// Whether connected
    [[nodiscard]] auto is_connected() const noexcept -> bool {
        return client_.is_connected();
    }

private:
    /// Drive event loop until condition is met
    void run_until(bool& done) {
        while (!done) {
            ctx_->poll();
        }
    }

    std::unique_ptr<io_context> ctx_;
    client                      client_;
};

} // namespace cnetmod::mqtt
