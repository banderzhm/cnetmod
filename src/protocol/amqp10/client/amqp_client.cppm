module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp10:amqp_client;
import std;
import :client_configuration;
import :client_error;
import :reconnect_policy;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import :primitive_value;
import :connection_state;
import :performative_model;
import :amqp_session;
import :performative_channel;

export namespace cnetmod::amqp10 {
struct client_options
{
    amqp10::endpoint endpoint;
    amqp10::credentials credentials;
    std::string container_id;
    std::string hostname;
    std::uint32_t max_frame_size = 262144;
    std::uint16_t channel_max = 65535;
    std::chrono::milliseconds idle_timeout{60000};
    std::shared_ptr<const reconnect_policy> reconnect;
    bool recover_sessions = true;
};

using state_handler = std::function<void(connection_state)>;

class client : private performative_channel
{
public:
    explicit client(io_context&);
    ~client();
    client(client&&) noexcept;
    auto operator=(client&&) noexcept -> client&;
    client(const client&) = delete;
    auto operator=(const client&) -> client& = delete;
    auto connect(client_options, cancel_token&)
        -> task<std::expected<void, error>>;
    auto reconnect(cancel_token&) -> task<std::expected<void, error>>;
    [[nodiscard]] auto make_session(session_options = {})
        -> std::expected<session, error>;
    auto close(cancel_token&) -> task<std::expected<void, error>>;
    void on_state_change(state_handler);
    void on_disconnect(disconnect_handler);
    [[nodiscard]] auto state() const noexcept -> connection_state;
    [[nodiscard]] auto remote_properties() const
        -> std::map<symbol, value, std::less<>>;

private:
    friend class session;
    friend class sender_link;
    friend class receiver_link;
    friend class transaction_controller;
    struct impl;
    std::unique_ptr<impl> impl_;
    auto send(std::uint16_t, const performative&, cancel_token&)
        -> task<std::expected<void, error>> override;
    auto receive(std::uint16_t, cancel_token&)
        -> task<std::expected<performative, error>> override;
    [[nodiscard]] auto maximum_frame_size() const noexcept
        -> std::uint32_t override;
    void register_recovery_observer(recovery_observer&) override;
    void unregister_recovery_observer(recovery_observer&) noexcept override;
    auto heartbeat_loop(std::shared_ptr<cancel_token>) -> task<void>;
    auto read_pump(std::shared_ptr<cancel_token>) -> task<void>;
    auto automatic_reconnect(std::shared_ptr<cancel_token>) -> task<void>;
};
} // namespace cnetmod::amqp10
