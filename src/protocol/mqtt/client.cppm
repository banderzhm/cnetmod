/// cnetmod.protocol.mqtt:client ? MQTT Async Client
/// Full coroutine-based client supporting MQTT v3.1.1 and v5.0

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.mqtt:client;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import :types;

namespace cnetmod::mqtt {

export using message_callback = std::function<void(const publish_message&)>;
export using disconnect_callback = std::function<void(std::string reason)>;
export using auth_callback = std::function<std::optional<std::pair<std::uint8_t, properties>>(std::uint8_t reason_code, const properties& props)>;

export struct reconnect_options {
    bool enabled = false;
    std::uint32_t max_retries = 0;
    std::chrono::milliseconds initial_delay = std::chrono::seconds(1);
    std::chrono::milliseconds max_delay = std::chrono::seconds(60);
    double backoff_multiplier = 2.0;
    bool restore_subscriptions = true;
};

export class client {
public:
    explicit client(io_context& ctx) noexcept;
    ~client();
    client(const client&) = delete;
    auto operator=(const client&) -> client& = delete;
    auto connect(connect_options opts = {}) -> task<std::expected<void, std::string>>;
    [[nodiscard]] auto is_connected() const noexcept -> bool;
    [[nodiscard]] auto session_present() const noexcept -> bool;
    [[nodiscard]] auto connack_properties() const noexcept -> const properties&;
    void close() noexcept;
    auto publish(std::string_view topic,std::string_view payload,qos q = qos::at_most_once,bool retain = false,const properties& props = {}) -> task<std::expected<void, std::string>>;
    auto subscribe(std::vector<subscribe_entry> entries,const properties& props = {}) -> task<std::expected<std::vector<std::uint8_t>, std::string>>;
    auto subscribe(std::string topic_filter,qos max_qos = qos::at_most_once,const properties& props = {}) -> task<std::expected<std::vector<std::uint8_t>, std::string>>;
    auto unsubscribe(std::vector<std::string> topic_filters,const properties& props = {}) -> task<std::expected<void, std::string>>;
    auto disconnect(std::uint8_t reason_code = 0,const properties& props = {}) -> task<std::expected<void, std::string>>;
    void on_message(message_callback cb);
    void on_disconnect(disconnect_callback cb);
    void on_auth(auth_callback cb);
    void set_reconnect(reconnect_options opts);
    auto send_auth(std::uint8_t reason_code = 0,const properties& props = {}) -> task<std::expected<void, std::string>>;
    [[nodiscard]] auto version() const noexcept -> protocol_version;
private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

} // namespace cnetmod::mqtt