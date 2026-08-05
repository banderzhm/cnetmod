module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL
    #ifdef CNETMOD_ENABLE_QUIC

export module cnetmod.protocol.http.v3.session;

import std;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.coro.channel;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.wait_group;
import cnetmod.protocol.http;
import cnetmod.protocol.quic;
import cnetmod.protocol.http.v3.qpack;
import cnetmod.utils.flat_map;

namespace cnetmod::http::v3 {

using quic::quic_connection;
using quic::stream_id;

export struct http3_request
{
    http_method method{http_method::GET};
    std::string path{"/"};
    std::string scheme{"https"};
    std::string host;
    std::uint16_t port{443};
    http_version version{http_version::http_3};
    header_map headers;
    std::string body;
    header_map trailers;
};

export struct http3_response
{
    int status{status::ok};
    http_version version{http_version::http_3};
    header_map headers;
    std::string body;
    header_map trailers;
};

export using server_request_handler =
    std::function<std::error_code(http3_request&, http3_response&)>;
export using client_request_handler =
    std::function<task<std::expected<http3_response, std::error_code>>(const http3_request&)>;

export struct http3_settings
{
    std::uint64_t max_header_list_size{};
    std::uint64_t qpack_max_table_capacity{};
    std::uint64_t qpack_blocked_streams{};
};

export class http3_server_session
{
public:
    http3_server_session(quic_connection& conn, server_request_handler handler);
    auto run() -> task<void>;
    auto close() -> task<void>;
    auto send_goaway(stream_id last_stream) -> task<void>;
    /// Validates bytes from one peer-initiated unidirectional stream.  The
    /// transport supplies a complete stream prefix and calls this again as it
    /// grows; protocol violations are reported to the caller for connection
    /// close handling.
    [[nodiscard]] auto process_peer_unidirectional_stream(stream_id id,
        byte_view bytes) -> std::expected<void, std::error_code>;
    [[nodiscard]] auto get_active_streams_count() const noexcept -> std::size_t;

private:
    auto service_peer_stream(stream_id id) -> task<void>;
    quic_connection& conn_;
    server_request_handler handler_;
    qpack_encoder encoder_;
    qpack_decoder decoder_;
    http3_settings local_settings_{};
    std::optional<stream_id> control_stream_;
    std::optional<stream_id> qpack_encoder_stream_;
    std::optional<stream_id> qpack_decoder_stream_;
    bool control_stream_sent_{};
    bool closing_{};
    std::size_t active_streams_{};
    bool peer_control_stream_seen_{};
    bool peer_settings_seen_{};
    bool peer_qpack_encoder_stream_seen_{};
    bool peer_qpack_decoder_stream_seen_{};
    bool received_goaway_{};
    std::uint64_t goaway_stream_id_{std::numeric_limits<std::uint64_t>::max()};
    cnetmod::flat_map<stream_id, std::uint64_t> peer_unidirectional_stream_types_;
    cnetmod::flat_map<stream_id, std::size_t> peer_unidirectional_stream_bytes_;
    async_wait_group peer_streams_;
};

export class http3_client_session
{
public:
    http3_client_session(quic_connection& conn, client_request_handler handler);
    auto configure_local_settings(http3_settings settings) noexcept -> void;
    auto connect() -> task<std::expected<void, std::error_code>>;
    auto close() -> task<void>;
    auto close_all() -> task<void>;
    auto send_request(const http3_request& req) -> task<std::expected<http3_response, std::error_code>>;
    auto send_request(const http3_request& req, cnetmod::cancel_token& token)
        -> task<std::expected<http3_response, std::error_code>>;
    [[nodiscard]] auto process_peer_unidirectional_stream(stream_id id,
        byte_view bytes) -> std::expected<void, std::error_code>;
    [[nodiscard]] auto accepting_requests() const noexcept -> bool;

private:
    quic_connection& conn_;
    client_request_handler handler_;
    qpack_encoder encoder_;
    qpack_decoder decoder_;
    http3_settings settings_;
    std::optional<stream_id> control_stream_;
    std::optional<stream_id> qpack_encoder_stream_;
    std::optional<stream_id> qpack_decoder_stream_;
    bool control_stream_sent_{};
    bool received_goaway_{};
    std::uint64_t goaway_stream_id_{std::numeric_limits<std::uint64_t>::max()};
    bool peer_control_stream_seen_{};
    bool peer_settings_seen_{};
    bool peer_qpack_encoder_stream_seen_{};
    bool peer_qpack_decoder_stream_seen_{};
    cnetmod::flat_map<stream_id, std::uint64_t> peer_unidirectional_stream_types_;
    cnetmod::flat_map<stream_id, std::size_t> peer_unidirectional_stream_bytes_;
    channel<std::monostate> qpack_progress_{1};
    cnetmod::flat_map<stream_id,
        std::deque<std::vector<header_field>>>
        completed_headers_;
};

export auto make_http3_server_session(quic_connection& conn, server_request_handler handler)
    -> std::unique_ptr<http3_server_session>;
export auto make_http3_client_session(quic_connection& conn, client_request_handler handler)
    -> std::unique_ptr<http3_client_session>;
} // namespace cnetmod::http::v3
    #endif
#endif
