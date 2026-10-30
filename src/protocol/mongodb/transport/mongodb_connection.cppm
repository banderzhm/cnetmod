module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.mongodb:connection;

import std;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :error;
import :bson_document;
import :connection_options;
import :wire_protocol;
import :sasl_reply;

export namespace cnetmod::mongodb {

/**
 * Consumes an exhaust OP_MSG response sequence.
 *
 * Returning an error stops consumption and retires the connection so unread
 * messages cannot corrupt a later command on the same socket.
 */
using command_stream_handler =
    std::function<task<result<void>>(bson_document)>;

class connection
{
public:
    explicit connection(io_context& context) noexcept;
    ~connection();
    connection(const connection&) = delete;
    auto operator=(const connection&) -> connection& = delete;
    connection(connection&&) = delete;
    auto operator=(connection&&) -> connection& = delete;

    auto connect(connection_options options = {}) -> task<result<void>>;
    /**
     * Connects with cancellation across transport setup and protocol commands.
     * Invoke on the owner loop with an exclusive token that outlives completion.
     */
    auto connect(connection_options options, cancel_token& cancellation) -> task<result<void>>;
    auto command(std::string_view database, bson_document command_document)
        -> task<result<bson_document>>;
    /// Executes a command and joins owner-thread caller cancellation.
    auto command(std::string_view database, bson_document command_document, cancel_token& cancellation)
        -> task<result<bson_document>>;
    /// Owns Kind 1 document batches across cancellation and timeout completion.
    auto command(std::string_view database, bson_document command_document,
        std::vector<document_sequence> sequences) -> task<result<bson_document>>;
    auto command(bson_document command_document)
        -> task<result<bson_document>>;
    /**
     * Executes a getMore command with the exhaustAllowed capability.
     *
     * The caller must first execute find or aggregate and pass the returned
     * cursor identifier in a getMore command. The handler receives every
     * response until the server clears the moreToCome flag.
     */
    auto command_stream(std::string_view database, bson_document command_document,
        command_stream_handler on_message) -> task<result<void>>;
    auto ping() -> task<result<void>>;
    /**
     * Pings with caller cancellation and joins its owner-thread notification.
     * The token must be exclusive to this operation and outlive completion.
     * Invoke on the connection's event loop; cancellation may come from any thread.
     */
    auto ping(cancel_token& cancellation) -> task<result<void>>;
    void cancel_active_command() noexcept;
    void close() noexcept;

    [[nodiscard]] auto is_open() const noexcept -> bool;
    [[nodiscard]] auto secure_channel() const noexcept -> bool;
    [[nodiscard]] auto capabilities() const noexcept
        -> const server_capabilities&;
    [[nodiscard]] auto hello_response() const noexcept -> const bson_document&;

private:
    template <bool Cancellable>
    auto connect_impl(connection_options options, cancel_token* transport) -> task<result<void>>;
    auto execute_command(std::string_view database,
        bson_document command_document, std::vector<document_sequence> sequences = {}) -> task<result<bson_document>>;
    auto execute_command_with_timer(std::string database,
        bson_document command_document, cancel_token& timer_token,
        std::vector<document_sequence> sequences)
        -> task<result<bson_document>>;
    auto command_timeout_watchdog(cancel_token& timer_token,
        std::atomic<bool>& timed_out) -> task<int>;
    auto execute_command_without_deadline(std::string_view database,
        bson_document command_document, std::vector<document_sequence> sequences = {}) -> task<result<bson_document>>;
    auto execute_command_stream(std::string_view database,
        bson_document command_document, command_stream_handler on_message)
        -> task<result<void>>;
    auto authenticate() -> task<result<void>>;
    auto authenticate_scram() -> task<result<void>>;
    auto authenticate_aws() -> task<result<void>>;
    auto authenticate_oidc() -> task<result<void>>;
    auto authenticate_x509() -> task<result<void>>;
    auto sasl_start(std::string_view mechanism, std::vector<std::byte> payload) -> task<result<sasl_reply>>;
    auto sasl_continue(std::int32_t conversation, std::vector<std::byte> payload) -> task<result<sasl_reply>>;
    auto read_exact(std::span<std::byte> destination)
        -> task<result<void>>;
    auto write_all(std::span<const std::byte> source)
        -> task<result<void>>;
    auto receive_response(std::int32_t expected_response_to)
        -> task<result<bson_document>>;
    auto receive_message(std::int32_t expected_response_to)
        -> task<result<decoded_message>>;

    io_context& context_;
    socket socket_;
    connection_options options_;
    server_capabilities capabilities_;
    bson_document hello_response_;
    std::int32_t next_request_id_ = 1;
    bool connected_ = false;
    bool connecting_ = false;
    bool authenticated_ = false;
    bool reauthenticating_ = false;
    std::optional<std::chrono::steady_clock::time_point> command_deadline_;
    std::string oidc_access_token_;
    bool command_in_progress_ = false;
    std::atomic<bool> active_command_{false};
    std::atomic<bool> command_cancel_requested_{false};
    cancel_token command_io_cancel_;
    std::optional<std::uint8_t> selected_compressor_;
#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> tls_context_;
    std::unique_ptr<ssl_stream> tls_stream_;
#endif
};

} // namespace cnetmod::mongodb
