/// cnetmod.protocol.mqtt:sync_client — MQTT 同步客户端
/// 包装异步 client，提供阻塞式 API

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
// MQTT 同步客户端
// =============================================================================

/// 同步客户端：包装异步 client，使用 io_context 驱动协程到完成
/// 适用于简单脚本、测试和不需要协程的场景
export class sync_client {
public:
    explicit sync_client()
        : ctx_(make_io_context()), client_(*ctx_) {}

    ~sync_client() = default;

    // 不可复制
    sync_client(const sync_client&) = delete;
    auto operator=(const sync_client&) -> sync_client& = delete;

    /// 同步连接到 MQTT Broker
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

    /// 同步发布消息
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

    /// 同步订阅
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

    /// 同步取消订阅
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

    /// 同步断开连接
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

    /// 注册消息到达回调
    void on_message(message_callback cb) { client_.on_message(std::move(cb)); }

    /// 注册断连回调
    void on_disconnect(disconnect_callback cb) { client_.on_disconnect(std::move(cb)); }

    /// 驱动事件循环一次（处理收到的消息等）
    void poll() { ctx_->poll(); }

    /// 驱动事件循环直到没有待处理事件
    void poll_all() { ctx_->poll(); }

    /// 获取底层异步 client
    [[nodiscard]] auto async_client() noexcept -> client& { return client_; }
    [[nodiscard]] auto async_client() const noexcept -> const client& { return client_; }

    /// 获取 io_context
    [[nodiscard]] auto context() noexcept -> io_context& { return *ctx_; }

    /// 是否已连接
    [[nodiscard]] auto is_connected() const noexcept -> bool {
        return client_.is_connected();
    }

private:
    /// 驱动事件循环直到条件满足
    void run_until(bool& done) {
        while (!done) {
            ctx_->poll();
        }
    }

    std::unique_ptr<io_context> ctx_;
    client                      client_;
};

} // namespace cnetmod::mqtt
