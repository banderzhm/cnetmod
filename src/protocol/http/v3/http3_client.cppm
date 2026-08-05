module;
#include <cnetmod/config.hpp>
#ifdef CNETMOD_ENABLE_QUIC
    #ifdef CNETMOD_HAS_SSL
export module cnetmod.protocol.http.v3.client;
import std;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.coro.channel;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.protocol.quic;
import cnetmod.protocol.http.v3.session;

namespace cnetmod::http::v3 {
export struct http3_client_options
{
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds request_timeout{30000};
    std::uint64_t h3_initial_max_data{1048576};
    std::uint64_t h3_initial_max_stream_data{262144};
    std::uint64_t h3_max_header_list_size{80};
    std::uint64_t h3_qpack_max_table_capacity{65536};
    std::uint64_t h3_qpack_blocked_streams{100};
    bool verify_certificate{true};
    std::string tls_sni_host;
    /// Automatic retries are restricted to replay-safe methods.  This also
    /// applies when a future resumption ticket enables 0-RTT.
    bool retry_idempotent_requests{true};
};

export class http3_client
{
public:
    http3_client(io_context& context, ssl_context& tls, http3_client_options options = {});
    [[nodiscard]] auto connect(std::string_view host, std::uint16_t port)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto send_request(const http3_request& request)
        -> task<std::expected<http3_response, std::error_code>>;
    [[nodiscard]] auto send_request(const http3_request& request,
        cnetmod::cancel_token& token)
        -> task<std::expected<http3_response, std::error_code>>;
    [[nodiscard]] auto send_request(const http3_request& request,
        cnetmod::deadline deadline)
        -> task<std::expected<http3_response, std::error_code>>;
    [[nodiscard]] auto close() -> task<void>;
    [[nodiscard]] auto is_connected() const noexcept -> bool;
    [[nodiscard]] auto peer_host() const noexcept -> std::string_view;
    [[nodiscard]] auto peer_port() const noexcept -> std::uint16_t;
    [[nodiscard]] auto can_reuse_origin(std::string_view host, std::uint16_t port) const noexcept -> bool;

private:
    auto wait_for_connection_driver() -> task<void>;

    io_context& ctx_;
    ssl_context& tls_;
    http3_client_options options_;
    std::string host_;
    std::uint16_t port_{};
    std::shared_ptr<quic::quic_connection> connection_;
    std::shared_ptr<channel<std::monostate>> driver_completion_;
    std::unique_ptr<http3_client_session> session_;
};
} // namespace cnetmod::http::v3
    #endif
#endif
