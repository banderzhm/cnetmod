module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:router;

import std;
import :semantics;
import :parser;
import :request;
import :response;
import :multipart;
import :sse;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;

namespace cnetmod::http {

export struct route_params {
    std::unordered_map<std::string, std::string> named;
    std::string wildcard;
    [[nodiscard]] auto get(std::string_view key) const noexcept -> std::string_view;
};

export class request_context {
public:
    request_context(io_context& ctx, socket& sock, const request_parser& parser,
                    response& resp, route_params params);
    request_context(io_context& ctx, socket& sock, std::string_view method,
                    std::string_view uri, const header_map& headers,
                    std::string_view body, response& resp, route_params params,
                    std::shared_ptr<request_body_stream> body_stream = {});
    [[nodiscard]] auto method() const noexcept -> std::string_view;
    [[nodiscard]] auto method_enum() const noexcept -> std::optional<http_method>;
    [[nodiscard]] auto path() const noexcept -> std::string_view;
    [[nodiscard]] auto query_string() const noexcept -> std::string_view;
    [[nodiscard]] auto uri() const noexcept -> std::string_view;
    [[nodiscard]] auto headers() const noexcept -> const header_map&;
    [[nodiscard]] auto body() const -> std::string_view;
    [[nodiscard]] auto has_body_stream() const noexcept -> bool;
    [[nodiscard]] auto receive_body_chunk() -> task<std::optional<request_body_chunk>>;
    [[nodiscard]] auto read_full_body() -> task<std::string_view>;
    [[nodiscard]] auto get_header(std::string_view key) const -> std::string_view;
    [[nodiscard]] auto param(std::string_view name) const noexcept -> std::string_view;
    [[nodiscard]] auto wildcard() const noexcept -> std::string_view;
    [[nodiscard]] auto params() const noexcept -> const route_params&;
    void text(int status_code, std::string_view text_body);
    void json(int status_code, std::string_view json_body);
    void html(int status_code, std::string_view html_body);
    void redirect(std::string_view location, int code = 302);
    void not_found();
    auto sse_begin(int status_code = status::ok) -> task<bool>;
    auto sse_send(std::string_view data, std::string_view event = {}) -> task<bool>;
    auto sse_json(std::string_view json_payload, std::string_view event = {}) -> task<bool>;
    auto sse_done() -> task<bool>;
    [[nodiscard]] auto parse_form() -> std::expected<const form_data*, std::error_code>;
    [[nodiscard]] auto resp() noexcept -> response&;
    [[nodiscard]] auto io_ctx() noexcept -> io_context&;
    [[nodiscard]] auto raw_socket() noexcept -> socket&;
private:
    void drain_available_body_chunks() const;
    void init_path_query(std::string_view uri);
    io_context& ctx_; socket& sock_; response& resp_; route_params params_;
    const header_map* headers_ptr_{}; std::string_view method_;
    mutable std::string body_storage_; mutable std::string_view body_;
    std::shared_ptr<request_body_stream> body_stream_;
    std::string uri_; std::string path_; std::string query_;
    std::optional<form_data> form_cache_;
    mutable bool body_stream_drained_ = false; bool sse_started_ = false;
};

export using handler_fn = std::function<task<void>(request_context&)>;
export using next_fn = std::function<task<void>()>;
export using middleware_fn = std::function<task<void>(request_context&, next_fn)>;

namespace detail {
enum class segment_kind { exact, param, wildcard };
struct segment { segment_kind kind; std::string value; };
struct route_score { int wildcard_count = 0; int param_count = 0; int literal_count = 0; int segment_count = 0; int method_cost = 0; std::uint64_t order = 0; };
}

export struct match_result { handler_fn handler; route_params params; };

export class router {
public:
    router() = default;
    auto get(std::string_view pattern, handler_fn fn) -> router&;
    auto post(std::string_view pattern, handler_fn fn) -> router&;
    auto put(std::string_view pattern, handler_fn fn) -> router&;
    auto del(std::string_view pattern, handler_fn fn) -> router&;
    auto patch(std::string_view pattern, handler_fn fn) -> router&;
    auto any(std::string_view pattern, handler_fn fn) -> router&;
    [[nodiscard]] auto match(http_method method, std::string_view path) const -> std::optional<match_result>;
    [[nodiscard]] auto match(std::string_view method, std::string_view path) const -> std::optional<match_result>;
private:
    struct route_entry { std::optional<http_method> method; std::vector<detail::segment> segments; handler_fn handler; detail::route_score score; std::uint64_t order = 0; };
    auto add(http_method method, std::string_view pattern, handler_fn fn) -> router&;
    auto add_route(std::optional<http_method> method, std::string_view pattern, handler_fn fn) -> router&;
    [[nodiscard]] auto find_exact(http_method method, std::string_view canonical_path) const -> const route_entry*;
    static auto try_match(const std::vector<detail::segment>& segs, const std::vector<std::string_view>& parts, route_params& out) -> bool;
    std::vector<route_entry> entries_;
    std::unordered_map<std::string, std::vector<std::size_t>> exact_index_;
    std::unordered_map<std::string, std::vector<std::size_t>> first_literal_index_;
    std::vector<std::size_t> generic_indices_;
    std::uint64_t next_order_ = 0;
};

} // namespace cnetmod::http
