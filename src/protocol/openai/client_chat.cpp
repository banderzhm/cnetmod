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

    private:
        std::string pending_;
        std::optional<std::size_t> remaining_;
        bool reading_trailers_ = false;
        bool complete_ = false;
    };
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

    auto& [status, content_type, chunked] = *header_r;
    if (status != 200)
    {
        auto err_body = co_await read_remaining_body();
        auto err = error_response::from_json(err_body);
        co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
    }

    std::string full_content;
    chunked_body_decoder decoder;
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
            auto read_result = co_await do_read_some();
            if (!read_result || read_result->empty())
                break;
            bytes = std::move(*read_result);
        }

        if (chunked)
        {
            auto decoded = decoder.feed(bytes);
            if (!decoded)
                co_return std::unexpected(decoded.error());
            rbuf_.append(*decoded);
        }
        else
        {
            rbuf_.append(bytes);
        }

        while (true)
        {
            auto nl = rbuf_.find('\n');
            if (nl == std::string::npos)
                break;

            auto line = rbuf_.substr(0, nl);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            rbuf_.erase(0, nl + 1);

            if (line.empty())
                continue;

            if (line.starts_with("data: "))
            {
                auto data = line.substr(6);

                if (data == "[DONE]")
                {
                    close();
                    co_return full_content;
                }

                auto chunk = chat_chunk::from_json(data);
                if (!chunk.delta_content.empty())
                {
                    full_content += chunk.delta_content;
                }
                if (on_chunk)
                    on_chunk(chunk);

                if (chunk.finish_reason == "stop" || chunk.finish_reason == "length")
                {
                    close();
                    co_return full_content;
                }
            }
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

    auto& [status, content_type, chunked] = *header_r;
    if (status != 200)
    {
        auto err_body = co_await read_remaining_body();
        auto err = error_response::from_json(err_body);
        co_return std::unexpected(std::format("HTTP {}: {}", status, err.message));
    }

    std::string full_content;
    chunked_body_decoder decoder;
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
            auto read_result = co_await do_read_some();
            if (!read_result || read_result->empty())
                break;
            bytes = std::move(*read_result);
        }

        if (chunked)
        {
            auto decoded = decoder.feed(bytes);
            if (!decoded)
                co_return std::unexpected(decoded.error());
            rbuf_.append(*decoded);
        }
        else
        {
            rbuf_.append(bytes);
        }

        while (true)
        {
            auto nl = rbuf_.find('\n');
            if (nl == std::string::npos)
                break;

            auto line = rbuf_.substr(0, nl);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            rbuf_.erase(0, nl + 1);

            if (line.empty())
                continue;

            if (line.starts_with("data: "))
            {
                auto data = line.substr(6);

                if (data == "[DONE]")
                {
                    close();
                    co_return full_content;
                }

                auto chunk = chat_chunk::from_json(data);
                if (!chunk.delta_content.empty())
                {
                    full_content += chunk.delta_content;
                }

                if (on_chunk)
                {
                    bool cont = co_await on_chunk(chunk);
                    if (!cont)
                    {
                        close();
                        co_return full_content;
                    }
                }

                if (chunk.finish_reason == "stop" || chunk.finish_reason == "length")
                {
                    close();
                    co_return full_content;
                }
            }
        }
    }

    close();
    co_return full_content;
}

} // namespace cnetmod::openai
