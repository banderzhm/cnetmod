module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp10:receiver_link;
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

struct received_message
{
    std::uint32_t delivery_id = 0;
    binary delivery_tag;
    message payload;
    bool settled = false;
    bool resumed = false;
};

class receiver_link
{
public:
    ~receiver_link();
    receiver_link(receiver_link&&) noexcept;
    auto operator=(receiver_link&&) noexcept -> receiver_link&;
    receiver_link(const receiver_link&) = delete;
    auto operator=(const receiver_link&) -> receiver_link& = delete;
    auto attach(std::uint32_t initial_credit, cancel_token&)
        -> task<std::expected<void, error>>;
    auto receive(cancel_token&)
        -> task<std::expected<received_message, error>>;
    auto add_credit(std::uint32_t credit, bool drain, cancel_token&)
        -> task<std::expected<void, error>>;
    auto settle(std::uint32_t delivery_id, delivery_outcome outcome,
        cancel_token&) -> task<std::expected<void, error>>;
    auto detach(bool close_link, cancel_token&)
        -> task<std::expected<void, error>>;
    [[nodiscard]] auto state() const noexcept -> link_state;
    [[nodiscard]] auto credit() const noexcept -> std::uint32_t;
    [[nodiscard]] auto name() const noexcept -> std::string_view;

private:
    friend class session;
    struct impl;
    explicit receiver_link(std::unique_ptr<impl>);
    static auto create(performative_channel&, std::uint16_t, std::uint32_t,
        std::string, source, sender_settle_mode,
        receiver_settle_mode) -> receiver_link;
    std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::amqp10
