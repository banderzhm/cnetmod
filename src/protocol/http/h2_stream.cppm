module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_NGHTTP2
#define NGHTTP2_NO_SSIZE_T
#include <nghttp2/nghttp2.h>
#endif

export module cnetmod.protocol.http:h2_stream;

import std;
import :types;
import :h2_types;

namespace cnetmod::http {

#ifdef CNETMOD_HAS_NGHTTP2

// =============================================================================
// h2_stream — Per-Stream State for HTTP/2
// =============================================================================
// Each HTTP/2 stream corresponds to one request/response exchange.
// The session creates an h2_stream when it sees BEGIN_HEADERS, accumulates
// headers and body via nghttp2 callbacks, then spawns a handler coroutine
// once the request is complete.

export class h2_stream {
public:
    explicit h2_stream(std::int32_t id) noexcept
        : stream_id_(id) {}

    // --- Request Building (called by nghttp2 callbacks) ---

    /// Add a header field (called by on_header_callback)
    void add_header(std::string_view name, std::string_view value) {
        // HTTP/2 pseudo-headers start with ':'
        if (!name.empty() && name[0] == ':') {
            if (name == ":method")    method_ = std::string(value);
            else if (name == ":path") path_ = std::string(value);
            else if (name == ":scheme") scheme_ = std::string(value);
            else if (name == ":authority") authority_ = std::string(value);
        } else {
            headers_[std::string(name)] = std::string(value);
        }
    }

    /// Append body data (called by on_data_chunk_recv_callback)
    void append_body(const std::uint8_t* data, std::size_t len) {
        body_.append(reinterpret_cast<const char*>(data), len);
    }

    /// Mark request as complete (all headers + body received)
    void mark_request_complete() noexcept { request_complete_ = true; }

    // --- Request Access ---

    [[nodiscard]] auto stream_id() const noexcept -> std::int32_t { return stream_id_; }
    [[nodiscard]] auto method() const noexcept -> std::string_view { return method_; }
    [[nodiscard]] auto scheme() const noexcept -> std::string_view { return scheme_; }
    [[nodiscard]] auto authority() const noexcept -> std::string_view { return authority_; }

    [[nodiscard]] auto path() const noexcept -> std::string_view {
        // Split off query string
        auto qpos = path_.find('?');
        return (qpos != std::string::npos) ? std::string_view(path_).substr(0, qpos) : path_;
    }

    [[nodiscard]] auto full_path() const noexcept -> std::string_view { return path_; }

    [[nodiscard]] auto query_string() const noexcept -> std::string_view {
        auto qpos = path_.find('?');
        if (qpos != std::string::npos)
            return std::string_view(path_).substr(qpos + 1);
        return {};
    }

    [[nodiscard]] auto headers() const noexcept -> const header_map& { return headers_; }
    [[nodiscard]] auto body() const noexcept -> std::string_view { return body_; }
    [[nodiscard]] auto request_complete() const noexcept -> bool { return request_complete_; }

    [[nodiscard]] auto get_header(std::string_view key) const -> std::string_view {
        auto it = headers_.find(std::string(key));
        if (it != headers_.end()) return it->second;
        return {};
    }

    [[nodiscard]] auto method_enum() const noexcept -> std::optional<http_method> {
        return string_to_method(method_);
    }

    // --- Response State ---

    struct response_data {
        int status_code = 200;
        header_map headers;
        std::string body;
        std::size_t body_offset = 0;  // Read position for DATA frame chunking
        bool sent = false;            // Has response been submitted to nghttp2?
    };

    [[nodiscard]] auto resp() noexcept -> response_data& { return response_; }
    [[nodiscard]] auto resp() const noexcept -> const response_data& { return response_; }

    /// Mark that the handler coroutine has finished and response is ready
    void mark_response_ready() noexcept { response_ready_ = true; }
    [[nodiscard]] auto response_ready() const noexcept -> bool { return response_ready_; }

    // --- Stream State ---

    void set_state(h2_stream_state s) noexcept { state_ = s; }
    [[nodiscard]] auto state() const noexcept -> h2_stream_state { return state_; }

private:
    std::int32_t stream_id_;
    h2_stream_state state_ = h2_stream_state::idle;

    // Request accumulation
    std::string method_;
    std::string path_;        // Full :path including query
    std::string scheme_;
    std::string authority_;
    header_map headers_;
    std::string body_;
    bool request_complete_ = false;

    // Response
    response_data response_;
    bool response_ready_ = false;
};

#endif // CNETMOD_HAS_NGHTTP2

} // namespace cnetmod::http
