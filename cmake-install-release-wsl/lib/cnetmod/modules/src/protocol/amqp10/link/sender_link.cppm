module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp10:sender_link;
import std;
import :client_configuration;
import :client_error;
import :reconnect_policy;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import :link_state;
import :delivery_state;
import :message_section;

export namespace cnetmod::amqp10 {
class performative_channel;

struct send_options
{
    bool settled = false;
    bool batchable = false;
    std::optional<binary> transaction_id;
};

struct send_result
{
    std::uint32_t delivery_id = 0;
    delivery_outcome outcome;
};

class sender_link
{
public:
    ~sender_link();
    sender_link(sender_link&&) noexcept;
    auto operator=(sender_link&&) noexcept -> sender_link&;
    sender_link(const sender_link&) = delete;
    auto operator=(const sender_link&) -> sender_link& = delete;
    auto attach(cancel_token&) -> task<std::expected<void, error>>;
    /// Writes one delivery without waiting for its remote disposition. The
    /// returned id can be passed to await_outcome(), allowing callers to keep a
    /// bounded window of unsettled deliveries in flight.
    auto begin_send(const message&, send_options, cancel_token&)
        -> task<std::expected<std::uint32_t, error>>;
    /// Waits for the remote disposition of a delivery started by begin_send().
    auto await_outcome(std::uint32_t delivery_id, cancel_token&)
        -> task<std::expected<send_result, error>>;
    auto send(const message&, send_options, cancel_token&)
        -> task<std::expected<send_result, error>>;
    auto detach(bool close_link, cancel_token&)
        -> task<std::expected<void, error>>;
    [[nodiscard]] auto state() const noexcept -> link_state;
    [[nodiscard]] auto credit() const noexcept -> std::uint32_t;
    /// Number of locally tracked deliveries that have not received a remote
    /// disposition yet.
    [[nodiscard]] auto pending_unsettled_count() const noexcept -> std::size_t;
    [[nodiscard]] auto name() const noexcept -> std::string_view;

private:
    friend class session;
    struct impl;
    explicit sender_link(std::unique_ptr<impl>);
    static auto create(performative_channel&, std::uint16_t, std::uint32_t,
        std::string, target, sender_settle_mode,
        receiver_settle_mode) -> sender_link;
    std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::amqp10
