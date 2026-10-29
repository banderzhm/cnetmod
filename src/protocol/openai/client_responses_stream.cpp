module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.openai;

import std;
import cnetmod.protocol.http;
import cnetmod.core.error;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.json;
import :client;
import :foundation;
import :responses;
import :tool_contracts;

namespace cnetmod::openai {
namespace {

class response_chunk_decoder
{
public:
    auto feed(std::string_view bytes) -> std::expected<std::string, std::string>
    {
        pending_.append(bytes);
        std::string output;
        while (!complete_)
        {
            if (trailers_) { consume_trailers(); break; }
            if (!remaining_ && !read_size()) break;
            if (invalid_) return std::unexpected("invalid HTTP chunk size");
            const auto available = std::min(*remaining_, pending_.size());
            output.append(pending_, 0, available);
            pending_.erase(0, available);
            *remaining_ -= available;
            if (*remaining_ || pending_.size() < 2) break;
            if (!pending_.starts_with("\r\n"))
                return std::unexpected("invalid HTTP chunk terminator");
            pending_.erase(0, 2);
            remaining_.reset();
        }
        return output;
    }
    [[nodiscard]] auto complete() const noexcept -> bool { return complete_; }
    [[nodiscard]] auto trailing_bytes() const noexcept -> bool
    {
        return complete_ && !pending_.empty();
    }

private:
    auto read_size() -> bool
    {
        const auto end = pending_.find("\r\n");
        if (end == std::string::npos) return false;
        auto text = std::string_view{pending_}.substr(0, end);
        if (auto semicolon = text.find(';'); semicolon != std::string_view::npos)
            text = text.substr(0, semicolon);
        std::size_t size = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), size, 16);
        invalid_ = parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size();
        pending_.erase(0, end + 2);
        if (size == 0 && !invalid_) { trailers_ = true; consume_trailers(); return false; }
        remaining_ = size;
        return true;
    }
    void consume_trailers()
    {
        if (pending_.starts_with("\r\n")) { pending_.erase(0, 2); complete_ = true; }
        else if (auto end = pending_.find("\r\n\r\n"); end != std::string::npos) {
            pending_.erase(0, end + 4); complete_ = true;
        }
    }
    std::string pending_;
    std::optional<std::size_t> remaining_;
    bool trailers_ = false, complete_ = false, invalid_ = false;
};

class response_sse_decoder
{
public:
    auto feed(std::string_view bytes) -> std::vector<std::string>
    {
        pending_.append(bytes);
        std::vector<std::string> events;
        for (;;) {
            const auto end = pending_.find('\n');
            if (end == std::string::npos) break;
            auto line = pending_.substr(0, end);
            pending_.erase(0, end + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) {
                if (!data_.empty()) { data_.pop_back(); events.push_back(std::exchange(data_, {})); }
                continue;
            }
            if (!line.starts_with("data:")) continue;
            auto text = std::string_view{line}.substr(5);
            if (text.starts_with(' ')) text.remove_prefix(1);
            data_.append(text);
            data_.push_back('\n');
        }
        return events;
    }
private:
    std::string pending_, data_;
};

class response_event_state
{
public:
    auto ingest(std::string_view data) -> std::expected<std::string, std::string>
    {
        if (data == "[DONE]") return std::string{};
        auto parsed = cnetmod::json::parse_document(data);
        if (!parsed || !parsed->is_object()) return std::unexpected("invalid Responses SSE event");
        const auto& value = *parsed;
        const auto type = cnetmod::json::value_or(value, "type", std::string{});
        if (type == "response.output_text.delta") {
            auto delta = cnetmod::json::value_or(value, "delta", std::string{});
            text_.append(delta);
            return delta;
        }
        if (type == "response.output_item.added" || type == "response.output_item.done")
            remember_item(value);
        if (type == "response.function_call_arguments.delta" ||
            type == "response.function_call_arguments.done") remember_arguments(value, type);
        if (type == "response.completed" || type == "response.incomplete")
            return complete(value, type == "response.completed" ? "completed" : "incomplete");
        if (type == "error" || type == "response.failed")
            return std::unexpected("Responses stream reported " + type);
        return std::string{};
    }
    [[nodiscard]] auto completed() const noexcept -> bool { return completed_; }
    auto take() -> response_result { return std::move(result_); }

private:
    void remember_item(const json& value)
    {
        const auto* item = cnetmod::json::find(value, "item");
        if (!item || cnetmod::json::value_or(*item, "type", "") != "function_call") return;
        const auto index = cnetmod::json::value_or(value, "output_index", -1);
        if (index < 0) return;
        auto& call = calls_[index];
        call.id = cnetmod::json::value_or(*item, "call_id", "");
        call.type = "function";
        call.function.name = cnetmod::json::value_or(*item, "name", "");
        auto arguments = cnetmod::json::value_or(*item, "arguments", "");
        if (!arguments.empty()) call.function.arguments = std::move(arguments);
    }
    void remember_arguments(const json& value, std::string_view type)
    {
        const auto index = cnetmod::json::value_or(value, "output_index", -1);
        if (index < 0) return;
        auto& arguments = calls_[index].function.arguments;
        if (type.ends_with(".done")) arguments = cnetmod::json::value_or(value, "arguments", "");
        else arguments.append(cnetmod::json::value_or(value, "delta", ""));
    }
    auto complete(const json& value, std::string_view status) -> std::expected<std::string, std::string>
    {
        const auto* response = cnetmod::json::find(value, "response");
        if (!response || !response->is_object()) return std::unexpected("Responses completion missing response");
        const auto encoded = cnetmod::json::write_document(*response);
        if (!encoded) return std::unexpected("Responses completion serialization failed");
        result_ = response_result::from_json(*encoded);
        if (result_.status != status) return std::unexpected("Responses stream status mismatch");
        if (result_.output_text.empty()) result_.output_text = text_;
        if (result_.tool_calls.empty())
            for (auto& [index, call] : calls_) { (void)index; result_.tool_calls.push_back(std::move(call)); }
        completed_ = true;
        return std::string{};
    }
    response_result result_;
    std::string text_;
    std::map<int, tool_call> calls_;
    bool completed_ = false;
};

auto deliver_response_events(response_event_state& state,
    std::vector<std::string> events, client::async_response_delta_fn& callback,
    cancel_token& cancellation)
    -> task<std::expected<void, std::string>>
{
    for (const auto& event : events) {
        if (cancellation.is_cancelled())
            co_return std::unexpected("Responses stream cancelled");
        auto delta = state.ingest(event);
        if (!delta) co_return std::unexpected(delta.error());
        if (!delta->empty() && callback && !(co_await callback(*delta)))
            co_return std::unexpected("Responses stream cancelled by callback");
    }
    co_return std::expected<void, std::string>{};
}
} // namespace

auto client::responses_stream(response_request req, async_response_delta_fn on_delta)
    -> task<std::expected<response_result, std::string>>
{
    cancel_token cancellation;
    co_return co_await responses_stream(std::move(req), std::move(on_delta), cancellation);
}

auto client::responses_stream(response_request req, async_response_delta_fn on_delta,
    cancel_token& cancellation) -> task<std::expected<response_result, std::string>>
{
    struct connection_guard {
        client& owner;
        bool reusable = false;
        ~connection_guard() { if (!reusable) owner.close(); }
    } guard{*this};
    if (!is_connected()) {
        auto connected = co_await connect(opts_, cancellation);
        if (!connected) co_return std::unexpected(connected.error());
    }
    auto header = co_await begin_response_stream(req, cancellation);
    if (!header) co_return std::unexpected(header.error());
    auto result = co_await read_response_stream(*header, on_delta, cancellation);
    guard.reusable = result.has_value() && !header->connection_close &&
        (header->chunked || header->content_length.has_value());
    co_return result;
}

auto client::begin_response_stream(response_request& req, cancel_token& cancellation)
    -> task<std::expected<response_header, std::string>>
{
    req.extra_body["stream"] = true;
    http::request request{http::http_method::POST, build_path("/responses")};
    apply_common_headers(request, "text/event-stream");
    request.set_body(req.to_json());
    const auto timeout = std::chrono::seconds{opts_.timeout_seconds};
    auto sent = co_await with_timeout(ctx_, timeout, send_http_request(request, cancellation), cancellation);
    if (!sent) co_return std::unexpected("Responses stream write failed: " + sent.error().message());
    auto header = co_await with_timeout(ctx_, timeout, read_response_header(cancellation), cancellation);
    if (!header) co_return std::unexpected("Responses stream header failed: " + header.error().message());
    if (header->status < 200 || header->status >= 300) {
        auto body = co_await read_response_error_body(*header, cancellation);
        if (!body) co_return std::unexpected(std::format("HTTP {}: {}", header->status, body.error()));
        co_return std::unexpected(std::format("HTTP {}: {}", header->status, *body));
    }
    co_return *header;
}

auto client::read_response_error_body(const response_header& header,
    cancel_token& cancellation) -> task<std::expected<std::string, std::string>>
{
    constexpr std::size_t max_body = 1024 * 1024;
    if (!header.chunked && header.content_length && *header.content_length > max_body)
        co_return std::unexpected("Responses error body exceeds limit");
    response_chunk_decoder chunks;
    std::string body;
    std::optional<std::string> pending{std::exchange(rbuf_, {})};
    for (;;) {
        if (!header.chunked && header.content_length && body.size() >= *header.content_length)
            co_return body;
        std::string bytes;
        if (pending) { bytes = std::move(*pending); pending.reset(); }
        else {
            auto read = co_await with_timeout(ctx_, std::chrono::seconds{opts_.timeout_seconds},
                do_read_some(cancellation), cancellation);
            if (!read) {
                if (!header.chunked && !header.content_length &&
                    read.error() == make_error_code(errc::end_of_file)) co_return body;
                co_return std::unexpected("Responses error body read failed: " + read.error().message());
            }
            bytes = std::move(*read);
        }
        if (header.chunked) {
            auto decoded = chunks.feed(bytes);
            if (!decoded) co_return std::unexpected(decoded.error());
            bytes = std::move(*decoded);
        } else if (header.content_length && bytes.size() > *header.content_length - body.size())
            bytes.resize(*header.content_length - body.size());
        if (bytes.size() > max_body - body.size())
            co_return std::unexpected("Responses error body exceeds limit");
        body.append(bytes);
        if (header.chunked && chunks.complete()) co_return body;
    }
}

auto client::read_response_stream(const response_header& header,
    async_response_delta_fn& on_delta, cancel_token& cancellation)
    -> task<std::expected<response_result, std::string>>
{
    response_chunk_decoder chunks;
    response_sse_decoder sse;
    response_event_state state;
    std::size_t body_bytes = 0;
    std::optional<std::string> pending{std::exchange(rbuf_, {})};
    for (;;) {
        std::string bytes;
        if (pending) { bytes = std::move(*pending); pending.reset(); }
        else {
            auto read = co_await with_timeout(ctx_, std::chrono::seconds{opts_.timeout_seconds},
                do_read_some(cancellation), cancellation);
            if (!read) co_return std::unexpected("Responses stream read failed: " + read.error().message());
            bytes = std::move(*read);
        }
        if (header.chunked) {
            auto decoded = chunks.feed(bytes);
            if (!decoded) co_return std::unexpected(decoded.error());
            if (chunks.trailing_bytes()) co_return std::unexpected("unexpected bytes after HTTP chunks");
            bytes = std::move(*decoded);
        } else {
            body_bytes += bytes.size();
            if (header.content_length && body_bytes > *header.content_length)
                co_return std::unexpected("unexpected bytes after HTTP body");
        }
        auto delivered = co_await deliver_response_events(state, sse.feed(bytes), on_delta, cancellation);
        if (!delivered) co_return std::unexpected(delivered.error());
        const bool framed_complete = (header.chunked && chunks.complete()) ||
            (!header.chunked && header.content_length && body_bytes >= *header.content_length);
        if (state.completed() && (framed_complete ||
            (!header.chunked && !header.content_length))) co_return state.take();
        if (framed_complete) co_return std::unexpected("Responses stream ended before completion");
    }
}
} // namespace cnetmod::openai
