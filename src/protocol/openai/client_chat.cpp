/// cnetmod.protocol.openai client — Chat Completions, Responses and SSE streaming

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.protocol.http;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :client;
import :foundation;
import :messages;
import :chat;
import :responses;
import nlohmann.json;

namespace cnetmod::openai {

namespace {
    class chunked_body_decoder
    {
    public:
        auto feed(std::string_view bytes) -> std::expected<std::string, std::string>
        {
            pending_.append(bytes);
            std::string output;
            while (!complete_)
            {
                if (reading_trailers_)
                {
                    if (pending_.starts_with("\r\n"))
                    {
                        pending_.erase(0, 2);
                        complete_ = true;
                    }
                    else if (const auto end = pending_.find("\r\n\r\n");
                        end != std::string::npos)
                    {
                        pending_.erase(0, end + 4);
                        complete_ = true;
                    }
                    break;
                }
                if (!remaining_)
                {
                    const auto end = pending_.find("\r\n");
                    if (end == std::string::npos)
                        break;
                    auto size_text = std::string_view{pending_}.substr(0, end);
                    if (const auto extension = size_text.find(';');
                        extension != std::string_view::npos)
                        size_text = size_text.substr(0, extension);
                    std::size_t size = 0;
                    const auto parsed = std::from_chars(size_text.data(),
                        size_text.data() + size_text.size(), size, 16);
                    if (parsed.ec != std::errc{} ||
                        parsed.ptr != size_text.data() + size_text.size())
                        return std::unexpected("invalid HTTP chunk size");
                    pending_.erase(0, end + 2);
                    if (size == 0)
                    {
                        reading_trailers_ = true;
                        continue;
                    }
                    remaining_ = size;
                }

                const auto available = std::min(*remaining_, pending_.size());
                output.append(pending_, 0, available);
                pending_.erase(0, available);
                *remaining_ -= available;
                if (*remaining_ != 0)
                    break;
                if (pending_.size() < 2)
                    break;
                if (!pending_.starts_with("\r\n"))
                    return std::unexpected("invalid HTTP chunk terminator");
                pending_.erase(0, 2);
                remaining_.reset();
            }
            return output;
        }

        [[nodiscard]] auto complete() const noexcept -> bool
        {
            return complete_;
        }

    private:
        std::string pending_;
        std::optional<std::size_t> remaining_;
        bool reading_trailers_ = false;
        bool complete_ = false;
    };

    class sse_event_decoder
    {
    public:
        auto feed(std::string_view bytes) -> std::vector<std::string>
        {
            pending_.append(bytes);
            std::vector<std::string> events;
            while (true)
            {
                const auto newline = pending_.find('\n');
                if (newline == std::string::npos)
                    break;

                auto line = pending_.substr(0, newline);
                pending_.erase(0, newline + 1U);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                if (line.empty())
                {
                    if (!data_.empty())
                    {
                        data_.pop_back();
                        events.push_back(std::exchange(data_, {}));
                    }
                    continue;
                }
                if (!line.starts_with("data:"))
                    continue;

                auto value = std::string_view{line}.substr(5U);
                if (!value.empty() && value.front() == ' ')
                    value.remove_prefix(1U);
                data_.append(value);
                data_.push_back('\n');
            }
            return events;
        }

    private:
        std::string pending_;
        std::string data_;
    };

    [[nodiscard]] auto requests_stream_usage(const chat_request& request) -> bool
    {
        const auto options = request.extra_body.find("stream_options");
        if (options == request.extra_body.end() || !options->is_object())
            return false;
        const auto include_usage = options->find("include_usage");
        return include_usage != options->end() && include_usage->is_boolean() &&
            include_usage->get<bool>();
    }
} // namespace

auto client::chat(chat_request req)
    -> task<std::expected<chat_response, std::string>>
{
    if (auto r = co_await ensure_connected(); !r)
    {
        co_return std::unexpected(r.error());
    }

    req.stream = false;
    auto body = req.to_json();

    http::request http_req(http::http_method::POST,
        build_path("/chat/completions"));
    apply_common_headers(http_req, "application/json");
    http_req.set_body(std::move(body));

    auto send_r = co_await send_http_request(http_req);
    if (!send_r)
    {
        co_return std::unexpected(send_r.error());
    }

    auto resp_r = co_await read_full_response();
    if (!resp_r)
    {
        co_return std::unexpected(resp_r.error());
    }

    auto& [status, resp_body] = *resp_r;
    if (status != 200)
    {
        auto err = error_response::from_json(resp_body);
        co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
    }

    co_return chat_response::from_json(resp_body);
}

auto client::responses(response_request req)
    -> task<std::expected<response_result, std::string>>
{
    if (auto connected = co_await ensure_connected(); !connected)
        co_return std::unexpected(connected.error());

    http::request http_req(http::http_method::POST, build_path("/responses"));
    apply_common_headers(http_req, "application/json");
    http_req.set_body(req.to_json());
    if (auto sent = co_await send_http_request(http_req); !sent)
        co_return std::unexpected(sent.error());

    auto response = co_await read_full_response();
    if (!response)
        co_return std::unexpected(response.error());
    auto& [status, body] = *response;
    if (status < 200 || status >= 300)
    {
        auto error = error_response::from_json(body);
        co_return std::unexpected(std::format("HTTP {}: {}", status, error.message));
    }
    co_return response_result::from_json(body);
}

auto client::chat_stream(chat_request req, on_chunk_fn on_chunk)
    -> task<std::expected<std::string, std::string>>
{
    if (auto r = co_await ensure_connected(); !r)
        co_return std::unexpected(r.error());

    const bool expect_usage = requests_stream_usage(req);
    req.stream = true;
    auto body = req.to_json();

    http::request http_req(http::http_method::POST,
        build_path("/chat/completions"));
    apply_common_headers(http_req, "text/event-stream");
    http_req.set_body(std::move(body));

    auto send_r = co_await send_http_request(http_req);
    if (!send_r)
        co_return std::unexpected(send_r.error());

    auto header_r = co_await read_response_header();
    if (!header_r)
        co_return std::unexpected(header_r.error());

    const auto& header = *header_r;
    if (header.status != 200)
    {
        auto err_body = co_await read_remaining_body();
        auto err = error_response::from_json(err_body);
        co_return std::unexpected(
            std::format("HTTP {}: {}", header.status, err.message));
    }

    std::string full_content;
    chunked_body_decoder decoder;
    sse_event_decoder events;
    std::size_t body_bytes = 0;
    bool finish_observed = false;
    bool usage_observed = false;
    std::optional<std::string> pending_bytes{std::exchange(rbuf_, {})};

    for (;;)
    {
        std::string bytes;
        if (pending_bytes)
        {
            bytes = std::move(*pending_bytes);
            pending_bytes.reset();
        }
        else
        {
            if (finish_observed && expect_usage && !usage_observed)
            {
                cancel_token token;
                auto read_result = co_await with_timeout(ctx_,
                    std::chrono::seconds{1}, do_read_some(token), token);
                if (!read_result)
                    break;
                bytes = std::move(*read_result);
            }
            else
            {
                auto read_result = co_await do_read_some();
                if (!read_result || read_result->empty())
                    break;
                bytes = std::move(*read_result);
            }
        }

        if (header.chunked)
        {
            auto decoded = decoder.feed(bytes);
            if (!decoded)
                co_return std::unexpected(decoded.error());
            rbuf_.append(*decoded);
        }
        else
        {
            rbuf_.append(bytes);
            body_bytes += bytes.size();
        }

        bool semantic_complete = false;
        for (auto& data : events.feed(std::exchange(rbuf_, {})))
        {
            if (data == "[DONE]")
            {
                semantic_complete = true;
                continue;
            }

            auto chunk = chat_chunk::from_json(data);
            if (!chunk.delta_content.empty())
                full_content += chunk.delta_content;
            if (on_chunk)
                on_chunk(chunk);
            finish_observed =
                finish_observed || !chunk.finish_reason.empty();
            usage_observed = usage_observed || chunk.token_usage.has_value();
        }

        const bool framed_complete =
            (header.chunked && decoder.complete()) ||
            (!header.chunked && header.content_length &&
                body_bytes >= *header.content_length);
        semantic_complete = semantic_complete ||
            (finish_observed && (!expect_usage || usage_observed));
        if (semantic_complete || framed_complete)
        {
            close();
            co_return full_content;
        }
    }

    close();
    co_return full_content;
}

auto client::chat_stream_async(chat_request req, async_chunk_fn on_chunk)
    -> task<std::expected<std::string, std::string>>
{
    if (auto r = co_await ensure_connected(); !r)
        co_return std::unexpected(r.error());

    const bool expect_usage = requests_stream_usage(req);
    req.stream = true;
    auto body = req.to_json();

    http::request http_req(http::http_method::POST,
        build_path("/chat/completions"));
    apply_common_headers(http_req, "text/event-stream");
    http_req.set_body(std::move(body));

    auto send_r = co_await send_http_request(http_req);
    if (!send_r)
        co_return std::unexpected(send_r.error());

    auto header_r = co_await read_response_header();
    if (!header_r)
        co_return std::unexpected(header_r.error());

    const auto& header = *header_r;
    if (header.status != 200)
    {
        auto err_body = co_await read_remaining_body();
        auto err = error_response::from_json(err_body);
        co_return std::unexpected(
            std::format("HTTP {}: {}", header.status, err.message));
    }

    std::string full_content;
    chunked_body_decoder decoder;
    sse_event_decoder events;
    std::size_t body_bytes = 0;
    bool finish_observed = false;
    bool usage_observed = false;
    std::optional<std::string> pending_bytes{std::exchange(rbuf_, {})};

    for (;;)
    {
        std::string bytes;
        if (pending_bytes)
        {
            bytes = std::move(*pending_bytes);
            pending_bytes.reset();
        }
        else
        {
            if (finish_observed && expect_usage && !usage_observed)
            {
                cancel_token token;
                auto read_result = co_await with_timeout(ctx_,
                    std::chrono::seconds{1}, do_read_some(token), token);
                if (!read_result)
                    break;
                bytes = std::move(*read_result);
            }
            else
            {
                auto read_result = co_await do_read_some();
                if (!read_result || read_result->empty())
                    break;
                bytes = std::move(*read_result);
            }
        }

        if (header.chunked)
        {
            auto decoded = decoder.feed(bytes);
            if (!decoded)
                co_return std::unexpected(decoded.error());
            rbuf_.append(*decoded);
        }
        else
        {
            rbuf_.append(bytes);
            body_bytes += bytes.size();
        }

        bool semantic_complete = false;
        for (auto& data : events.feed(std::exchange(rbuf_, {})))
        {
            if (data == "[DONE]")
            {
                semantic_complete = true;
                continue;
            }

            auto chunk = chat_chunk::from_json(data);
            if (!chunk.delta_content.empty())
                full_content += chunk.delta_content;

            if (on_chunk)
            {
                const bool cont = co_await on_chunk(chunk);
                if (!cont)
                {
                    close();
                    co_return full_content;
                }
            }
            finish_observed =
                finish_observed || !chunk.finish_reason.empty();
            usage_observed = usage_observed || chunk.token_usage.has_value();
        }

        const bool framed_complete =
            (header.chunked && decoder.complete()) ||
            (!header.chunked && header.content_length &&
                body_bytes >= *header.content_length);
        semantic_complete = semantic_complete ||
            (finish_observed && (!expect_usage || usage_observed));
        if (semantic_complete || framed_complete)
        {
            close();
            co_return full_content;
        }
    }

    close();
    co_return full_content;
}

} // namespace cnetmod::openai
