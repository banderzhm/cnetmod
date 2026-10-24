module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.websocket:connection;

import std;

import cnetmod.protocol.http;
import :types;
import :frame;
import :handshake;
import cnetmod.protocol.http.semantics;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.coro.cancel;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::ws {

export struct connect_options
{
    std::string subprotocol;
    std::string origin;
#ifdef CNETMOD_HAS_SSL
    bool tls_verify = true;
    std::string tls_ca_file;
#endif
};

/// Collects complete messages for the current receive burst. All queued frames
/// are written before the loop waits for more input; no timer delays a reply.
export class message_writer
{
public:
    auto send(opcode op, std::span<const std::byte> payload)
        -> std::expected<void, std::error_code>;

private:
    friend class connection;
    message_writer(std::vector<std::byte>& output, bool mask, std::size_t limit);
    void trust_input(opcode op, std::span<const std::byte> payload,
        bool defer_exact_output) noexcept;
    [[nodiscard]] auto finish_trusted_input() noexcept -> bool;
    void materialize_deferred_output();
    std::vector<std::byte>& output_;
    bool mask_;
    std::size_t limit_;
    std::error_code error_;
    opcode trusted_op_{opcode::continuation};
    const std::byte* trusted_data_{};
    std::size_t trusted_size_{};
    bool defer_exact_output_{};
    bool exact_output_deferred_{};
};

/// The payload is borrowed until the callback returns. Copy it before retaining
/// it. A callback may emit any number of messages through the writer. Use the
/// ordinary recv/send API when application work needs asynchronous suspension.
export using message_handler_fn = std::function<std::expected<void, std::error_code>(
    opcode, std::span<const std::byte>, message_writer&)>;

export class connection
{
public:
    explicit connection(io_context& ctx) noexcept;
    ~connection();

    connection(const connection&) = delete;
    auto operator=(const connection&) -> connection& = delete;
    connection(connection&& o) noexcept;

    auto async_connect(std::string_view url_str, const connect_options& opts = {})
        -> task<std::expected<void, std::error_code>>;
    auto async_accept(socket client_sock)
        -> task<std::expected<void, std::error_code>>;
#ifdef CNETMOD_HAS_SSL
    auto async_accept_tls(socket client_sock, ssl_context& ssl_ctx)
        -> task<std::expected<void, std::error_code>>;
#endif

    auto async_send_text(std::string_view text)
        -> task<std::expected<void, std::error_code>>;
    auto async_send_binary(std::span<const std::byte> data)
        -> task<std::expected<void, std::error_code>>;
    auto async_ping(std::span<const std::byte> payload = {})
        -> task<std::expected<void, std::error_code>>;
    auto async_send(opcode op, std::span<const std::byte> payload)
        -> task<std::expected<void, std::error_code>>;

    auto async_recv() -> task<std::expected<ws_message, std::error_code>>;
    auto async_process_messages(message_handler_fn handler,
        std::size_t max_message_size = 16 * 1024 * 1024)
        -> task<std::expected<void, std::error_code>>;
    auto async_echo_messages(std::size_t max_message_size = 16 * 1024 * 1024)
        -> task<std::expected<void, std::error_code>>;
    auto async_close(std::uint16_t code = close_code::normal,
        std::string_view reason = "")
        -> task<std::expected<void, std::error_code>>;

    void attach(socket sock, bool as_server = true) noexcept;
    /// Adopt a transport together with bytes already read past its HTTP upgrade.
    void attach(socket sock, bool as_server, std::span<const std::byte> received);
    void set_cancel_token(cancel_token* token) noexcept;
    void clear_cancel_token() noexcept;

    [[nodiscard]] auto is_open() const noexcept -> bool;
    [[nodiscard]] auto is_server() const noexcept -> bool;
    [[nodiscard]] auto handshake_path() const noexcept -> std::string_view;
    [[nodiscard]] auto handshake_query() const noexcept -> std::string_view;
    [[nodiscard]] auto handshake_headers() const noexcept
        -> const http::header_map&;
#ifdef CNETMOD_HAS_SSL
    [[nodiscard]] auto is_secure() const noexcept -> bool;
#endif

private:
    auto async_process_messages_impl(message_handler_fn handler,
        bool echo_messages, std::size_t max_message_size)
        -> task<std::expected<void, std::error_code>>;
    auto async_read_some(char* buffer, std::size_t length)
        -> task<std::expected<std::size_t, std::error_code>>;
    auto async_write_all(const char* data, std::size_t length)
        -> task<std::expected<void, std::error_code>>;
    auto handle_control_frame(const frame_header& header,
        const std::vector<std::byte>& payload)
        -> task<void>;
    void close_socket() noexcept;
    void reset_server_request_metadata();
    void store_server_request_metadata(const http::request_parser& request);

    io_context& ctx_;
    socket sock_;
    bool is_server_ = false;
    bool connected_ = false;
    bool close_sent_ = false;
    bool close_received_ = false;
    dynamic_buffer recv_buf_{1024};
    std::vector<std::byte> send_buf_;
    cancel_token* cancel_token_{nullptr};
    std::string handshake_path_;
    std::string handshake_query_;
    http::header_map handshake_headers_;
#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> ssl_ctx_;
    std::unique_ptr<ssl_stream> ssl_;
    bool secure_ = false;
#endif
};

} // namespace cnetmod::ws
