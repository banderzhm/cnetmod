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

export namespace cnetmod::mongodb {

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
    auto command(std::string_view database, bson_document command_document)
        -> task<result<bson_document>>;
    auto command(bson_document command_document)
        -> task<result<bson_document>>;
    auto ping() -> task<result<void>>;
    void cancel_active_command() noexcept;
    void close() noexcept;

    [[nodiscard]] auto is_open() const noexcept -> bool;
    [[nodiscard]] auto secure_channel() const noexcept -> bool;
    [[nodiscard]] auto capabilities() const noexcept
        -> const server_capabilities&;
    [[nodiscard]] auto hello_response() const noexcept -> const bson_document&;

private:
    auto execute_command(std::string_view database,
        bson_document command_document) -> task<result<bson_document>>;
    auto execute_command_with_timer(std::string database,
        bson_document command_document, cancel_token& timer_token)
        -> task<result<bson_document>>;
    auto command_timeout_watchdog(cancel_token& timer_token,
        std::atomic<bool>& timed_out) -> task<int>;
    auto execute_command_without_deadline(std::string_view database,
        bson_document command_document) -> task<result<bson_document>>;
    auto authenticate() -> task<result<void>>;
    auto read_exact(std::span<std::byte> destination)
        -> task<result<void>>;
    auto write_all(std::span<const std::byte> source)
        -> task<result<void>>;
    auto receive_response(std::int32_t expected_response_to)
        -> task<result<bson_document>>;

    io_context& context_;
    socket socket_;
    connection_options options_;
    server_capabilities capabilities_;
    bson_document hello_response_;
    std::int32_t next_request_id_ = 1;
    bool connected_ = false;
    bool authenticated_ = false;
    bool command_in_progress_ = false;
    std::atomic<bool> active_command_{false};
    std::atomic<bool> command_cancel_requested_{false};
    std::optional<std::uint8_t> selected_compressor_;
#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> tls_context_;
    std::unique_ptr<ssl_stream> tls_stream_;
#endif
};

} // namespace cnetmod::mongodb
