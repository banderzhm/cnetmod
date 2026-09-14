module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp091:protocol_connection;
import std;
import :delivery_acknowledgement;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import :protocol_constants;
import :connection_options;
import :wire_frame_codec;
import :message_delivery;
import :publisher_confirm;
import :topology_recovery;

export namespace cnetmod::amqp091 {
class logical_channel;

class connection_observer
{
public:
    virtual ~connection_observer() = default;
    virtual void on_state_changed(connection_state state) = 0;
    virtual void on_connection_error(const error& reason) = 0;
};

class protocol_connection
    : public std::enable_shared_from_this<protocol_connection>
{
public:
    explicit protocol_connection(io_context& context);
    ~protocol_connection();
    protocol_connection(const protocol_connection&) = delete;
    auto operator=(const protocol_connection&) -> protocol_connection& = delete;
    auto async_connect(connection_options options) -> task<result<void>>;
    /**
     * @brief Connects and authenticates, releasing transport on handshake failure.
     *
     * A failed attempt resets parser state before the same client is retried.
     * State observer failures during rollback do not replace the primary error.
     */
    auto async_connect(connection_options options, cancel_token& token)
        -> task<result<void>>;
    auto async_run(cancel_token& token) -> task<result<void>>;
    /**
     * @brief Owns one reader and topology replay without reconnecting or retrying.
     * Calls on_ready only after replay succeeds; the caller must await this task.
     */
    auto async_run_session(cancel_token& token, std::function<void()> on_ready) -> task<result<void>>;
    auto async_recover(cancel_token& token) -> task<result<void>>;
    auto async_close(std::string reply_text = "client shutdown")
        -> task<result<void>>;
    [[nodiscard]] auto state() const noexcept -> connection_state;
    [[nodiscard]] auto negotiated_frame_max() const noexcept -> std::uint32_t;
    [[nodiscard]] auto negotiated_channel_max() const noexcept -> std::uint16_t;
    auto async_open_channel() -> task<result<std::shared_ptr<logical_channel>>>;
    void observe(std::weak_ptr<connection_observer> observer);
    void set_return_handler(return_handler handler);
    void set_recovery_strategy(std::shared_ptr<recovery_strategy> strategy);

private:
    friend class logical_channel;
    friend class delivery_acknowledgement;
    /**
     * @brief Retires channel-owned callbacks and confirmations before suspension.
     */
    void retire_channel(std::uint16_t channel, const error& reason) noexcept;
    /**
     * @brief Interrupts a session whose accepted subscription cannot be registered.
     * Active I/O owners retain their borrowed transport until they finish.
     */
    void abort_subscription(std::uint64_t generation, std::uint16_t channel) noexcept;
    auto async_settle_delivery(std::uint64_t generation, std::uint16_t channel,
        std::uint64_t tag, std::uint16_t method, std::uint8_t flags) -> task<result<void>>;
    struct impl;
    std::unique_ptr<impl> impl_;
    template <bool Recovery>
    auto async_connect_attempt(connection_options options, cancel_token& token)
        -> task<result<void>>;
    template <typename OnStarted>
    auto async_run_with_start(cancel_token& token, OnStarted on_started) -> task<result<void>>;
    template <bool Recovery>
    auto async_open_channel_attempt() -> task<result<std::shared_ptr<logical_channel>>>;
    auto async_replay_topology(const topology_snapshot& saved, cancel_token& token) -> task<result<void>>;
    /**
     * @brief Identifies the connection attempt that owns a logical channel.
     *
     * Access follows the connection's single-executor contract. A channel from
     * an earlier attempt must never become valid when a later handshake opens.
     */
    [[nodiscard]] auto generation() const noexcept -> std::uint64_t;
    auto async_receive_frames(cancel_token& token) -> task<result<void>>;
    auto async_rpc(method_frame request, std::uint16_t expected_class,
        std::uint16_t expected_method) -> task<result<method_frame>>;
    auto async_send(frame value) -> task<result<void>>;
    auto async_send_message(std::uint16_t channel, method_frame publish,
        message message, publisher_confirm_tracker* confirmations, std::uint64_t generation)
        -> task<result<std::uint64_t>>;
    void register_delivery_handler(std::uint16_t channel,
        std::string consumer_tag,
        delivery_handler handler);
    void unregister_delivery_handler(std::uint16_t channel,
        std::string_view consumer_tag);
    auto confirm_tracker(std::uint16_t channel)
        -> std::shared_ptr<publisher_confirm_tracker>;
    auto topology() -> std::shared_ptr<topology_recorder>;
};
} // namespace cnetmod::amqp091
